# melonRA netplay (experimental)

How netplay works and the rules any change to it has to keep. For what it does and how to use it, see
the [changelog](CHANGELOG.md).

## The idea

A DS game's local wireless play expects answers within microseconds, which the internet can't
deliver. So every computer in a session runs **every player's console**: with two players, both
computers run DS #1 and DS #2 side by side, linked the same way melonDS links consoles on one PC. The
only thing sent over the internet is each player's buttons and touch input for each frame. Identical
consoles fed identical inputs stay identical, so every computer sees the same game.

On each computer, this player's console runs in the main window; the others run hidden and muted.

## Code layout

| Piece | Where |
|---|---|
| Local wireless link on emulated time | `src/net/LockstepMP.*` |
| UDP connections (ENet) | `src/net/NetplayLink.*` |
| Message format and checks | `src/frontend/qt_sdl/Netplay/NetplayProtocol.h` |
| The room, inputs and desync checks | `Netplay/NetplaySession.*` |
| Setting up and removing the consoles | `Netplay/NetplayRunner.*` |
| Host, Join and Room windows | `Netplay/NetplayDialogs.*` |
| What an instance needs for its console | `Netplay/NetplayConsole.h` |

Hooks: `EmuInstance` (`netplayBoot`, firmware/RTC/save handling while `netplay` is set), `EmuThread`
(inputs from the session, blocked hotkeys, `netplayFrameDone`), `Platform.cpp` (MP calls by console
number, no internet for netplay consoles), `Config` (session overrides), `Window` (the Experimental
menu).

## Keeping the consoles identical

Anything that could make one computer's copy of a console differ from another's is fixed for the
session:

- **The wireless link** (`LockstepMP`). melonDS's usual link (`LocalMP`) gives up waiting after a
  real-time timeout, so results depend on how fast each thread runs. `LockstepMP` uses each
  console's system clock instead (`NDS::GetSysTimestamp()`):
  - a console polling at clock *t* first waits until every other console has reached *t*, then gets
    the packets sent before *t*, ordered by (clock, console, sequence)
  - a host waiting for MP replies promises not to transmit until the end of a 256 µs reply window
    (later packets are stamped no earlier than that), so clients can catch up and answer; replies
    sent from the command's transmission to the end of the window count
  - powering the wireless off or leaving is recorded at an emulated time, never a thread's timing
  - unread packets expire after one emulated second
- **Settings** (`Config::SetSessionOverrides`): DS mode, built-in BIOS and firmware, direct boot,
  software renderer, the host's JIT settings, no audio interpolation, silent microphone, the DS's
  own speed. Each console gets its owner's nickname, birthday, color, language and message, and a
  MAC address by console number. These override the usual and per-game settings without changing
  them.
- **Start state**: every console boots from its owner's save, at the host's date and time, with no
  Wi-Fi settings file, RTC file, SD card, GBA cart or cheats.
- **During the game**: no savestates, pausing, reset, fast forward, slow motion, frame advance, lid,
  solar sensor, microphone or internet access for the games.

Every 60 frames each computer hashes every console (main RAM and system clock) and the host
compares them. A mismatch is reported once, with the frame and console.

## Protocol

ENet over UDP, default port 7066. The host relays everything, so players only connect to the host.
Channel 0 carries the session and is reliable and ordered; channel 1 carries inputs and hashes.
Messages start with a type byte; numbers are little-endian.

1. Client → host: `Hello` (magic, protocol version, melonRA build, name).
2. Host → client: `Room` (build, game code, title, ROM SHA-1, ROM size, input delay).
3. Client finds the ROM (game list first, by game code and size, then by hand) and sends `HaveROM`.
4. Host assigns a slot and sends everyone's `Console` (DS settings, save size) and `SaveChunk`s,
   then the `Roster`. The client sends its own `Console` and save the same way.
5. Host: `Start` (date and time, JIT settings, input delay, console order). Everyone sets up their
   consoles and sends `Loaded`; the host answers `Go`.
6. Each frame, each player sends `Input` for frame *f* + input delay; the first frames are neutral.
7. `Hash` every 60 frames; `Desync` from the host when they differ.

At any time: `Chat` (to the host, which relays it with the sender's slot), `Delay` from the host
when it changes the input delay, and `Pings` from the host every second with each player's round
trip to it (ENet's measurement). The suggested delay covers half the two largest round trips (inputs
between two players pass through the host) plus 20 ms.

**Changing the input delay during the game** needs no coordination. Only a console's owner decides
which frame each of their inputs lands on; everyone else just plays the inputs that arrive. When the
delay goes up, the owner fills the frames in between with their last input; when it goes down, they
skip sending until the new delay catches up. Every computer still receives exactly the same inputs.

Disconnects carry a reason (left, room closed, removed). If a player leaves during the game, the
session ends for everyone.

## Security rules

Everything from the network is untrusted. Changes must keep these:

- **Bounded parsing.** All reads go through `Reader`, which never reads past the end. Every string,
  count, size and offset has a hard limit checked before use. A malformed message disconnects the
  sender (host) or ends the session (client). ENet's packet size and queued data are capped.
- **Nothing executable or file-like crosses the wire.** No ROMs (players match by hash), no
  savestates, no code or mods, no file names or paths. The only blob is a player's save, limited to
  32 MB, kept in memory and never written over anyone's files; only this computer's own console writes
  to this player's own save file.
- **Text from peers is shown as plain text.** Names, titles and chat are cut to length and stripped
  of control and direction-override characters (`CleanText`, `CleanName`, `CleanDSText`), and every
  label, message box or chat line that shows them uses plain text.
- **Chat is rate-limited** by the host: a player gets 5 messages per 5 seconds, the rest is dropped.
  Messages are at most 200 bytes.
- **No account data.** Nothing about the RetroAchievements login is sent. Netplay can't start in
  Hardcore mode.
- **Flood limits.** Connections that don't introduce themselves within 10 s are dropped, pending
  connections are capped, and rooms refuse joins once the game has started. The host can remove a
  player before the game starts.
- **Peers only affect the session.** A misbehaving player can desync or end the game, never change
  anything outside it.

Tests to rerun after changes: the link determinism test (2–4 consoles, random thread delays, must be
identical every run, also under ThreadSanitizer) and the session test (host and client over
localhost, then malformed and truncated messages, also under AddressSanitizer and
UndefinedBehaviorSanitizer).

## A lobby list later

If public rooms are added (like Kaillera's or sm64coopdx's CoopNet), the lobby server must:

- list only the room name, game, player count and whether it has a password, never IP addresses;
  hand out an address only to someone joining, or relay or hole-punch the traffic instead
- be reached only over HTTPS at an address built into melonRA
- rate-limit room creation, cap room name length, support private rooms with passwords checked by
  the server (never stored or listed in plain text), and expire rooms whose host goes quiet
- carry chat and passwords encrypted

Running such a server also means keeping it patched and moderated.
