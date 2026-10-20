# RetroAchievements in melonDS-RA

This fork of [melonDS](https://github.com/melonDS-emu/melonDS) adds native
[RetroAchievements](https://retroachievements.org) support to the desktop
(Qt) frontend, using the official
[rcheevos](https://github.com/RetroAchievements/rcheevos) `rc_client` runtime.

## Features

- **Login** with your RetroAchievements account. Only a login token is
  stored in `melonDS.toml`, never your password.
- **Automatic game identification.** The ROM is hashed with the official
  RetroAchievements DS hashing method. Plain `.nds`, `.zst` and archived ROMs
  all work.
- **Multi-set support.** Subsets attached to a game are loaded alongside the
  core set. The achievement list groups them per set and shows each set's
  progress, and finishing a subset shows its own mastery notification.
- **Hardcore mode** (on by default). It blocks loading savestates, cheats, slow
  motion, frame advance, the RAM search tool and the GDB stub, and it throttles
  pausing. Switching from casual to hardcore resets the game. Fast forward and
  *saving* states remain available.
- **Overlay notifications**, drawn by both the software and OpenGL renderers:
  - game placard with your progress and the active mode
  - achievement unlocks, game mastery and subset completion
  - leaderboard started, failed, submitted, and your new rank
  - server errors, and connection lost/restored
- **Challenge indicators.** Primed achievements show as small badges in the
  bottom-left corner.
- **Progress indicators.** Measured achievements briefly show `12/50`-style
  progress.
- **Leaderboard trackers.** Live timers and scores appear in the bottom-right
  corner, in a fixed-width font so they don't jitter.
- **Achievements and leaderboards browser** (*RetroAchievements → Achievements…*).
  - Achievements are grouped by state and by set, with badges, points, unlock
    dates, rarity and measured progress.
  - The leaderboards tab fetches the rankings around you from the server.
- **Rich Presence**, sent to the site automatically. It is also shown in the
  achievements window.
- **Savestates carry achievement state** (hit counts, leaderboard progress),
  so casual play resumes correctly after a load.
- **Encore**, **unofficial (unpromoted)** and **spectator** modes.
- **Sound effects** for unlocks, mastery and leaderboard submissions. These are
  original synthesized sounds bundled with the build.
- **RAIntegration toolkit support** (Windows x86/x64): the RetroAchievements
  developer toolkit.
  - It is loaded through rcheevos' `rc_client_begin_load_raintegration`.
  - The settings can download or update `RA_Integration-x64.dll`, using the
    same `latestintegration` lookup as the official RAInterface.
  - At startup melonRA offers the download if the DLL is missing or outdated.
  - Its tools appear under *RetroAchievements → Developer toolkit*.
  - The toolkit's memory inspector can write to emulated RAM.

## Using it

1. Open *RetroAchievements → Log in…* and sign in.
2. Load a DS game that has an achievement set. A placard appears in the top
   right with your progress and the current mode.
3. Change the mode or the notification options under *RetroAchievements →
   Settings…*.

Only the first emulator instance runs achievements. Extra local-multiplayer
instances play without them.

## Implementation notes

| Piece | Where |
|---|---|
| rcheevos (vendored, MIT) | `src/rcheevos/` (`VERSION.txt` holds the upstream commit) |
| `rc_client` wrapper, events, hardcore logic | `src/frontend/qt_sdl/RetroAchievements/RAClient.*` |
| HTTP: WinHTTP on Windows, Qt Network elsewhere | `RetroAchievements/RAHttp.*` |
| Badge/avatar download and disk cache | `RetroAchievements/RAImageCache.*` |
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
as `[data][u32 length]["RCHV"]`. melonDS-RA strips it before parsing the
state, so states without the block still load.

## Hardcore compliance

The client identifies itself to the server as:

```
melonDS-RA/0.1.0 (<OS>) melonDS/<version> rcheevos/<version>
```

`MELONDS_RA_VERSION` in `RAClient.h` must only ever increase.

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
| A. Offline queueing | ⚠️ rcheevos retries failed unlocks for the rest of the session. Unlocks still pending when melonRA is closed are lost; there is no on-disk queue yet. |
| A. Hit counts in savestates | ✅ |
| A. Toolkit (RAIntegration) support | ✅ Windows x86/x64; includes downloader/updater |
| A. Standard save file formats | ✅ unchanged melonDS `.sav` |
| B. Cheats, rewind, slow motion, frame advance blocked | ✅ (melonDS has no rewind) |
| B. Loading states always blocked; saving allowed | ✅ |
| B. Rich Presence and leaderboards can't be disabled in hardcore | ✅ (trackers forced visible) |
| B. Casual → hardcore resets the game | ✅ |
| B. Memory editors, debuggers, input playback blocked | ✅ RAM search and GDB stub blocked; melonDS has no input playback |
| C. Unique, numeric User-Agent | ✅ `melonDS-RA/x.y.z`; never used another client's UA |
| D. 6 months public | ✅ via upstream melonDS |
| E. Hardcore on by default, mode shown at game start | ✅ |
| F. No monetization; licenses list; privacy policy | ✅ [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md), [PRIVACY.md](PRIVACY.md) |

## Building

Build exactly like upstream melonDS (see [BUILD.md](BUILD.md)). rcheevos is
built from `src/rcheevos` automatically and needs no extra dependencies. On
Windows the build also links `winhttp`, which is part of the system.
