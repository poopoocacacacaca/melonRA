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

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iterator>
#include <QMetaObject>

#include "NetplaySession.h"
#include "version.h"

using melonDS::NetplayLink;

namespace Netplay
{

std::string BuildId()
{
    std::string id = MELONRA_VERSION;
#ifdef MELONDS_EMBED_BUILD_INFO
    id += " ";
    id += MELONDS_GIT_HASH;
#endif
    return id;
}

static constexpr int kChanSession = 0;
static constexpr int kChanInput = 1;
static constexpr int kHelloTimeoutMs = 10000;

Session::Session(QObject* parent) : QObject(parent)
{
}

Session::~Session()
{
    leave();
}

void Session::emitLater(std::function<void()> fn)
{
    QMetaObject::invokeMethod(this, std::move(fn), Qt::QueuedConnection);
}

// --- setup ---

bool Session::host(u16 port, const QString& name, const RoomInfo& room,
                   const ConsoleInfo& console, std::vector<u8> save, QString& error)
{
    if (CurState != State::Idle) return false;
    if (!ValidSaveSize((u32)save.size()))
    {
        error = tr("This game's save is too large for netplay.");
        return false;
    }

    std::string err;
    if (!Link.Host(port, kMaxPlayers + 2, err))
    {
        error = QString::fromStdString(err);
        return false;
    }

    std::lock_guard<std::mutex> lk(Lock);
    IsHost = true;
    MySlot = 0;
    Port = port;
    Room = room;
    Room.Build = BuildId();
    Room.InputDelay = std::clamp<u8>(Room.InputDelay, 1, kMaxInputDelay);
    MyName = CleanName(name.toStdString(), "Player 1");

    Player& me = Slots[0];
    me = Player();
    me.Present = true;
    me.Name = MyName;
    me.HasROM = true;
    me.HasConsole = true;
    me.Console = console;
    me.Console.SaveSize = (u32)save.size();
    me.Save = std::move(save);
    me.SaveReceived = me.Console.SaveSize;

    CurState = State::Lobby;
    Quit = false;
    NetThread = std::thread(&Session::runNetwork, this);
    return true;
}

bool Session::join(const QString& address, u16 port, const QString& name, QString& error)
{
    return join({{address.trimmed().toStdString(), port}}, name, error);
}

bool Session::join(const std::vector<NetplayLink::Target>& targets, const QString& name, QString& error)
{
    if (CurState != State::Idle || targets.empty()) return false;

    std::string err;
    if (!Link.ConnectAny(targets, err))
    {
        error = QString::fromStdString(err);
        return false;
    }

    std::lock_guard<std::mutex> lk(Lock);
    IsHost = false;
    MySlot = -1;
    Port = targets[0].Port;
    Address = QString::fromStdString(targets[0].Address);
    MyName = CleanName(name.toStdString(), "Player");
    CurState = State::Connecting;
    Quit = false;
    NetThread = std::thread(&Session::runNetwork, this);
    return true;
}

void Session::setInvite(const QString& partyId, const QString& joinSecret)
{
    std::lock_guard<std::mutex> lk(Lock);
    if (!IsHost) return;
    std::string id = partyId.toStdString(), secret = joinSecret.toStdString();
    if (!ValidInviteText(id, kMaxPartyId) || !ValidInviteText(secret, kMaxJoinSecret)) return;
    PartyId = id;
    JoinSecret = secret;
    if (CurState != State::Idle && CurState != State::Ended)
        sendInvite(-1);
}

QString Session::partyId() const
{
    std::lock_guard<std::mutex> lk(Lock);
    return QString::fromStdString(PartyId);
}

QString Session::joinSecret() const
{
    std::lock_guard<std::mutex> lk(Lock);
    return QString::fromStdString(JoinSecret);
}

// with Lock held, host: the Discord invite to one client, or all (-1)
void Session::sendInvite(int toPeer)
{
    if (PartyId.empty()) return;
    Writer w(Msg_Invite);
    w.Str(PartyId, kMaxPartyId).Str(JoinSecret, kMaxJoinSecret);
    if (toPeer >= 0) sendTo(toPeer, kChanSession, w);
    else sendAll(kChanSession, w);
}

void Session::provideROM(const std::array<u8, 20>& hash, const ConsoleInfo& console, std::vector<u8> save)
{
    std::lock_guard<std::mutex> lk(Lock);
    if (IsHost || CurState != State::Lobby) return;

    if (!ValidSaveSize((u32)save.size()))
    {
        Link.Disconnect(0, Bye_Left);
        finish(tr("This game's save is too large for netplay."));
        return;
    }

    PendingConsole = console;
    PendingConsole.SaveSize = (u32)save.size();
    PendingSave = std::move(save);

    Writer w(Msg_HaveROM);
    w.Bytes(hash.data(), hash.size());
    sendTo(0, kChanSession, w);
}

bool Session::canStart() const
{
    std::lock_guard<std::mutex> lk(Lock);
    if (!IsHost || CurState != State::Lobby) return false;

    int n = 0;
    for (const Player& p : Slots)
    {
        if (!p.Present) continue;
        if (!p.HasConsole || !saveComplete(p)) return false;
        n++;
    }
    return n >= 2;
}

void Session::start()
{
    if (!canStart()) return;

    std::lock_guard<std::mutex> lk(Lock);

    Order.clear();
    for (int i = 0; i < kMaxPlayers; i++)
        if (Slots[i].Present) Order.push_back(i);

    Start.Players = (u8)Order.size();
    Start.InputDelay = Room.InputDelay;

    Writer w(Msg_Start);
    w.U16(Start.Year).U8(Start.Month).U8(Start.Day)
     .U8(Start.Hour).U8(Start.Minute).U8(Start.Second)
     .U8(Start.InputDelay)
     .U8(Start.JIT).U8(Start.JITBlockSize).U8(Start.JITLiteral).U8(Start.JITBranch).U8(Start.JITFastMem)
     .U8(Start.Players);
    for (int s : Order) w.U8((u8)s);
    sendAll(kChanSession, w);

    beginLoading();
    emitLater([this] { emit started(); });
}

void Session::setStartTime(int year, int month, int day, int hour, int minute, int second)
{
    std::lock_guard<std::mutex> lk(Lock);
    Start.Year = (u16)std::clamp(year, 2000, 2099);
    Start.Month = (u8)std::clamp(month, 1, 12);
    Start.Day = (u8)std::clamp(day, 1, 31);
    Start.Hour = (u8)std::clamp(hour, 0, 23);
    Start.Minute = (u8)std::clamp(minute, 0, 59);
    Start.Second = (u8)std::clamp(second, 0, 59);
}

void Session::setJIT(bool enable, int blockSize, bool literal, bool branch, bool fastMem)
{
    std::lock_guard<std::mutex> lk(Lock);
    Start.JIT = enable;
    Start.JITBlockSize = (u8)std::clamp(blockSize, 1, 32);
    Start.JITLiteral = literal;
    Start.JITBranch = branch;
    Start.JITFastMem = fastMem;
}

// with Lock held: the game is about to start on this computer
void Session::beginLoading()
{
    CurState = State::Loading;
    MyConsole = -1;
    for (size_t i = 0; i < Order.size(); i++)
        if (Order[i] == MySlot) MyConsole = (int)i;

    for (auto& in : Inputs) in.clear();
    for (size_t c = 0; c < Order.size(); c++)
        Inputs[c].assign(Start.InputDelay, InputFrame());
    Delay = Start.InputDelay;

    for (Player& p : Slots) p.Loaded = false;
    Hashes.clear();
    MyHashes.clear();
    MyHashCount.clear();
    DesyncReported = false;
}

void Session::loaded()
{
    std::lock_guard<std::mutex> lk(Lock);
    if (CurState != State::Loading) return;

    if (IsHost)
    {
        Slots[0].Loaded = true;
        checkAllLoaded();
    }
    else
    {
        Writer w(Msg_Loaded);
        sendTo(0, kChanSession, w);
    }
}

// with Lock held, host: once every player is ready, tell everyone to go
void Session::checkAllLoaded()
{
    for (int s : Order)
        if (Slots[s].Present && !Slots[s].Loaded) return;

    Writer w(Msg_Go);
    sendAll(kChanSession, w);
    CurState = State::Playing;
    emitLater([this] { emit go(); });
}

void Session::leave()
{
    bool wasRunning = NetThread.joinable();
    {
        std::lock_guard<std::mutex> lk(Lock);
        if (CurState != State::Idle && CurState != State::Ended)
            CurState = State::Ended;
    }
    Quit = true;
    InputArrived.notify_all();

    if (wasRunning)
    {
        NetThread.join();
        Link.Close(IsHost ? Bye_Closed : Bye_Left);
    }
}

// --- chat, delay, players ---

void Session::sendChat(const QString& text)
{
    std::string t = CleanText(text.toStdString(), "", kMaxChat);
    if (t.empty()) return;

    std::lock_guard<std::mutex> lk(Lock);
    if (CurState == State::Idle || CurState == State::Ended || CurState == State::Connecting) return;
    if (IsHost)
        relayChat(MySlot, t);
    else if (MySlot >= 0)
    {
        Writer w(Msg_Chat);
        w.Str(t, kMaxChat);
        sendTo(0, kChanSession, w);
    }
}

// with Lock held, host: a chat message to everyone (and to this player)
void Session::relayChat(int slot, const std::string& text)
{
    Writer w(Msg_Chat);
    w.U8((u8)slot).Str(text, kMaxChat);
    sendAll(kChanSession, w);

    QString name = QString::fromStdString(Slots[slot].Name);
    QString t = QString::fromStdString(text);
    emitLater([this, name, t] { emit chat(name, t); });
}

void Session::setInputDelay(int frames)
{
    frames = std::clamp(frames, 1, kMaxInputDelay);
    std::lock_guard<std::mutex> lk(Lock);
    if (!IsHost || CurState == State::Idle || CurState == State::Ended) return;

    Writer w(Msg_Delay);
    w.U8((u8)frames);
    sendAll(kChanSession, w);
    applyDelay(frames);
}

// with Lock held: the room's delay changed
void Session::applyDelay(int frames)
{
    Room.InputDelay = (u8)frames;
    if (CurState == State::Playing || CurState == State::Loading)
        Delay = frames;
    emitLater([this, frames] { emit delayChanged(frames); });
}

int Session::inputDelay() const
{
    std::lock_guard<std::mutex> lk(Lock);
    return (CurState == State::Playing || CurState == State::Loading) ? Delay : Room.InputDelay;
}

int Session::suggestedDelay() const
{
    std::lock_guard<std::mutex> lk(Lock);

    // inputs between two players go through the host: the slowest trip is
    // half of the two largest round trips to the host added up
    int a = 0, b = 0;
    for (int i = 1; i < kMaxPlayers; i++)
    {
        if (!Slots[i].Present || Slots[i].Ping < 0) continue;
        int p = Slots[i].Ping;
        if (p > a) { b = a; a = p; }
        else if (p > b) b = p;
    }
    return DelayForTrip((a + b) / 2);
}

bool Session::removePlayer(int slot)
{
    std::lock_guard<std::mutex> lk(Lock);
    if (!IsHost || CurState != State::Lobby || slot <= 0 || slot >= kMaxPlayers || !Slots[slot].Present)
        return false;

    QString name = QString::fromStdString(Slots[slot].Name);
    Link.Disconnect(Slots[slot].Peer, Bye_Kicked);
    Slots[slot] = Player();
    broadcastRoster();
    emitLater([this, name] { emit notice(tr("%1 was removed from the room.").arg(name)); emit rosterChanged(); });
    return true;
}

// with Lock held, host: everyone's round trip, for showing and for the
// suggested delay
void Session::sendPings()
{
    Writer w(Msg_Pings);
    int n = 0;
    for (int i = 1; i < kMaxPlayers; i++) n += Slots[i].Present;
    w.U8((u8)n);
    for (int i = 1; i < kMaxPlayers; i++)
    {
        if (!Slots[i].Present) continue;
        Slots[i].Ping = Link.PeerRTT(Slots[i].Peer);
        w.U8((u8)i).U16((u16)std::clamp(Slots[i].Ping, 0, 9999));
    }
    sendAll(kChanSession, w);
    emitLater([this] { emit pingsChanged(); });
}

// --- state for the UI ---

int Session::players() const
{
    std::lock_guard<std::mutex> lk(Lock);
    if (!Order.empty()) return (int)Order.size();
    int n = 0;
    for (const Player& p : Slots) n += p.Present;
    return n;
}

std::vector<PlayerView> Session::roster() const
{
    std::lock_guard<std::mutex> lk(Lock);
    std::vector<PlayerView> out;
    for (int i = 0; i < kMaxPlayers; i++)
    {
        const Player& p = Slots[i];
        if (!p.Present) continue;
        bool ready = IsHost ? saveComplete(p) : (i == MySlot ? true : p.HostReady);
        out.push_back({i, QString::fromStdString(p.Name), ready, i == 0 ? -1 : p.Ping});
    }
    return out;
}

std::vector<int> Session::order() const
{
    std::lock_guard<std::mutex> lk(Lock);
    return Order;
}

int Session::myConsole() const
{
    std::lock_guard<std::mutex> lk(Lock);
    return MyConsole;
}

ConsoleInfo Session::consoleOf(int console) const
{
    std::lock_guard<std::mutex> lk(Lock);
    if (console < 0 || console >= (int)Order.size()) return ConsoleInfo();
    return Slots[Order[console]].Console;
}

std::vector<u8> Session::saveOf(int console) const
{
    std::lock_guard<std::mutex> lk(Lock);
    if (console < 0 || console >= (int)Order.size()) return {};
    return Slots[Order[console]].Save;
}

QString Session::nameOf(int console) const
{
    std::lock_guard<std::mutex> lk(Lock);
    if (console < 0 || console >= (int)Order.size()) return QString();
    return QString::fromStdString(Slots[Order[console]].Name);
}

// --- emulation threads ---

void Session::submitLocalInput(u32 frame, const InputFrame& in)
{
    std::lock_guard<std::mutex> lk(Lock);
    if (CurState != State::Playing || MyConsole < 0) return;

    // Only this player decides which frame their input lands on; everyone
    // else plays whatever arrives. So when the delay changes, the frames in
    // between are filled with the last input (delay went up) or this input
    // is dropped until the new delay catches up (delay went down), and every
    // computer still gets exactly the same inputs.
    auto& mine = Inputs[MyConsole];
    u32 target = frame + (u32)Delay;
    if (target < mine.size()) return;

    u32 first = (u32)mine.size();
    while (mine.size() < target)
        mine.push_back(mine.empty() ? InputFrame() : mine.back());
    mine.push_back(in);
    InputArrived.notify_all();

    u32 n = (u32)mine.size() - first;
    Writer w(Msg_Input);
    w.U8((u8)MyConsole).U32(first).U8((u8)n);
    for (u32 f = first; f < mine.size(); f++)
    {
        const InputFrame& i = mine[f];
        w.U16(i.Keys).U8(i.Touching ? 1 : 0).U8(i.TouchX).U8(i.TouchY);
    }
    if (IsHost) sendAll(kChanInput, w);
    else sendTo(0, kChanInput, w);
}

bool Session::waitInput(int console, u32 frame, InputFrame& out)
{
    std::unique_lock<std::mutex> lk(Lock);
    if (console < 0 || console >= kMaxPlayers) return false;

    InputArrived.wait(lk, [&]
    {
        return Quit || CurState == State::Ended || Inputs[console].size() > frame;
    });

    if (Inputs[console].size() <= frame) return false;
    out = Inputs[console][frame];
    return true;
}

bool Session::inputReady(int console, u32 frame) const
{
    std::lock_guard<std::mutex> lk(Lock);
    if (console < 0 || console >= kMaxPlayers) return false;
    return Inputs[console].size() > frame;
}

void Session::reportHash(int console, u32 frame, u64 hash)
{
    std::lock_guard<std::mutex> lk(Lock);
    if (CurState != State::Playing) return;
    if (console < 0 || console >= (int)Order.size()) return;

    auto& mine = MyHashes[frame];
    mine[console] = hash;
    if (++MyHashCount[frame] < (int)Order.size()) return;

    if (IsHost)
    {
        Hashes[frame][MySlot] = mine;
        checkHashes(frame);
    }
    else
    {
        Writer w(Msg_Hash);
        w.U32(frame).U8((u8)Order.size());
        for (size_t c = 0; c < Order.size(); c++) w.U64(mine[c]);
        sendTo(0, kChanInput, w);
    }

    MyHashes.erase(frame);
    MyHashCount.erase(frame);
}

// with Lock held, host: compare the hashes every computer sent for a frame
void Session::checkHashes(u32 frame)
{
    auto it = Hashes.find(frame);
    if (it == Hashes.end()) return;

    for (int s : Order)
        if (Slots[s].Present && !it->second.count(s)) return;

    const auto& ref = it->second.at(MySlot);
    int bad = -1;
    for (auto& [slot, hashes] : it->second)
    {
        for (size_t c = 0; c < Order.size(); c++)
        {
            if (hashes[c] != ref[c]) { bad = (int)c; break; }
        }
        if (bad >= 0) break;
    }

    // older frames can't be completed anymore
    Hashes.erase(Hashes.begin(), std::next(it));

    if (bad >= 0 && !DesyncReported)
    {
        DesyncReported = true;
        Writer w(Msg_Desync);
        w.U32(frame).U8((u8)bad);
        sendAll(kChanSession, w);
        emitLater([this, frame, bad] { emit desync((int)frame, bad); });
    }
}

// --- network thread ---

void Session::runNetwork()
{
    using Clock = std::chrono::steady_clock;

    while (!Quit)
    {
        NetplayLink::Event ev;
        if (Link.Service(ev, 5))
        {
            std::lock_guard<std::mutex> lk(Lock);
            handle(ev);
        }

        // connections that never introduce themselves are dropped
        if (IsHost)
        {
            std::lock_guard<std::mutex> lk(Lock);
            auto now = Clock::now();

            if (now - LastPings > std::chrono::seconds(1) && CurState != State::Ended)
            {
                LastPings = now;
                sendPings();
            }

            for (auto it = PendingPeers.begin(); it != PendingPeers.end(); )
            {
                auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.Since).count();
                if (!it->second.Hello && age > kHelloTimeoutMs)
                {
                    Link.Disconnect(it->first, Bye_Kicked);
                    it = PendingPeers.erase(it);
                }
                else ++it;
            }
        }
    }
}

void Session::handle(const NetplayLink::Event& ev)
{
    if (CurState == State::Ended) return;

    switch (ev.Kind)
    {
    case NetplayLink::Event::Connected:
        if (IsHost)
        {
            if (PendingPeers.size() >= (size_t)kMaxPlayers)
            {
                Link.Disconnect(ev.Peer, Bye_Kicked);
                return;
            }
            Pending p;
            p.Since = std::chrono::steady_clock::now();
            PendingPeers[ev.Peer] = p;
        }
        else
        {
            CurState = State::Lobby;

            // the address that answered (of the ones tried)
            const std::string where = Link.PeerAddress(0);
            const size_t colon = where.rfind(':');
            if (colon != std::string::npos)
            {
                Address = QString::fromStdString(where.substr(0, colon));
                Port = (u16)std::stoi(where.substr(colon + 1));
            }

            Writer w(Msg_Hello);
            w.U32(kMagic).U16(kProtocolVersion).Str(BuildId(), kMaxBuild).Str(MyName, kMaxName);
            sendTo(0, kChanSession, w);
        }
        break;

    case NetplayLink::Event::Disconnected:
        if (IsHost) peerLeft(ev.Peer);
        else
        {
            QString why;
            switch (ev.Reason)
            {
            case Bye_Closed: why = tr("The host closed the room."); break;
            case Bye_Kicked: why = tr("The host removed you from the room."); break;
            default:
                why = (CurState == State::Connecting)
                    ? tr("Couldn't connect. Check the address and port, and that the host has forwarded the port.")
                    : tr("Lost the connection to the host.");
                break;
            }
            if (RejectedWhy.size()) why = RejectedWhy;
            finish(why);
        }
        break;

    case NetplayLink::Event::Message:
        if (IsHost) handleHost(ev.Peer, ev.Data);
        else handleClient(ev.Data);
        break;

    default:
        break;
    }
}

void Session::peerLeft(int peer)
{
    PendingPeers.erase(peer);

    int slot = slotOfPeer(peer);
    if (slot < 0) return;

    QString name = QString::fromStdString(Slots[slot].Name);
    Slots[slot] = Player();

    if (CurState == State::Lobby)
    {
        broadcastRoster();
        emitLater([this, name] { emit notice(tr("%1 left the room.").arg(name)); emit rosterChanged(); });
    }
    else
    {
        // everyone runs every console, so nobody can continue without them
        for (int i = 1; i < kMaxPlayers; i++)
            if (Slots[i].Present) Link.Disconnect(Slots[i].Peer, Bye_Closed);
        finish(tr("%1 left the game.").arg(name));
    }
}

void Session::reject(int peer, RejectReason reason)
{
    Writer w(Msg_Reject);
    w.U8(reason);
    sendTo(peer, kChanSession, w);
    Link.Disconnect(peer, Bye_Kicked);
    PendingPeers.erase(peer);
}

void Session::handleHost(int peer, const std::vector<u8>& data)
{
    Reader r(data);
    u8 type = r.U8();
    int slot = slotOfPeer(peer);

    switch (type)
    {
    case Msg_Hello:
        {
            auto it = PendingPeers.find(peer);
            if (it == PendingPeers.end() || it->second.Hello) { reject(peer, Reject_Protocol); return; }

            u32 magic = r.U32();
            u16 version = r.U16();
            std::string build = r.Str(kMaxBuild);
            std::string name = r.Str(kMaxName);
            if (!r.AtEnd() || magic != kMagic) { reject(peer, Reject_Protocol); return; }
            if (version != kProtocolVersion || build != Room.Build) { reject(peer, Reject_Version); return; }
            if (CurState != State::Lobby) { reject(peer, Reject_Started); return; }

            it->second.Hello = true;
            it->second.Name = CleanName(name, "Player");

            Writer w(Msg_Room);
            w.Str(Room.Build, kMaxBuild).Str(Room.GameCode, 4).Str(Room.Title, kMaxTitle)
             .Bytes(Room.ROMHash.data(), 20).U32(Room.ROMSize).U8(Room.InputDelay);
            sendTo(peer, kChanSession, w);
            return;
        }

    case Msg_HaveROM:
        {
            auto it = PendingPeers.find(peer);
            if (it == PendingPeers.end() || !it->second.Hello) { reject(peer, Reject_Protocol); return; }

            std::array<u8, 20> hash;
            r.Bytes(hash.data(), 20);
            if (!r.AtEnd()) { reject(peer, Reject_Protocol); return; }
            if (hash != Room.ROMHash) { reject(peer, Reject_ROM); return; }
            if (CurState != State::Lobby) { reject(peer, Reject_Started); return; }

            int s = freeSlot();
            if (s < 0) { reject(peer, Reject_Full); return; }

            Player& p = Slots[s];
            p = Player();
            p.Present = true;
            p.Peer = peer;
            p.Name = it->second.Name;
            p.HasROM = true;
            PendingPeers.erase(it);

            // the newcomer gets everyone's DS settings and saves
            for (int i = 0; i < kMaxPlayers; i++)
                if (i != s && Slots[i].Present && Slots[i].HasConsole)
                    sendConsoleAndSave(peer, i);

            broadcastRoster();
            sendInvite(peer);
            QString name = QString::fromStdString(p.Name);
            emitLater([this, name] { emit notice(tr("%1 joined.").arg(name)); emit rosterChanged(); });
            return;
        }

    case Msg_Console:
        {
            if (slot < 0 || CurState != State::Lobby) { reject(peer, Reject_Protocol); return; }
            u8 s = r.U8();
            ConsoleInfo c;
            if (s != slot || !readConsole(r, c) || !r.AtEnd() || Slots[slot].HasConsole)
            {
                reject(peer, Reject_Protocol);
                return;
            }

            Player& p = Slots[slot];
            p.HasConsole = true;
            p.Console = c;
            p.Save.assign(c.SaveSize, 0);
            p.SaveReceived = 0;

            sendConsoleHeader(-1, slot, peer);
            broadcastRoster();
            emitLater([this] { emit rosterChanged(); });
            return;
        }

    case Msg_SaveChunk:
        {
            if (slot < 0 || CurState != State::Lobby) { reject(peer, Reject_Protocol); return; }
            u8 s = r.U8();
            if (s != slot || !readSaveChunk(r, Slots[slot])) { reject(peer, Reject_Protocol); return; }

            // pass it on unchanged
            for (int i = 1; i < kMaxPlayers; i++)
                if (i != slot && Slots[i].Present) Link.Send(Slots[i].Peer, kChanSession, data);

            if (saveComplete(Slots[slot]))
            {
                broadcastRoster();
                emitLater([this] { emit rosterChanged(); });
            }
            return;
        }

    case Msg_Loaded:
        if (slot < 0 || CurState != State::Loading || !r.AtEnd()) { reject(peer, Reject_Protocol); return; }
        Slots[slot].Loaded = true;
        checkAllLoaded();
        return;

    case Msg_Input:
        {
            if (slot < 0 || CurState != State::Playing) return;
            int console = consoleOfSlot(slot);
            if (!readInputs(r, console))
            {
                Link.Disconnect(peer, Bye_Kicked);
                return;
            }
            for (int i = 1; i < kMaxPlayers; i++)
                if (i != slot && Slots[i].Present) Link.Send(Slots[i].Peer, kChanInput, data);
            return;
        }

    case Msg_Chat:
        {
            if (slot < 0) { reject(peer, Reject_Protocol); return; }
            std::string text = r.Str(kMaxChat);
            if (!r.AtEnd()) { reject(peer, Reject_Protocol); return; }
            text = CleanText(text, "", kMaxChat);
            if (text.empty()) return;

            // a few messages at a time; the rest is dropped
            auto now = std::chrono::steady_clock::now();
            auto& times = Slots[slot].ChatTimes;
            while (!times.empty() && now - times.front() > std::chrono::milliseconds(kChatWindowMs))
                times.pop_front();
            if ((int)times.size() >= kChatBurst) return;
            times.push_back(now);

            relayChat(slot, text);
            return;
        }

    case Msg_Hash:
        {
            if (slot < 0 || CurState != State::Playing) return;
            u32 frame = r.U32();
            u8 n = r.U8();
            if (n != Order.size()) { Link.Disconnect(peer, Bye_Kicked); return; }
            std::array<u64, kMaxPlayers> h {};
            for (int c = 0; c < n; c++) h[c] = r.U64();
            if (!r.AtEnd() || frame % kHashInterval) { Link.Disconnect(peer, Bye_Kicked); return; }

            // only keep a bounded window of frames
            if (Hashes.size() > 64) Hashes.erase(Hashes.begin());
            Hashes[frame][slot] = h;
            checkHashes(frame);
            return;
        }

    default:
        reject(peer, Reject_Protocol);
        return;
    }
}

void Session::handleClient(const std::vector<u8>& data)
{
    Reader r(data);
    u8 type = r.U8();

    auto bad = [this]
    {
        finish(tr("The host sent something melonRA didn't understand."));
    };

    switch (type)
    {
    case Msg_Room:
        {
            RoomInfo room;
            room.Build = r.Str(kMaxBuild);
            room.GameCode = r.Str(4);
            room.Title = CleanText(r.Str(kMaxTitle), "", kMaxTitle);
            r.Bytes(room.ROMHash.data(), 20);
            room.ROMSize = r.U32();
            room.InputDelay = r.U8();
            if (!r.AtEnd() || room.InputDelay < 1 || room.InputDelay > kMaxInputDelay) { bad(); return; }
            for (char c : room.GameCode)
                if (!isalnum((unsigned char)c)) { bad(); return; }

            Room = room;
            emitLater([this, room] { emit roomReceived(room); });
            return;
        }

    case Msg_Reject:
        {
            u8 why = r.U8();
            switch (why)
            {
            case Reject_Version: RejectedWhy = tr("The host uses a different melonRA version. Netplay needs exactly the same version on every computer."); break;
            case Reject_ROM: RejectedWhy = tr("Your copy of the game doesn't match the host's. Netplay needs the exact same ROM."); break;
            case Reject_Full: RejectedWhy = tr("The room is full."); break;
            case Reject_Started: RejectedWhy = tr("The game in this room has already started."); break;
            default: RejectedWhy = tr("The host refused the connection."); break;
            }
            return;
        }

    case Msg_Roster:
        {
            u8 yours = r.U8();
            u8 n = r.U8();
            if (n > kMaxPlayers || yours >= kMaxPlayers) { bad(); return; }

            std::array<bool, kMaxPlayers> seen {};
            std::array<std::string, kMaxPlayers> names;
            std::array<bool, kMaxPlayers> ready {};
            for (int i = 0; i < n; i++)
            {
                u8 s = r.U8();
                std::string name = r.Str(kMaxName);
                u8 rd = r.U8();
                if (!r.Ok() || s >= kMaxPlayers || seen[s]) { bad(); return; }
                seen[s] = true;
                names[s] = CleanName(name, "Player");
                ready[s] = rd != 0;
            }
            if (!r.AtEnd() || !seen[yours]) { bad(); return; }

            bool first = (MySlot < 0);
            MySlot = yours;
            for (int i = 0; i < kMaxPlayers; i++)
            {
                Player& p = Slots[i];
                if (!seen[i]) { if (i != MySlot) p = Player(); continue; }
                bool had = p.Present;
                p.Present = true;
                p.Name = names[i];
                if (!had) { p.HasConsole = false; p.Save.clear(); p.SaveReceived = 0; }
                p.HostReady = ready[i];
            }

            if (first)
            {
                // send our DS settings and save now that we have a slot
                Player& me = Slots[MySlot];
                me.Name = MyName;
                me.HasConsole = true;
                me.Console = PendingConsole;
                me.Save = std::move(PendingSave);
                me.SaveReceived = me.Console.SaveSize;
                sendConsoleAndSave(0, MySlot);
            }

            emitLater([this] { emit rosterChanged(); });
            return;
        }

    case Msg_Console:
        {
            u8 s = r.U8();
            ConsoleInfo c;
            if (s >= kMaxPlayers || s == MySlot || !readConsole(r, c) || !r.AtEnd()) { bad(); return; }
            Player& p = Slots[s];
            p.Present = true;
            p.HasConsole = true;
            p.Console = c;
            p.Save.assign(c.SaveSize, 0);
            p.SaveReceived = 0;
            return;
        }

    case Msg_SaveChunk:
        {
            u8 s = r.U8();
            if (s >= kMaxPlayers || s == MySlot || !Slots[s].HasConsole || !readSaveChunk(r, Slots[s])) { bad(); return; }
            return;
        }

    case Msg_Start:
        {
            if (CurState != State::Lobby || MySlot < 0) { bad(); return; }
            StartInfo st;
            st.Year = r.U16(); st.Month = r.U8(); st.Day = r.U8();
            st.Hour = r.U8(); st.Minute = r.U8(); st.Second = r.U8();
            st.InputDelay = r.U8();
            st.JIT = r.U8(); st.JITBlockSize = r.U8(); st.JITLiteral = r.U8();
            st.JITBranch = r.U8(); st.JITFastMem = r.U8();
            st.Players = r.U8();
            if (!r.Ok() || st.Players < 2 || st.Players > kMaxPlayers) { bad(); return; }

            std::vector<int> order;
            std::array<bool, kMaxPlayers> seen {};
            for (int i = 0; i < st.Players; i++)
            {
                u8 s = r.U8();
                if (!r.Ok() || s >= kMaxPlayers || seen[s]) { bad(); return; }
                seen[s] = true;
                order.push_back(s);
            }
            if (!r.AtEnd() || !seen[MySlot]) { bad(); return; }
            if (st.InputDelay < 1 || st.InputDelay > kMaxInputDelay || st.JITBlockSize < 1 || st.JITBlockSize > 32 ||
                st.Year < 2000 || st.Year > 2099 || st.Month < 1 || st.Month > 12 || st.Day < 1 || st.Day > 31 ||
                st.Hour > 23 || st.Minute > 59 || st.Second > 59) { bad(); return; }

            for (int s : order)
            {
                if (!Slots[s].HasConsole || !saveComplete(Slots[s]))
                {
                    finish(tr("Not every player's data arrived before the game started."));
                    return;
                }
            }

            Start = st;
            Order = order;
            beginLoading();
            emitLater([this] { emit started(); });
            return;
        }

    case Msg_Go:
        if (CurState != State::Loading || !r.AtEnd()) { bad(); return; }
        CurState = State::Playing;
        emitLater([this] { emit go(); });
        return;

    case Msg_Input:
        {
            if (CurState != State::Playing && CurState != State::Loading) return;
            u8 console = 0;
            if (r.Left() > 0) console = data[1];
            if (console == MyConsole || !readInputs(r, console)) { bad(); return; }
            return;
        }

    case Msg_Chat:
        {
            u8 s = r.U8();
            std::string text = r.Str(kMaxChat);
            if (!r.AtEnd() || s >= kMaxPlayers) { bad(); return; }
            text = CleanText(text, "", kMaxChat);
            if (text.empty()) return;
            QString name = QString::fromStdString(Slots[s].Present ? Slots[s].Name : "?");
            QString t = QString::fromStdString(text);
            emitLater([this, name, t] { emit chat(name, t); });
            return;
        }

    case Msg_Delay:
        {
            u8 d = r.U8();
            if (!r.AtEnd() || d < 1 || d > kMaxInputDelay) { bad(); return; }
            applyDelay(d);
            return;
        }

    case Msg_Invite:
        {
            std::string id = r.Str(kMaxPartyId);
            std::string secret = r.Str(kMaxJoinSecret);
            if (!r.AtEnd() || !ValidInviteText(id, kMaxPartyId) || !ValidInviteText(secret, kMaxJoinSecret)) { bad(); return; }
            PartyId = id;
            JoinSecret = secret;
            emitLater([this] { emit inviteChanged(); });
            return;
        }

    case Msg_Pings:
        {
            u8 n = r.U8();
            if (n > kMaxPlayers) { bad(); return; }
            for (int i = 0; i < n; i++)
            {
                u8 s = r.U8();
                u16 ms = r.U16();
                if (!r.Ok() || s >= kMaxPlayers) { bad(); return; }
                Slots[s].Ping = ms;
            }
            if (!r.AtEnd()) { bad(); return; }
            emitLater([this] { emit pingsChanged(); });
            return;
        }

    case Msg_Desync:
        {
            u32 frame = r.U32();
            u8 console = r.U8();
            if (!r.AtEnd() || console >= Order.size()) { bad(); return; }
            emitLater([this, frame, console] { emit desync((int)frame, console); });
            return;
        }

    default:
        bad();
        return;
    }
}

// --- message pieces ---

bool Session::readConsole(Reader& r, ConsoleInfo& out)
{
    out.Nickname = CleanDSText(r.Str16(kMaxNickname), kMaxNickname);
    out.Message = CleanDSText(r.Str16(kMaxMessage), kMaxMessage);
    out.Language = r.U8();
    out.FavoriteColor = r.U8();
    out.BirthdayMonth = r.U8();
    out.BirthdayDay = r.U8();
    out.SaveSize = r.U32();
    if (!r.Ok()) return false;
    if (out.Language > 7 || out.FavoriteColor > 15) return false;
    if (out.BirthdayMonth < 1 || out.BirthdayMonth > 12 || out.BirthdayDay < 1 || out.BirthdayDay > 31) return false;
    return ValidSaveSize(out.SaveSize);
}

void Session::writeConsole(Writer& w, const ConsoleInfo& c)
{
    w.Str16(c.Nickname, kMaxNickname).Str16(c.Message, kMaxMessage)
     .U8(c.Language).U8(c.FavoriteColor).U8(c.BirthdayMonth).U8(c.BirthdayDay)
     .U32(c.SaveSize);
}

bool Session::readSaveChunk(Reader& r, Player& p)
{
    u32 offset = r.U32();
    u16 len = r.U16();
    if (!r.Ok() || len == 0 || len > kSaveChunk) return false;
    if (offset != p.SaveReceived || (u64)offset + len > p.Save.size()) return false;
    if (!r.Bytes(p.Save.data() + offset, len) || !r.AtEnd()) return false;
    p.SaveReceived += len;
    return true;
}

// a player's DS settings, followed by their save in chunks
void Session::sendConsoleAndSave(int toPeer, int slot)
{
    const Player& p = Slots[slot];

    Writer w(Msg_Console);
    w.U8((u8)slot);
    writeConsole(w, p.Console);
    sendTo(toPeer, kChanSession, w);

    for (u32 off = 0; off < p.Console.SaveSize; off += kSaveChunk)
    {
        u16 len = (u16)std::min<u32>(kSaveChunk, p.Console.SaveSize - off);
        Writer c(Msg_SaveChunk);
        c.U8((u8)slot).U32(off).U16(len).Bytes(p.Save.data() + off, len);
        sendTo(toPeer, kChanSession, c);
    }
}

// host: a player's DS settings to every other client (their save chunks are
// passed on as they arrive)
void Session::sendConsoleHeader(int, int slot, int exceptPeer)
{
    Writer w(Msg_Console);
    w.U8((u8)slot);
    writeConsole(w, Slots[slot].Console);
    sendAll(kChanSession, w, exceptPeer);
}

bool Session::readInputs(Reader& r, int console)
{
    u8 c = r.U8();
    u32 first = r.U32();
    u8 n = r.U8();
    if (!r.Ok() || c != console || console < 0 || console >= (int)Order.size()) return false;
    if (n == 0 || n > kMaxInputBatch) return false;

    auto& list = Inputs[console];
    for (int i = 0; i < n; i++)
    {
        InputFrame in;
        in.Keys = r.U16() & 0xFFF;
        in.Touching = r.U8() & 1;
        in.TouchX = r.U8();
        in.TouchY = r.U8();
        if (!r.Ok()) return false;
        if (in.TouchY >= 192) in.TouchY = 191;

        u32 frame = first + i;
        if (frame < list.size()) continue;      // already have it
        if (frame != list.size()) return false; // frames can't be skipped
        list.push_back(in);
    }
    if (!r.AtEnd()) return false;

    InputArrived.notify_all();
    return true;
}

void Session::broadcastRoster()
{
    for (int i = 1; i < kMaxPlayers; i++)
    {
        if (!Slots[i].Present) continue;

        Writer w(Msg_Roster);
        w.U8((u8)i);
        int n = 0;
        for (const Player& p : Slots) n += p.Present;
        w.U8((u8)n);
        for (int s = 0; s < kMaxPlayers; s++)
        {
            const Player& p = Slots[s];
            if (!p.Present) continue;
            w.U8((u8)s).Str(p.Name, kMaxName).U8(p.HasConsole && saveComplete(p));
        }
        sendTo(Slots[i].Peer, kChanSession, w);
    }
}

void Session::sendTo(int peer, int channel, const Writer& w)
{
    Link.Send(peer, channel, w.Data);
}

void Session::sendAll(int channel, const Writer& w, int exceptPeer)
{
    for (int i = 1; i < kMaxPlayers; i++)
    {
        if (!Slots[i].Present || Slots[i].Peer == exceptPeer) continue;
        Link.Send(Slots[i].Peer, channel, w.Data);
    }
}

bool Session::saveComplete(const Player& p) const
{
    return p.HasConsole && p.SaveReceived == p.Console.SaveSize;
}

int Session::slotOfPeer(int peer) const
{
    if (peer < 0) return -1;
    for (int i = 1; i < kMaxPlayers; i++)
        if (Slots[i].Present && Slots[i].Peer == peer) return i;
    return -1;
}

int Session::consoleOfSlot(int slot) const
{
    for (size_t c = 0; c < Order.size(); c++)
        if (Order[c] == slot) return (int)c;
    return -1;
}

int Session::freeSlot() const
{
    for (int i = 1; i < kMaxPlayers; i++)
        if (!Slots[i].Present) return i;
    return -1;
}

// with Lock held
void Session::finish(const QString& reason)
{
    if (CurState == State::Ended) return;
    CurState = State::Ended;
    Quit = true;
    InputArrived.notify_all();
    emitLater([this, reason] { emit ended(reason); });
}

}
