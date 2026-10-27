# Changelog

melonRA versions are `MAJOR.MINOR.PATCH`:

- **MAJOR** goes up for major releases.
- **MINOR** goes up for feature releases.
- **PATCH** goes up for small updates and bug fixes.

## Unreleased

- **Play offline.** Games you've played online once keep their achievements without an internet
  connection, in Hardcore and Casual mode alike: melonRA saves what the RetroAchievements server sends
  and uses it when the server can't be reached. Hardcore rules apply offline exactly as online.
  Unlocks earned offline stay unlocked in later offline sessions and are submitted in the mode they
  were earned in once you're back online. A notification says when you're playing offline. Can be turned off in
  RetroAchievements Settings.
- **Screen layout hotkeys.** Two new hotkeys (Config → Input and Hotkeys → General Hotkeys) step through the
  screen layouts (Natural, Vertical, Horizontal, Hybrid) and the screen sizings (Even, Emphasize Top,
  Emphasize Bottom, Auto, Top Only, Bottom Only), with an on-screen message naming the new one.
  They're unassigned until you pick a key or button.
- **Game Settings.** Right-click a game in the list → Game Settings... to give it its own settings,
  like RPCS3's custom configurations. Every setting starts at "Global" (follow the emulator) and can
  instead have its own value, or be turned On or Off for that game alone:
  - Graphics: renderer, internal resolution, threaded rendering, polygon splitting, high-resolution
    coordinates, OpenGL display, vsync, screen filtering
  - Screen Layout: layout, sizing, rotation, gap, swap, integer scaling, aspect ratios
  - Wi-Fi: Global, Off (the game can't go online), Wiimmfi, Kaeru WFC or a custom DNS server
  - Emulation & Audio: console type, direct boot, JIT, audio interpolation, volume
  - Cheats: on or off for the game, and which of its cheats (from the cheats folder in Path Settings)
    are active

  Games with their own settings show a gear in front of their title (and on their cover in the grid).
  While such a game runs, changing one of its settings from the menus changes the game's value, not
  the global one.
- **Hardcore mode, checked against the RetroAchievements requirements.** Anything hardcore doesn't
  allow (loading or undoing save states, frame advance, slow motion, cheats, RAM search, the GDB stub)
  now shows a short on-screen note such as "Loading save states doesn't work in Hardcore mode",
  whether it's used from a hotkey or a menu, instead of offering to switch to casual. In hardcore the game
  always runs at the DS's own speed and fast forward is the only way to change it, like in RetroArch:
  a custom target speed in Interface Settings is ignored, and a fast-forward speed below full speed is
  raised to it. Cheats turned on in settings or Game Settings say they're off when a game starts.
- **Settings windows don't pause the game.** Emulation, Input and Hotkeys, Camera, Firmware, Path,
  Multiplayer, Wi-Fi, Interface and Set Up Cheat Codes now leave the game running, like Video and Audio
  already did. Config → Emulation Settings has "Keep the game running while settings windows are
  open" (on by default); turn it off for the old behavior.
- **DSiWare in the game list.** DSiWare titles show "DSiWare" as their platform. Double-clicking one
  explains how to install and play it on an emulated DSi (it can't be started like a cartridge), with
  a button to open Manage DSi Titles when a DSi NAND is set up.
- **Online play server picker.** Config → Wi-Fi Settings has an Online Play section to pick a
  Nintendo Wi-Fi Connection replacement (Wiimmfi or Kaeru WFC) or any other service by its DNS
  address. melonRA points the firmware's Wi-Fi connections at it when a game starts, so there's no
  DNS to edit by hand. "Firmware settings (don't change)" keeps the old behavior.
- **Settings backup.** Config → Back Up Settings... saves all of melonRA's settings to one file, and
  Restore Settings... brings them back (melonRA restarts to apply them), on the same computer or
  another one. File and folder locations can be left out, and your RetroAchievements login is only
  included if you ask for it. Restoring keeps the previous settings as `melonRA.toml.bak`.
- **Challenge details on hover.** Pointing the mouse at a challenge indicator shows the achievement's
  name, points and description, in a box that follows the indicators' style settings.
- Nightly builds show their commit in the window title, e.g. "melonRA 0.3.0 (b7a432a)"; releases show
  just the version.
- The JIT recompiler works on Intel Macs again (from melonDS).
- Security: updates are only downloaded from melonRA's own GitHub releases and are checked against the
  SHA-256 checksum GitHub publishes before they're installed; the RAIntegration toolkit is only
  downloaded from RetroAchievements over HTTPS; logging out also deletes the offline copy of your
  login; the typed password is cleared from memory once it's sent.
- **The game list remembers your games.** It shows up instantly from the last scan, then checks the
  folders in the background and only reads ROMs that are new or have changed. If nothing changed, the
  list isn't touched, so your selection and scroll position stay put.

## 0.2.0

- **Grid view with box art.** The bar at the bottom of the game list switches between List and Grid;
  the grid shows each game's cover, with a size slider (or Ctrl + mouse wheel). Covers come from
  GameTDB by game code and region, then from the libretro thumbnails by title, and are kept on disk
  after the first download. melonRA asks before downloading any, and the bar shows the download
  progress with a button to stop it. View → Game Covers picks the sources, turns automatic downloads
  on or off, downloads or stops on demand, can use a folder of your own covers (named by game code or
  after the ROM), and opens the downloaded covers folder. Covers load in the background, so large
  collections scroll smoothly.
- The game list has no hover highlight; the selected game's text changes color in the list, and the
  selected cover gets an outline in the grid. The game count stays centered in the bottom bar.
- Right-clicking a game offers Show in Explorer (Finder on macOS, the file manager on Linux), which
  opens the folder with the ROM selected, next to Open Containing Folder.
- **All your games in the achievements window.** RetroAchievements → Achievements... has a sidebar
  with the game being played and every game from the game list that's on RetroAchievements, each with
  your progress ("12 / 50 achievements"). Picking one shows its achievements (locked, unlocked in
  hardcore or casual, rarity) and leaderboards with rankings, without starting it. Games that aren't
  on RetroAchievements are listed separately. Each ROM is fingerprinted once and matched on your
  computer against RetroAchievements' list of DS games.
- **Game list.** While no game is running, the main window lists the DS games in your game folders
  (Config → Path Settings, or Add Folder... on the main screen). Each game shows the icon and
  title from its ROM, the developer, a region flag (USA, Europe, Japan, Spain, Germany and more), the
  game code and the size. Columns can be sorted, hidden and rearranged, there's a search box, and
  double-clicking a game starts it. View → Show Game List turns it off.
- **Updater.** melonRA checks for new versions at startup and offers to update now, skip that version or
  wait. Config → Update Settings picks the Stable or Nightly channel, can install updates automatically
  before melonRA opens, and can turn the question off.
- **Notification layout and style.** A new Appearance tab in RetroAchievements Settings places each
  kind of notification (achievements, leaderboards, challenge indicators, progress, trackers) in a
  corner or in the middle of the top or bottom edge. Each one can have rounded or square
  corners, bold text and a border. There are four sizes, and popups can be colored by kind or
  black and white (plain translucent black, like RetroArch). A live preview and a Test Notifications
  button show the result.
- **Your account at game start.** When a game starts, a notification shows your RetroAchievements
  username and points (can be turned off).
- Notifications use the room a wider window has, and long achievement names and descriptions wrap
  onto more lines instead of being cut off.
- The overlay is only redrawn while a notification slides in or fades out, instead of every frame
  while one is on screen.
- With the RAIntegration toolkit, quitting asks first if it has unsaved changes.
- Menus, window titles and section headings use Title Case ("Emulation Settings", "Wi-Fi Settings", ...).
- Fixed the menu bar's hover highlight being shorter than the menu name with the Windows 11 theme.

## 0.1.0

First release of melonRA: melonDS 1.1 with RetroAchievements built in.

- **Account and games**
  - Log in with your RetroAchievements account. Only a login token is stored, never your password.
  - Games are recognized automatically from the ROM.
  - Bonus sets (subsets) are supported, each with its own progress and mastery notification.
- **Hardcore mode**, on by default.
  - Blocks loading savestates, cheats, slow motion, frame advance, RAM search and the GDB stub.
  - Limits how often you can pause.
  - Switching from casual to hardcore resets the game.
- **On-screen overlay**
  - Popups for the game placard, unlocks, mastery and leaderboard results.
  - Challenge indicators, a progress indicator and live leaderboard trackers.
  - Works with both the software and OpenGL renderers.
- **Achievements and leaderboards window**, with badges, unlock dates, rarity and the rankings around you.
- **Rich Presence** on your RetroAchievements profile.
- **Offline-safe unlocks.** If the connection drops, unlocks and leaderboard entries are saved and submitted later, even after restarting.
- **Savestates** keep achievement progress.
- **Extra modes:** encore, spectator and unofficial achievements.
- **Sound effects** for unlocks, mastery and leaderboard submissions.
- **RAIntegration developer toolkit** support on Windows, with a built-in download/update button.
- **melonRA branding:** app name, icon, `melonRA.exe` and the `melonRA.toml` config file. An existing `melonDS.toml` in the same folder is imported on first start.

RetroAchievements has not approved melonRA for hardcore yet. Until it does, hardcore unlocks are recorded as casual on the site.
