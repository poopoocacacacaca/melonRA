#!/bin/bash
# Uploads build files to a GitHub release.
#
#   tools/ci-release.sh FILE...
#
# Runs in GitHub Actions:
#  - on version tags (v0.1.0, ...): uploads to the release for that tag. The
#    release is created on first use (titled "melonRA 0.1.0", notes taken from
#    the matching section of CHANGELOG.md). Fails if the tag doesn't match
#    MELONRA_VERSION in CMakeLists.txt.
#  - on pushes to master: updates the "nightly" pre-release. Each platform's
#    files are replaced as its build finishes and labelled with the commit they
#    were built from; a file is only skipped if it's already from a newer
#    commit. The tag, title and notes follow the newest commit published.
set -euo pipefail

repo="$GITHUB_REPOSITORY"

if [[ "${GITHUB_REF_TYPE:-}" == "branch" && "${GITHUB_REF_NAME:-}" == "master" ]]; then
    notes="$(mktemp)"
    {
        echo "Automatic build of the latest \`master\` commit. It has the newest changes before they"
        echo "make it into a release, so it may be less stable."
        echo
        echo "- **Windows:** \`melonRA-windows-x86_64.zip\` (or \`-arm64\`). Unzip it anywhere and run \`melonRA.exe\`."
        echo "- **Linux:** \`melonRA-linux-x86_64.AppImage\` (or \`-aarch64\`). Make it executable and run it."
        echo
        echo "**Last updated:** $(date -u '+%Y-%m-%d %H:%M') UTC, from commit [\`${GITHUB_SHA::7}\`](https://github.com/$repo/commit/$GITHUB_SHA)."
        echo
        echo "A new nightly is built for every commit to \`master\`. Each file is replaced as its build"
        echo "finishes, about 10-15 minutes after the commit, so for a short while the platforms can be"
        echo "one commit apart. Built from ${GITHUB_SHA}."
    } > "$notes"

    if ! gh release view nightly --repo "$repo" > /dev/null 2>&1; then
        gh release create nightly --repo "$repo" --prerelease --target "$GITHUB_SHA" \
            --title "melonRA nightly (${GITHUB_SHA::7})" --notes-file "$notes" || true
    else
        # move the tag, title and notes forward, never back: platforms finish
        # in any order, and a slow one may still be on an older commit
        current="$(gh api "repos/$repo/git/ref/tags/nightly" --jq .object.sha)"
        status="$(gh api "repos/$repo/compare/$current...$GITHUB_SHA" --jq .status || echo unknown)"
        if [[ "$status" == "ahead" || "$status" == "diverged" ]]; then
            gh api -X PATCH "repos/$repo/git/refs/tags/nightly" -f sha="$GITHUB_SHA" -F force=true > /dev/null
        fi
        if [[ "$status" != "behind" ]]; then
            gh release edit nightly --repo "$repo" --prerelease \
                --title "melonRA nightly (${GITHUB_SHA::7})" --notes-file "$notes"
        fi
    fi

    # Each file is labelled with the commit it was built from, and the updater
    # goes by that label. Replace a file unless it's already from a newer
    # commit than this build (a later push whose build finished first).
    assets="$(gh api "repos/$repo/releases/tags/nightly" --jq '.assets[] | "\(.name)\t\(.label)"' || true)"
    files=()
    for f in "$@"; do
        name="$(basename "$f")"
        existing="$(printf '%s\n' "$assets" | awk -F'\t' -v n="$name" '$1 == n { print $2 }' \
                    | sed -n 's/.*commit \([0-9a-f]\{7,40\}\).*/\1/p')"
        if [[ -n "$existing" ]]; then
            cmp="$(gh api "repos/$repo/compare/$existing...$GITHUB_SHA" --jq .status || echo unknown)"
            if [[ "$cmp" == "behind" || "$cmp" == "identical" ]]; then
                echo "$name is already from commit $existing, as new as or newer than this build; skipping it."
                continue
            fi
        fi
        files+=("$f#$name (commit ${GITHUB_SHA::7})")
    done
    if [[ ${#files[@]} -eq 0 ]]; then
        exit 0
    fi

    for attempt in 1 2 3; do
        if gh release upload nightly "${files[@]}" --repo "$repo" --clobber; then
            exit 0
        fi
        sleep $((attempt * 10))
    done
    echo "Failed to upload nightly files" >&2
    exit 1
fi

if [[ "${GITHUB_REF_TYPE:-}" != "tag" ]]; then
    echo "Not a tag or master build, nothing to release."
    exit 0
fi

root="$(cd "$(dirname "$0")/.." && pwd)"
tag="$GITHUB_REF_NAME"
version="${tag#v}"

expected="$(sed -n 's/^set(MELONRA_VERSION "\(.*\)")$/\1/p' "$root/CMakeLists.txt")"
if [[ "$version" != "$expected" ]]; then
    echo "Tag $tag does not match MELONRA_VERSION $expected in CMakeLists.txt" >&2
    exit 1
fi

# release notes: the CHANGELOG.md section for this version
notes="$(mktemp)"
awk -v ver="$version" '
    $0 == "## " ver { found = 1; next }
    found && /^## / { exit }
    found { print }
' "$root/CHANGELOG.md" > "$notes"
if [[ ! -s "$notes" ]]; then
    echo "melonRA $version" > "$notes"
fi
{
    echo
    echo "**Windows:** download \`melonRA-$version-windows-x86_64.zip\` (or \`-arm64\` for ARM devices), unzip it anywhere and run \`melonRA.exe\`."
    echo
    echo "**Linux:** download \`melonRA-$version-linux-x86_64.AppImage\` (or \`-aarch64\`), make it executable and run it."
} >> "$notes"

# several workflows publish to the same release; whichever runs first creates it
if ! gh release view "$tag" --repo "$repo" > /dev/null 2>&1; then
    gh release create "$tag" --repo "$repo" --verify-tag \
        --title "melonRA $version" --notes-file "$notes" || true
fi

for attempt in 1 2 3; do
    if gh release upload "$tag" "$@" --repo "$repo" --clobber; then
        exit 0
    fi
    sleep $((attempt * 10))
done
echo "Failed to upload release files" >&2
exit 1
