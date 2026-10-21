<p align="center"><img src="res/melonRA-logo.png" alt="melonRA" width="480"></p>
<h3 align="center">melonRA - fork of melonDS with RetroAchievements integration</h3>
<p align="center">
<a href="https://retroachievements.org"><img src="https://img.shields.io/badge/RetroAchievements-supported-%23f2b632.svg"></a>
<a href="https://github.com/melonDS-emu/melonDS"><img src="https://img.shields.io/badge/based%20on-melonDS%201.1-%235c913b.svg"></a>
<a href="https://www.gnu.org/licenses/gpl-3.0"><img src="https://img.shields.io/badge/License-GPL%20v3-%23ff554d.svg"></a>
<br>
<a href="https://github.com/poopoocacacacaca/melonRA/actions/workflows/build-windows.yml"><img src="https://github.com/poopoocacacacaca/melonRA/actions/workflows/build-windows.yml/badge.svg" /></a>
<a href="https://github.com/poopoocacacacaca/melonRA/actions/workflows/build-ubuntu.yml"><img src="https://github.com/poopoocacacacaca/melonRA/actions/workflows/build-ubuntu.yml/badge.svg" /></a>
<a href="https://github.com/poopoocacacacaca/melonRA/actions/workflows/build-macos.yml"><img src="https://github.com/poopoocacacacaca/melonRA/actions/workflows/build-macos.yml/badge.svg" /></a>
<a href="https://github.com/poopoocacacacaca/melonRA/actions/workflows/build-bsd.yml"><img src="https://github.com/poopoocacacacaca/melonRA/actions/workflows/build-bsd.yml/badge.svg" /></a>
</p>

melonRA is [melonDS](https://github.com/melonDS-emu/melonDS), the Nintendo DS/DSi emulator, with
[RetroAchievements](https://retroachievements.org) built in. Log in, load a game, and earn achievements,
compete on leaderboards and show off your progress on your RetroAchievements profile, all from the
regular melonDS desktop app.

## Features

- **Log in** with your RetroAchievements account. Only a login token is saved, never your password.
- **Automatic game detection.** Games are recognized from the `.nds` ROM itself.
- **Multi-set support.** Bonus sets (subsets) load alongside the main set, each with its own progress and mastery notification.
- (WIP) **Hardcore mode**, on by default, for full-credit unlocks.
- **Offline-safe unlocks.** If the connection drops, unlocks and leaderboard entries are saved and submitted later, even after restarting melonRA.
- **On-screen notifications:**
  - your username and points when a game starts
  - game start with your progress
  - unlocks and mastery
  - leaderboard attempts and your new rank
- **Challenge and progress indicators**, so you know when an achievement is "armed" or how close you are.
- **Leaderboard trackers**, which show the live timer or score while an attempt is running.
- **Customizable overlay.** Put each kind of notification in any corner or at the top or bottom center,
  with rounded or square corners, bold text and borders, colored or black and white, at four sizes, with
  a live preview and a test button.
- **Achievements and leaderboards browser** with badges, unlock dates, rarity and the rankings around you.
  Its sidebar lists every game in your game list that's on RetroAchievements, with your progress, so
  you can browse any game's achievements without starting it.
- **Rich Presence.** Your RetroAchievements profile shows what you're doing in the game.
- **Extras:**
  - encore mode, spectator mode and unofficial achievements
  - sound effects
  - savestates that remember achievement progress
- **Optional RAIntegration** developer toolkit on Windows, for achievement creators.
- **Game list.** Point melonRA at your game folders and it lists every DS game with its icon, title,
  developer, region flag, game code and size. Sort by any column, search, and double-click to play.
  Switch to the **grid** at the bottom of the list to browse box art instead, with a size slider.
- **Built-in updater** with a Stable and a Nightly channel.

## Getting started

1. Download the [latest release](https://github.com/poopoocacacacaca/melonRA/releases/latest):
   `melonRA-<version>-windows-x86_64.zip` for Windows, or the Linux AppImage. Unzip it anywhere and run
   `melonRA.exe`. To try the newest changes before a release, use the
   [nightly build](https://github.com/poopoocacacacaca/melonRA/releases/tag/nightly) (Windows and Linux),
   or build it yourself (see below).
2. Set up melonDS as usual (BIOS/firmware are optional, see the original README below).
   Click **Add Folder...** on the main screen (or the **Game folders** list in **Config → Path Settings**) to list your
   games there.
3. Open **RetroAchievements → Log In…** and sign in with your
   [RetroAchievements.org](https://retroachievements.org) account.
4. Load a game. A popup shows the game and your progress. **RetroAchievements → Achievements…** opens
   the full list and the leaderboards.

All options live in **RetroAchievements → Settings…**: hardcore, encore, spectator, unofficial
achievements, which notifications to show, and sounds. Its **Appearance** tab sets where each
notification goes and how it looks.

## Versions

melonRA versions are `MAJOR.MINOR.PATCH`, starting at 0.1.0:

- **MAJOR** goes up for major releases.
- **MINOR** goes up for feature releases.
- **PATCH** goes up for small updates and fixes.

What changed in each version is in [CHANGELOG.md](./CHANGELOG.md).

## Updates

When a new version is out, melonRA asks whether to update now, skip that version, or wait until next
time. **Config → Update Settings** has the rest:

- **Update Channel:** *Stable* follows the releases. *Nightly* follows the
  [nightly build](https://github.com/poopoocacacacaca/melonRA/releases/tag/nightly), a new build for every
  change.
- **Always update to the latest version** downloads and installs updates on startup, before melonRA opens.
- **Never ask about updates** stops the check at startup. You can still check from the same window.

melonRA updates itself on Windows and when run as the Linux AppImage. On macOS, or a Linux build installed
another way, it tells you about new versions and links to the download.

## Building

melonRA builds exactly like melonDS. The RetroAchievements library (rcheevos) is included in the source
tree, so there are **no extra dependencies**.

### Windows (MSYS2)

1. Install [MSYS2](https://www.msys2.org/) and open the **MSYS2 UCRT64** terminal.
2. Update with `pacman -Syu` (reopen the terminal if it asks).
3. Install the tools and dependencies:
   ```bash
   pacman -S git mingw-w64-ucrt-x86_64-{toolchain,cmake,SDL2,libarchive,enet,zstd,faad2,qt6-base,qt6-svg,qt6-multimedia,qt6-tools}
   ```
4. Clone and build:
   ```bash
   git clone https://github.com/poopoocacacacaca/melonRA
   cd melonRA
   cmake -B build
   cmake --build build
   ```
5. Run `build/melonRA.exe` from the same terminal, so it can find the DLLs.

### Linux

Install the same dependencies as for melonDS. On Ubuntu 24.04:

```bash
sudo apt install cmake extra-cmake-modules libcurl4-gnutls-dev libpcap0.8-dev libsdl2-dev libarchive-dev \
  libenet-dev libzstd-dev libfaad-dev qt6-{base,base-private,multimedia,svg}-dev
git clone https://github.com/poopoocacacacaca/melonRA
cd melonRA
cmake -B build
cmake --build build -j$(nproc --all)
```

### macOS

```zsh
brew install git pkg-config cmake sdl2 qt@6 libarchive enet zstd faad2
git clone https://github.com/poopoocacacacaca/melonRA
cd melonRA
cmake -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6);$(brew --prefix libarchive)"
cmake --build build -j$(sysctl -n hw.logicalcpu)
```

For other platforms, Nix and static builds, see [BUILD.md](./BUILD.md).

### Optional: RAIntegration (Windows)

For achievement developers:

1. Enable *Load the RAIntegration developer toolkit* in **RetroAchievements → Settings…**.
2. Click **Download / update RAIntegration**. melonRA fetches the latest `RA_Integration-x64.dll` from
   retroachievements.org and puts it next to `melonRA.exe`. You can also place the DLL there yourself.
3. Restart melonRA. The toolkit appears under **RetroAchievements → Developer Toolkit**.

melonRA also offers to download the DLL at startup if it is missing or older than the minimum version
RetroAchievements requires. The toolkit is available on x64 (and x86) Windows only; it has no ARM64 build.

## For developers

The RetroAchievements code is in `src/frontend/qt_sdl/RetroAchievements/`, and the vendored rcheevos library
is in `src/rcheevos/`. [RETROACHIEVEMENTS.md](./RETROACHIEVEMENTS.md) covers:

- how the pieces fit together and where melonDS is hooked
- the DS memory map and the savestate format
- the offline unlock queue
- the hardcore compliance checklist

## Privacy and licenses

melonRA has no telemetry. The only data it sends goes to RetroAchievements.org, and only when you're logged in. See
[PRIVACY.md](./PRIVACY.md). Bundled and linked components are listed in [THIRD-PARTY-LICENSES.md](./THIRD-PARTY-LICENSES.md).

## Credits

- [melonDS](https://github.com/melonDS-emu/melonDS) by Arisotura and the melonDS team, which is all the actual emulation.
- [RetroAchievements](https://retroachievements.org) and [rcheevos](https://github.com/RetroAchievements/rcheevos) (MIT).
- melonRA is free software under the GPLv3, like melonDS. It is not affiliated with the melonDS team or RetroAchievements.

## Ethos on AI Usage

I am not a developer with much finesse, but new technologies make it easier every day to mold things in a direction that you'd like them to.
I don't think developing stuff with the help of AI is inherently bad, but there are a slew of people who only really care about the end result
and not forging a more polished product, I can at least say I polish up on visuals and stuff that AI could never replace to a T,
my end goal is to have a temporary solution until something better comes along the way, hopefully taking into account what AI was able to come
up with in this or any other repository, Pandora's Box has already opened... Why not take advantage of it?

<br>
<hr>
<hr>
<br>

# Original melonDS README

<p align="center"><img src="https://raw.githubusercontent.com/melonDS-emu/melonDS/master/res/icon/melon_128x128.png"></p>
<h2 align="center"><b>melonDS</b></h2>
<p align="center">
<a href="http://melonds.kuribo64.net/" alt="melonDS website"><img src="https://img.shields.io/badge/website-melonds.kuribo64.net-%2331352e.svg"></a>
<a href="http://melonds.kuribo64.net/downloads.php" alt="Release: 1.1"><img src="https://img.shields.io/badge/release-1.1-%235c913b.svg"></a>
<a href="https://www.gnu.org/licenses/gpl-3.0" alt="License: GPLv3"><img src="https://img.shields.io/badge/License-GPL%20v3-%23ff554d.svg"></a>
<a href="https://kiwiirc.com/client/irc.badnik.net/?nick=IRC-Source_?#melonds" alt="IRC channel: #melonds"><img src="https://img.shields.io/badge/IRC%20chat-%23melonds-%23dd2e44.svg"></a>
<a href="https://discord.gg/pAMAtExcqV" alt="Discord"><img src="https://img.shields.io/badge/Discord-Kuribo64-7289da?logo=discord&logoColor=white"></a>
<br>
<a href="https://github.com/melonDS-emu/melonDS/actions/workflows/build-windows.yml?query=event%3Apush"><img src="https://github.com/melonDS-emu/melonDS/actions/workflows/build-windows.yml/badge.svg" /></a>
<a href="https://github.com/melonDS-emu/melonDS/actions/workflows/build-ubuntu.yml?query=event%3Apush"><img src="https://github.com/melonDS-emu/melonDS/actions/workflows/build-ubuntu.yml/badge.svg" /></a>
<a href="https://github.com/melonDS-emu/melonDS/actions/workflows/build-macos.yml?query=event%3Apush"><img src="https://github.com/melonDS-emu/melonDS/actions/workflows/build-macos.yml/badge.svg" /></a>
<a href="https://github.com/melonDS-emu/melonDS/actions/workflows/build-bsd.yml?query=event%3Apush"><img src="https://github.com/melonDS-emu/melonDS/actions/workflows/build-bsd.yml/badge.svg" /></a>
</p>
DS emulator, sorta

The goal is to do things right and fast, akin to blargSNES (but hopefully better). But also to, you know, have a fun challenge :)
<hr>

## How to use

Firmware boot (not direct boot) requires a BIOS/firmware dump from an original DS or DS Lite.
DS firmwares dumped from a DSi or 3DS aren't bootable and only contain configuration data, thus they are only suitable when booting games directly.

### Possible firmware sizes

 * 128KB: DSi/3DS DS-mode firmware (reduced size due to lacking bootcode)
 * 256KB: regular DS firmware
 * 512KB: iQue DS firmware

DS BIOS dumps from a DSi or 3DS can be used with no compatibility issues. DSi BIOS dumps (in DSi mode) are not compatible. Or maybe they are. I don't know.

As for the rest, the interface should be pretty straightforward. If you have a question, don't hesitate to ask, though!

## How to build
See [BUILD.md](./BUILD.md) for build instructions.

## TODO LIST

 * better DSi emulation
 * better OpenGL rendering
 * netplay
 * the impossible quest of pixel-perfect 3D graphics
 * support for rendering screens to separate windows
 * emulating some fancy addons
 * other non-core shit (debugger, graphics viewers, etc)

### TODO LIST FOR LATER (low priority)

 * big-endian compatibility (Wii, etc)
 * LCD refresh time (used by some games for blending effects)
 * any feature you can eventually ask for that isn't outright stupid

## Credits

 * Martin for GBAtek, a good piece of documentation
 * Cydrak for the extra 3D GPU research
 * limittox for the icon
 * All of you comrades who have been testing melonDS, reporting issues, suggesting shit, etc

## Licenses

[![GNU GPLv3 Image](https://www.gnu.org/graphics/gplv3-127x51.png)](http://www.gnu.org/licenses/gpl-3.0.en.html)

melonDS is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

### External
* Images used in the Input Config Dialog - see `src/frontend/qt_sdl/InputConfig/resources/LICENSE.md`
