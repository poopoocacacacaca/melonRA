# melonRA Privacy Policy

_Last updated: 2026-10-20_

melonRA is a free, open-source emulator. It has **no telemetry, analytics,
advertising or accounts of its own**, and the melonRA developers run no
servers and receive no data from you.

## What melonRA sends, and to whom

melonRA talks to the network for RetroAchievements and to check for updates.

### RetroAchievements

This traffic only happens while RetroAchievements is enabled and you are
logged in. All of it goes over HTTPS to **RetroAchievements.org** (`retroachievements.org` and
`media.retroachievements.org`):

- your RetroAchievements username, and your password the first time you log
  in (afterwards, the login token the server issued)
- a hash (fingerprint) of the ROM you load, used to identify the game. The ROM
  itself is never uploaded.
- achievement unlocks, leaderboard submissions, and periodic "Rich Presence"
  status text describing what you are doing in the game
- the client version and operating system name, sent in the User-Agent header

melonRA also downloads achievement badges, game icons and user avatars from
`media.retroachievements.org`.

When you open the Achievements window, melonRA downloads RetroAchievements'
list of DS game fingerprints and matches the games in your game list against
it on your computer (the fingerprints of your ROMs aren't uploaded), and
fetches your progress for DS games. Selecting a game fetches its achievements
and which of them you've unlocked.

If you enable the RAIntegration developer toolkit, the toolkit is downloaded
from retroachievements.org and runs under RetroAchievements' own terms.

RetroAchievements' handling of this data is covered by
[their privacy policy](https://retroachievements.org/privacy). melonRA has
no control over their servers, retention periods or server locations.

### Update checks

At startup (unless you turned this off) and when you click *Check for Updates
Now*, melonRA asks **GitHub** (`api.github.com`) for the latest melonRA
release or nightly build, and downloads updates from `github.com`. These
requests carry no account or personal information, only the melonRA version
in the User-Agent header (like any website, GitHub sees your IP address).
GitHub's handling of them is covered by the
[GitHub privacy statement](https://docs.github.com/site-policy/privacy-policies/github-general-privacy-statement).

### Game covers

When the game list is in grid view and you agree to it (melonRA asks the first
time), melonRA downloads box art for the games it shows from **GameTDB** (`art.gametdb.com`, by game code) and the **libretro
thumbnails** (`thumbnails.libretro.com`, by title). Both can be turned off in
View → Game Covers. The covers are kept in the `covers` folder.

### Other features

Other melonDS features that use the network work exactly as in upstream
melonDS. Local wireless, LAN/netplay and the DSi title downloader only
connect when you use them.

## What melonRA stores on your computer

- `melonRA.toml` in the melonRA folder holds your RetroAchievements username,
  your login token and your settings. Your password is **never** stored. Log
  out to delete the token.
- `retroachievements/cache/` holds downloaded badge and avatar images. You can
  delete this folder at any time.
- `retroachievements/library_hashes.json` holds the fingerprints of the ROMs in
  your game list, so they don't have to be read again. You can delete it at any
  time.
- `retroachievements/pending_submissions.json` exists only while there are
  unlocks or leaderboard entries that couldn't reach the server yet. It holds
  your username, the achievement or leaderboard IDs, the score, a game hash and
  a timestamp, but never your password or token. It is deleted once
  everything has been submitted.
- `retroachievements/offline/` holds the RetroAchievements server's answers
  for games you've played (your account and points, the games' achievements,
  your unlocks), so achievements also work without a connection. It includes
  the same login token kept in `melonRA.toml`, never your password. You can
  delete this folder at any time, or turn the feature off in RetroAchievements
  Settings.
- Savestates may contain achievement progress (hit counts) for the game.

Nothing is kept anywhere else, and nothing leaves your computer except the
RetroAchievements and update traffic described above.

## GDPR

melonRA itself collects and retains no personal data, so there is nothing
for the melonRA developers to export or delete. For data held by
RetroAchievements (your account, unlocks, and so on), contact
RetroAchievements through their site.

## Monetization

melonRA is free and has no paid features, in-app purchases or donations
tied to functionality.

## Contact

Open an issue at <https://github.com/poopoocacacacaca/melonRA/issues>.
