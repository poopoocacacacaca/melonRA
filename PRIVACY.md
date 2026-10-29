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

When you open the Achievements window, or while the game list shows achievement
progress (its Achievements column or the mastery stars), melonRA downloads RetroAchievements'
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

### Discord

While the Discord app is open on your computer, melonRA tells it which game you're
playing, since when, and for games with achievements, the RetroAchievements Rich
Presence text, your achievement progress and Hardcore or Casual mode. During netplay
it also says you're in a room and how many players are in it (never their names).
While you host a room that isn't full, it also gives Discord a join code with your
public and home network IP addresses and port, so friends can press Join: Discord
passes it to whoever joins, and they see your address as with any netplay
connection. Discord shows this on your profile. melonRA talks only to the Discord app on your own computer,
never to Discord's servers. The images are links to the game's box art (GameTDB or
libretro thumbnails), its RetroAchievements icon and the melonRA logo on GitHub,
which Discord loads. If cover downloads are allowed and a game's cover wasn't
downloaded yet, melonRA checks GameTDB for it. Turn this off in Config →
Interface Settings.

So that Discord can start melonRA when you press Join, melonRA registers itself
with your system as the program for its Discord invites (a registry entry under
your user on Windows, a file in Discord's folder on macOS, and a hidden desktop
entry on Linux).

Discord's handling of it is covered by the
[Discord privacy policy](https://discord.com/privacy).

### Online play servers

If you pick an online play server in Config → Wi-Fi Settings (Wiimmfi, Kaeru
WFC or a custom one), games that go online connect to that service instead of
Nintendo's closed servers, exactly as a real DS set up for it would. melonRA
itself sends nothing to these services; what the game sends is covered by the
service's own terms.

### Netplay (Experimental)

Netplay connects your computer directly to the other players' computers, with
no server in between. Only while you host or join a session, and only with the
players in it, melonRA shares:

- your IP address, as any direct connection does (the host's address is what
  players type to join, and the host sees the address of everyone who joins)
- the name you enter for the session
- your DS settings: nickname, message, birthday, favorite color and language
- your save file for the game being played, so every computer starts your
  console from it
- a fingerprint (SHA-1) of your copy of the game, to check that everyone has
  the same one; the game itself is never sent
- your button and touch inputs while playing, and checksums of the consoles'
  state to notice when they stop matching

When you host, melonRA also asks your router (over UPnP, inside your home
network) to forward the room's port to your computer until the room closes; this
can be turned off in the Host window. To learn your public IP address when the
router doesn't say, it asks a public STUN server (Google's `stun.l.google.com`
or Cloudflare's `stun.cloudflare.com`), which sees your IP address and nothing
else.

Nothing about your RetroAchievements account is sent. The connection isn't
encrypted. While a game runs, melonRA keeps the other players' saves in memory
only, and never writes them to disk.

### Other features

Other melonDS features that use the network work exactly as in upstream
melonDS. Local wireless, LAN and the DSi title downloader only connect when
you use them.

## What melonRA stores on your computer

- `melonRA.toml` in the melonRA folder holds your RetroAchievements username,
  your login token and your settings. Your password is **never** stored. Log
  out to delete the token.
- Settings backups (Config → Back Up Settings) are only written where you
  choose. They hold your settings, and your RetroAchievements username and
  login token only if you tick "Include my RetroAchievements login". Restoring
  one keeps your previous settings as `melonRA.toml.bak`.
- `gamelist.cache` holds the game list from the last scan (each ROM's path,
  size, date, title, icon, game code and region), so the list shows up at
  once. You can delete it at any time.
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
  the same login token kept in `melonRA.toml`, never your password; logging out
  deletes it. You can
  delete this folder at any time, or turn the feature off in RetroAchievements
  Settings.
- Savestates may contain achievement progress (hit counts) for the game.
- `melonRA.toml` also keeps the name, address and ports you last used for
  netplay, to fill them in next time.

Nothing is kept anywhere else, and nothing leaves your computer except the
traffic described above.

## GDPR

melonRA itself collects and retains no personal data, so there is nothing
for the melonRA developers to export or delete. For data held by
RetroAchievements (your account, unlocks, and so on), contact
RetroAchievements through their site.

## Monetization

melonRA is free and has no paid features, in-app purchases or donations
tied to functionality.

## Contact

Open an issue at <https://github.com/ppkkkkk/melonRA/issues>.
