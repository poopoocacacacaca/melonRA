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
  - a host waiting for MP replies promises not to transmit until the end of a reply window (later
    packets are stamped no earlier than that), so clients can catch up and answer; replies sent from
    the command's transmission to the end of the window count
  - the window starts at 256 µs and grows 64 µs at a time, up to 2 ms, until every client the
    command expects has answered. A client's wireless clock trails the host's by however late it
    noticed the association response (up to about half a millisecond), so its reply comes that much
    later. melonDS's own link waits for replies in real time; this waits in emulated time instead,
    and each step depends only on what the clients did before it
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

1. Client → host: `Hello` (magic, protocol version (3), melonRA build, name).
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
trip to it (ENet's measurement), and `Invite` from the host with the room's Discord party id and join
secret (to each player once their ROM is accepted, and to everyone when it changes). The suggested delay covers half the two largest round trips (inputs
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

## Discord invites and the router

Hosting starts a `PortMapper` (`Netplay/PortMapper.cpp`):

1. **UPnP IGD.** An SSDP M-SEARCH goes out on every interface; the first Internet Gateway Device
   that answers is used. Its description is fetched only from the address that answered discovery
   (no redirects elsewhere, 256 KB cap, 3 s timeouts, no proxy). `AddPortMapping` maps the room's
   UDP port to this computer with a one-hour lease, renewed at half time, and `DeletePortMapping`
   removes it when the room closes. Error 725 retries with a permanent lease; 718 (port taken) tries
   the next port, up to 8. `GetExternalIPAddress` gives the public address; a private or
   100.64.0.0/10 answer means a second NAT, so the room says players outside can't get in.
2. **STUN.** If the router doesn't say, a Binding Request to a public STUN server
   (`stun.l.google.com`, `stun.cloudflare.com`) gives the public address.

The host's Discord activity carries a party (random id, size) and, while the room is in the lobby
and not full, a join secret:

```
mr1|<public address>:<port>|<home network address>:<port>
```

Either part may be empty. On the joining side `Runner::parseJoinSecret` accepts only that exact
shape: IPv4 literals and decimal ports, at most 128 characters; anything else is refused before
any connection is made. `NetplayLink::ConnectAny` tries every address at once and keeps the first
that answers, so friends on the same network connect directly. The host also sends the party and
secret to each player (`Msg_Invite`), so their Discord shows the same room with Join.

Discord IPC: melonRA subscribes to `ACTIVITY_JOIN` (this player pressed Join, or accepted an
invite: the secret is handed to `MainWindow::onDiscordJoin`) and `ACTIVITY_JOIN_REQUEST` (a friend
asks to join: the host is asked, and the answer is `SEND_ACTIVITY_JOIN_INVITE` or
`CLOSE_ACTIVITY_REQUEST`; requests outside an open lobby are declined without asking). The friend's
name is shown as plain text with control and direction characters removed. So that Discord can start
melonRA for a Join, it's registered as the handler for `discord-<application id>`: a per-user
registry key on Windows, `~/Library/Application Support/discord/games/<id>.json` on macOS, and a
hidden `.desktop` file on Linux.

## A lobby list later

If public rooms are added (like Kaillera's or sm64coopdx's CoopNet), the lobby server must:

- list only the room name, game, player count and whether it has a password, never IP addresses;
  hand out an address only to someone joining, or relay or hole-punch the traffic instead
- be reached only over HTTPS at an address built into melonRA
- rate-limit room creation, cap room name length, support private rooms with passwords checked by
  the server (never stored or listed in plain text), and expire rooms whose host goes quiet
- carry chat and passwords encrypted

Running such a server also means keeping it patched and moderated.
