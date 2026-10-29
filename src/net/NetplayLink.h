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

#ifndef NETPLAYLINK_H
#define NETPLAYLINK_H

#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "types.h"

namespace melonDS
{

// Connections between netplay players, over ENet (UDP). One side hosts, the
// others connect to it by address and port.
//
// Service() must always be called from the same thread; Send() and
// Disconnect() may be called from any thread and take effect on the next
// Service().
class NetplayLink
{
public:
    static constexpr u32 kMaxMessage = 0x10000;  // larger messages are refused
    static constexpr int kChannels = 2;           // 0 = session, 1 = inputs

    struct Event
    {
        enum Type { None, Connected, Disconnected, Message } Kind = None;
        int Peer = -1;
        int Channel = 0;
        u32 Reason = 0;       // for Disconnected: what the other side said
        std::vector<u8> Data;
    };

    NetplayLink();
    ~NetplayLink();
    NetplayLink(const NetplayLink&) = delete;
    NetplayLink& operator=(const NetplayLink&) = delete;

    bool Host(u16 port, int maxPeers, std::string& error);
    bool Connect(const std::string& address, u16 port, std::string& error);

    // tries several addresses of the same host at once (its public and home
    // network ones, say); the first that answers is kept as peer 0
    struct Target { std::string Address; u16 Port; };
    bool ConnectAny(const std::vector<Target>& targets, std::string& error);
    void Close(u32 reason = 0);

    bool IsOpen() const { return Handle != nullptr; }

    // waits up to timeoutMs for something to happen; returns false if nothing did
    bool Service(Event& ev, int timeoutMs);

    void Send(int peer, int channel, std::vector<u8> data);
    void Disconnect(int peer, u32 reason = 0);

    // "1.2.3.4:5678", for showing who is connected
    std::string PeerAddress(int peer);

    // round trip to a peer in milliseconds, as ENet measures it; -1 if not
    // connected (call from the Service() thread)
    int PeerRTT(int peer);

private:
    struct Outgoing
    {
        int Peer;
        int Channel;
        bool Drop;
        u32 Reason;
        std::vector<u8> Data;
    };

    void flushOutbox();
    int peerIndex(void* peer) const;

    void* Handle = nullptr;
    std::vector<void*> Peers;
    std::vector<void*> Candidates;   // client, while connecting

    std::mutex OutLock;
    std::deque<Outgoing> Outbox;

    static int Users;
    static std::mutex InitLock;
};

}

#endif // NETPLAYLINK_H
