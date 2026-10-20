# Third-party components and licenses

melonRA as a whole is distributed under the **GNU General Public License v3.0 or later**
(see [LICENSE](LICENSE)), the same license as melonDS. Its complete source code is
available at <https://github.com/poopoocacacacaca/melonRA>.

melonRA is a single emulator built on melonDS; it doesn't ship separate emulator cores.
The following components are included in, or linked into, melonRA builds.

## Included in the source tree

| Component | Purpose | License | Upstream |
|---|---|---|---|
| melonDS | the emulator | GPL-3.0-or-later | https://github.com/melonDS-emu/melonDS |
| rcheevos | RetroAchievements runtime (`src/rcheevos`) | MIT | https://github.com/RetroAchievements/rcheevos |
| teakra | DSi DSP emulation (`src/teakra`) | MIT | https://github.com/wwylele/teakra |
| FatFs | FAT filesystem (`src/fatfs`) | FatFs license (BSD-style) | http://elm-chan.org/fsw/ff/ |
| Blip_Buffer (blip-buf) | audio resampling (`src/blip-buf`) | LGPL-2.1-or-later | https://code.google.com/archive/p/blip-buf/ |
| Dolphin (portions) | JIT emitter code (`src/dolphin`) | GPL-2.0-or-later | https://github.com/dolphin-emu/dolphin |
| tiny-AES-c | AES (`src/tiny-AES-c`) | Unlicense | https://github.com/kokke/tiny-AES-c |
| xxHash | hashing (`src/xxhash`) | BSD-2-Clause | https://github.com/Cyan4973/xxHash |
| SHA-1 (sha1) | hashing (`src/sha1`) | Public domain | — |
| libslirp | user-mode networking (`src/net/libslirp`) | BSD-3-Clause and others (see `src/net/libslirp/COPYRIGHT`) | https://gitlab.freedesktop.org/slirp/libslirp |
| glad | OpenGL loader (`src/frontend/glad`) | MIT / Public domain | https://github.com/Dav1dde/glad |
| DuckStation GL context code (portions) | OpenGL context handling (`src/frontend/duckstation`) | GPL-3.0, from when DuckStation was GPL-licensed (as included in upstream melonDS) | https://github.com/stenzek/duckstation |
| toml11 | config file parsing (`src/frontend/qt_sdl/toml`) | MIT | https://github.com/ToruNiina/toml11 |
| gif-h | GIF writing (`src/frontend/qt_sdl/gif-h`) | Public domain | https://github.com/charlietangora/gif-h |
| famfamfam flag icons | region flags in the game list (`src/frontend/qt_sdl/GameList/flags`) | Public domain | http://www.famfamfam.com/lab/icons/flags/ |

## Linked libraries (provided by the build environment)

| Component | License | Upstream |
|---|---|---|
| Qt 6 (Core, Gui, Widgets, Network, Multimedia, OpenGL, Svg) | LGPL-3.0 | https://www.qt.io |
| SDL2 | zlib | https://www.libsdl.org |
| libarchive | BSD-2-Clause | https://www.libarchive.org |
| zstd | BSD-3-Clause / GPL-2.0 | https://github.com/facebook/zstd |
| FAAD2 | GPL-2.0-or-later | https://github.com/knik0/faad2 |
| ENet | MIT | http://enet.bespin.org |

## Downloaded at the user's request (not shipped)

| Component | License | Upstream |
|---|---|---|
| RAIntegration (`RA_Integration-x64.dll`, Windows, optional) | see upstream | https://github.com/RetroAchievements/RAIntegration |

## Assets

The RetroAchievements notification sounds in
`src/frontend/qt_sdl/RetroAchievements/resources/` were synthesized for this project
and are released under the same license as melonRA. The melonRA logo
(`res/melonRA-logo.png`) belongs to the melonRA project.

The full license texts of the included components are kept next to their sources
(for example `src/rcheevos/LICENSE`, `src/teakra/LICENSE`, `src/fatfs/LICENSE.txt`).
