#!/bin/bash
# Uploads build files to the GitHub release for the tag being built.
#
#   tools/ci-release.sh FILE...
#
# Runs in GitHub Actions on tag pushes (tags look like v0.1.0). The release
# is created on first use (titled "melonRA 0.1.0", notes taken from the
# matching section of CHANGELOG.md), and later workflows add their files to
# it. Fails if the tag doesn't match MELONRA_VERSION in CMakeLists.txt.
set -euo pipefail

if [[ "${GITHUB_REF_TYPE:-}" != "tag" ]]; then
    echo "Not a tag build, nothing to release."
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
} >> "$notes"

# several workflows publish to the same release; whichever runs first creates it
if ! gh release view "$tag" --repo "$GITHUB_REPOSITORY" > /dev/null 2>&1; then
    gh release create "$tag" --repo "$GITHUB_REPOSITORY" --verify-tag \
        --title "melonRA $version" --notes-file "$notes" || true
fi

for attempt in 1 2 3; do
    if gh release upload "$tag" "$@" --repo "$GITHUB_REPOSITORY" --clobber; then
        exit 0
    fi
    sleep $((attempt * 10))
done
echo "Failed to upload release files" >&2
exit 1
