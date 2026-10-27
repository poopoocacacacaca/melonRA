# melonRA developer notes

How the RetroAchievements integration is put together. For what it does and how to use it, see
the [README](README.md).

melonRA uses the official [rcheevos](https://github.com/RetroAchievements/rcheevos) `rc_client`
runtime in the Qt desktop frontend. Only the first emulator instance runs achievements; extra
local-multiplayer instances play without them.

## Code layout

| Piece | Where |
|---|---|
| rcheevos (vendored, MIT) | `src/rcheevos/` (`VERSION.txt` holds the upstream commit) |
| `rc_client` wrapper, events, hardcore logic | `src/frontend/qt_sdl/RetroAchievements/RAClient.*` |
| HTTP: WinHTTP on Windows, Qt Network elsewhere | `RetroAchievements/RAHttp.*` |
| Badge/avatar download and disk cache | `RetroAchievements/RAImageCache.*` |
| Saved (offline) submission queue | `RetroAchievements/RAPendingQueue.*` |
| Saved server answers for offline sessions | `RetroAchievements/RAOfflineCache.*` |
| Overlay widgets (popups, indicators, trackers) | `RetroAchievements/RAOverlay.*` |
| Login, settings, achievements/leaderboards windows | `RetroAchievements/RADialogs.*` |

**Hooks into melonDS**

- `EmuThread.cpp`
  - calls `rc_client_do_frame` after every emulated frame, including
    fast-forwarded ones
  - calls `rc_client_idle` while paused
  - throttles pausing, and blocks slow motion and frame advance in hardcore
  - loads, resets or unloads the game on boot, reset, cart swap and stop
- `EmuInstance.cpp`
  - hashes the ROM in `loadROM`
  - appends the rcheevos progress to savestates
  - blocks state loading, undo, cheats and the GDB stub in hardcore
- `Window.cpp`
  - adds the *RetroAchievements* menu
  - when a blocked feature is used in hardcore, offers to switch to casual mode
- `Screen.cpp` draws the overlay. The OpenGL path renders it into a texture
  and reuses melonDS's OSD shader.

**Memory map** (`rc_consoleinfo.c`)

- RA `$0000000` maps to DS main RAM: 4 MB on a DS, 16 MB on a DSi.
- RA `$1000000` maps to the ARM9 data TCM.
- Memory is only read from the emulator thread.

**Savestate format.** The rcheevos block is appended to the end of the file
as `[data][u32 length]["RCHV"]`. melonRA strips it before parsing the
state, so states without the block still load.

## Offline queue

rcheevos retries failed unlocks itself, but only in memory. melonRA adds a
persistent queue on top, in `retroachievements/pending_submissions.json`:

- Every unlock (`awardachievement`) and leaderboard submission
  (`submitlbentry`) is recorded **before** it is sent.
- Entries are removed once the server has given a definitive answer,
  including "already unlocked".
- Entries are kept on no connection, timeouts, HTTP 5xx and 429. rcheevos'
  own in-session retries map onto the same entry, so there are no duplicates.
- The queue stores what was earned and when: achievement or leaderboard ID,
  hardcore flag or score, game hash, and time. It never stores the login token.
- On the next login, or when the connection comes back, entries are rebuilt
  with rcheevos' request builders. The "seconds since unlock" value is updated
  and the request re-signed, so the server records the original unlock time.
- Each entry has an HMAC-SHA256 under a random per-install key
  (`RetroAchievements.QueueKey` in `melonRA.toml`). Corrupted or edited entries
  are discarded instead of submitted.

## Offline sessions

Games played online once can start their achievements without a connection
(Settings → "Play offline with saved achievement data", on by default).
Every request goes through `Manager::send`, which hands it to
`OfflineCache` in `retroachievements/offline/`:

- Successful answers to read-only requests are saved: `login2` (token logins
  only), `gameid`, `achievementsets`, `patch`, `startsession`, and the
  achievements window's `hashlibrary`, `allprogress`, `gameinfolist` and
  `lbinfo`. The key is the request minus the token, password and client
  version (and, for `startsession`, the hardcore flag and ROM hash).
- When a request gets no answer or an HTTP 5xx, the saved answer is returned to
  rcheevos as if the server had sent it, and melonRA shows that it's playing
  offline. Unlocks, pings and leaderboard entries are never answered from the
  cache; they fail as before and go to the offline queue.
- A saved `login2` is only used for a token login with the same token. A
  password login always needs the server.
- Unlocks sent while offline are also recorded per user and merged into the
  saved `startsession` answer, with `ServerNow` set to the current time, so
  they stay unlocked in later offline sessions instead of triggering again.
  They're dropped from this record once an online `startsession` lists them.
- The first live answer after an offline stretch switches back to online and
  submits the offline queue.

## Hardcore compliance

The client identifies itself to the server as:

```
melonRA/<version> (<OS>) melonDS/<version> rcheevos/<version>
```

The version comes from `MELONRA_VERSION` in the top-level `CMakeLists.txt` and must only ever increase.

Until RetroAchievements approves this client for hardcore, the server records
hardcore unlocks as casual ones. To apply, follow the
[hardcore compliance requirements](https://docs.retroachievements.org/general/hardcore-compliance-requirements.html)
and contact [RAdmin](https://retroachievements.org/messages/create?to=RAdmin).
The emulator, or its parent emulator, must have been public for at least six
months; melonDS qualifies. Disclose that this is a fork of melonDS when
applying.

## Compliance checklist

| Requirement | Status |
|---|---|
| A. Achievement list accessible in-emulator | ✅ RetroAchievements → Achievements… |
| A. Triggers, measured progress, challenge indicators | ✅ |
| A. Rich Presence and leaderboards | ✅ |
| A. Offline queueing | ✅ Unlocks and leaderboard entries are saved to disk before sending. If the app closes before they're delivered, they're resubmitted (re-signed) at the next login or reconnect; see [Offline queue](#offline-queue). Sessions can also start offline from saved data; see [Offline sessions](#offline-sessions). |
| A. Hit counts in savestates | ✅ |
| A. Toolkit (RAIntegration) support | ✅ Windows x86/x64; includes downloader/updater |
| A. Standard save file formats | ✅ unchanged melonDS `.sav` |
| B. Cheats, rewind, slow motion, frame advance blocked | ✅ Cheats (menu, cheat files, per-game Game Settings) stay off; slow motion and frame advance blocked; the game runs at the DS's own speed and only fast forward changes it (a custom target speed is ignored, a fast-forward speed below full speed is raised); melonDS has no rewind |
| B. Loading states always blocked; saving allowed | ✅ |
| B. Rich Presence and leaderboards can't be disabled in hardcore | ✅ (trackers forced visible) |
| B. Casual → hardcore resets the game | ✅ |
| B. Quick resume switches to casual | ✅ not applicable: melonDS has no resume feature |
| B. Pausing can't be used as slowdown | ✅ pausing is rate-limited with rcheevos' `rc_client_can_pause` |
| B. Memory editors, debuggers, input playback blocked | ✅ RAM search and GDB stub blocked; melonDS has no input playback |
| C. Unique, numeric User-Agent | ✅ `melonRA/x.y.z`; never used another client's UA |
| D. 6 months public | ✅ via upstream melonDS |
| E. Hardcore on by default, mode shown at game start | ✅ |
| Blocked features explain themselves | ✅ a short on-screen note ("Loading save states doesn't work in Hardcore mode") for hotkeys and menus alike |
| F. No monetization; licenses list; privacy policy | ✅ [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md), [PRIVACY.md](PRIVACY.md) |

## Building

Build instructions are in the [README](README.md#building) and [BUILD.md](BUILD.md). rcheevos is
built from `src/rcheevos` automatically and needs no extra dependencies. On Windows the build also
links `winhttp`, which is part of the system.
