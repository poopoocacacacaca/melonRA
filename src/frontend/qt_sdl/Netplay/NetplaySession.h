/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#ifndef NETPLAYSESSION_H
#define NETPLAYSESSION_H

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include <QObject>
#include <QString>

#include "NetplayProtocol.h"
#include "NetplayLink.h"

namespace Netplay
{

// The game a room plays: every player needs a ROM with the same SHA-1.
struct RoomInfo
{
    std::string Build;
    std::string GameCode;
    std::string Title;
    std::array<u8, 20> ROMHash {};
    u32 ROMSize = 0;
    u8 InputDelay = 3;
};

struct PlayerView
{
    int Slot;
    QString Name;
    bool Ready;     // its DS settings and save have arrived
};

// One netplay session, hosting or joined.
//
// Every player runs one DS per player. Each player's own DS ("console" with
// the player's slot number) uses that player's DS settings and save, and is
// played with that player's inputs; only inputs travel during the game. The
// network runs on its own thread; signals are delivered on the thread that
// created the session (the UI thread).
// melonRA version (and commit for nightlies): everyone in a session needs the same
std::string BuildId();

class Session : public QObject
{
    Q_OBJECT

public:
    enum class State { Idle, Connecting, Lobby, Loading, Playing, Ended };

    explicit Session(QObject* parent = nullptr);
    ~Session() override;

    // --- UI thread ---

    bool host(u16 port, const QString& name, const RoomInfo& room,
              const ConsoleInfo& console, std::vector<u8> save, QString& error);
    bool join(const QString& address, u16 port, const QString& name, QString& error);

    // after roomReceived: the ROM found for the room, with this player's DS
    // settings and save for it
    void provideROM(const std::array<u8, 20>& hash, const ConsoleInfo& console, std::vector<u8> save);

    // host: settings sent with the start, and starting once everyone has
    // everything
    void setStartTime(int year, int month, int day, int hour, int minute, int second);
    void setJIT(bool enable, int blockSize, bool literal, bool branch, bool fastMem);
    bool canStart() const;
    void start();

    // after started: this computer's consoles are ready to run
    void loaded();

    void leave();

    State state() const { return CurState; }
    bool isHost() const { return IsHost; }
    int mySlot() const { return MySlot; }
    int players() const;
    const RoomInfo& room() const { return Room; }
    const StartInfo& startInfo() const { return Start; }
    std::vector<PlayerView> roster() const;

    // once started: the consoles in order (console N belongs to the player
    // in slot order()[N]), and which one is this computer's
    std::vector<int> order() const;
    int myConsole() const;

    // a console's DS settings, save and player name, for setting it up
    ConsoleInfo consoleOf(int console) const;
    std::vector<u8> saveOf(int console) const;
    QString nameOf(int console) const;

    // --- emulation threads ---

    // this player's input for frame `frame`, used `delay` frames later
    void submitLocalInput(u32 frame, const InputFrame& in);

    // blocks until a console's input for frame is known; false once the
    // session ended
    bool waitInput(int console, u32 frame, InputFrame& out);
    bool inputReady(int console, u32 frame) const;

    // a console's state hash at a frame that is a multiple of kHashInterval
    void reportHash(int console, u32 frame, u64 hash);

    bool active() const { return CurState == State::Playing || CurState == State::Loading; }

signals:
    void rosterChanged();
    void roomReceived(const Netplay::RoomInfo& room);
    void started();
    void go();
    void desync(int frame, int slot);
    void ended(const QString& reason);
    void notice(const QString& text);

private:
    struct Player
    {
        bool Present = false;
        int Peer = -1;            // host only: link peer
        std::string Name;
        bool HasROM = false;
        bool HasConsole = false;
        ConsoleInfo Console;
        std::vector<u8> Save;
        u32 SaveReceived = 0;
        bool Loaded = false;
        bool HostReady = false;   // client: what the host's roster says
    };

    struct Pending           // host: a connection that hasn't joined yet
    {
        bool Hello = false;
        std::string Name;
        std::chrono::steady_clock::time_point Since;
    };

    void runNetwork();
    void handle(const melonDS::NetplayLink::Event& ev);
    void handleHost(int peer, const std::vector<u8>& data);
    void handleClient(const std::vector<u8>& data);
    void peerLeft(int peer);

    bool readConsole(Reader& r, ConsoleInfo& out);
    void writeConsole(Writer& w, const ConsoleInfo& c);
    bool readSaveChunk(Reader& r, Player& p);
    bool readInputs(Reader& r, int console);
    void sendConsoleAndSave(int toPeer, int slot);
    void sendConsoleHeader(int toPeer, int slot, int exceptPeer);
    void broadcastRoster();
    void sendTo(int peer, int channel, const Writer& w);
    void sendAll(int channel, const Writer& w, int exceptPeer = -1);
    void reject(int peer, RejectReason reason);
    void finish(const QString& reason);
    void beginLoading();
    void checkAllLoaded();
    bool saveComplete(const Player& p) const;
    int slotOfPeer(int peer) const;
    int consoleOfSlot(int slot) const;
    int freeSlot() const;
    void checkHashes(u32 frame);
    void emitLater(std::function<void()> fn);

    melonDS::NetplayLink Link;
    std::thread NetThread;
    std::atomic<bool> Quit {false};

    mutable std::mutex Lock;
    std::condition_variable InputArrived;

    State CurState = State::Idle;
    bool IsHost = false;
    int MySlot = 0;
    RoomInfo Room;
    StartInfo Start;
    std::string MyName;
    std::array<Player, kMaxPlayers> Slots;
    std::map<int, Pending> PendingPeers;
    ConsoleInfo PendingConsole;           // client: sent once we have a slot
    std::vector<u8> PendingSave;
    QString RejectedWhy;

    std::vector<int> Order;               // slots, in console order
    int MyConsole = -1;

    // inputs, by console, indexed by frame
    std::array<std::vector<InputFrame>, kMaxPlayers> Inputs;

    // hashes: frame -> per computer (slot) -> per console
    std::map<u32, std::map<int, std::array<u64, kMaxPlayers>>> Hashes;
    std::map<u32, std::array<u64, kMaxPlayers>> MyHashes;
    std::map<u32, int> MyHashCount;
    bool DesyncReported = false;
};

}

#endif // NETPLAYSESSION_H
