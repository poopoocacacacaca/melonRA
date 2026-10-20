# Changelog

melonRA versions are `MAJOR.MINOR.PATCH`:

- **MAJOR** goes up for major releases.
- **MINOR** goes up for feature releases.
- **PATCH** goes up for small updates and bug fixes.

## Unreleased

- **Updater.** melonRA checks for new versions at startup and offers to update now, skip that version or
  wait. Config → Update Settings picks the Stable or Nightly channel, can install updates automatically
  before melonRA opens, and can turn the question off.
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
