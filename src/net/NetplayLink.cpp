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

#include <cstdio>
#include <enet/enet.h>

#include "NetplayLink.h"

namespace melonDS
{

int NetplayLink::Users = 0;
std::mutex NetplayLink::InitLock;

NetplayLink::NetplayLink()
{
    std::lock_guard<std::mutex> lk(InitLock);
    if (Users++ == 0)
        enet_initialize();
}

NetplayLink::~NetplayLink()
{
    Close();

    std::lock_guard<std::mutex> lk(InitLock);
    if (--Users == 0)
        enet_deinitialize();
}

bool NetplayLink::Host(u16 port, int maxPeers, std::string& error)
{
    Close();

    ENetAddress addr;
    addr.host = ENET_HOST_ANY;
    addr.port = port;

    ENetHost* host = enet_host_create(&addr, maxPeers, kChannels, 0, 0);
    if (!host)
    {
        error = "Couldn't open port " + std::to_string(port) + ". It may already be in use.";
        return false;
    }

    host->maximumPacketSize = kMaxMessage;
    host->maximumWaitingData = 4 * kMaxMessage;
    Handle = host;
    Peers.assign(maxPeers, nullptr);
    return true;
}

bool NetplayLink::Connect(const std::string& address, u16 port, std::string& error)
{
    Close();

    ENetAddress addr;
    if (enet_address_set_host(&addr, address.c_str()) != 0)
    {
        error = "Couldn't find \"" + address + "\".";
        return false;
    }
    addr.port = port;

    ENetHost* host = enet_host_create(nullptr, 1, kChannels, 0, 0);
    if (!host)
    {
        error = "Couldn't set up the network connection.";
        return false;
    }
    host->maximumPacketSize = kMaxMessage;
    host->maximumWaitingData = 4 * kMaxMessage;

    ENetPeer* peer = enet_host_connect(host, &addr, kChannels, 0);
    if (!peer)
    {
        enet_host_destroy(host);
        error = "Couldn't set up the network connection.";
        return false;
    }

    Handle = host;
    Peers.assign(1, peer);
    return true;
}

void NetplayLink::Close(u32 reason)
{
    if (!Handle) return;

    ENetHost* host = (ENetHost*)Handle;
    flushOutbox();
    for (void* p : Peers)
        if (p) enet_peer_disconnect((ENetPeer*)p, reason);

    // give the goodbyes a moment to go out
    ENetEvent e;
    for (int i = 0; i < 20 && enet_host_service(host, &e, 10) >= 0; i++)
    {
        if (e.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(e.packet);
    }
    for (void* p : Peers)
        if (p && ((ENetPeer*)p)->state != ENET_PEER_STATE_DISCONNECTED)
            enet_peer_reset((ENetPeer*)p);
    enet_host_destroy(host);

    Handle = nullptr;
    Peers.clear();

    std::lock_guard<std::mutex> lk(OutLock);
    Outbox.clear();
}

int NetplayLink::peerIndex(void* peer) const
{
    for (size_t i = 0; i < Peers.size(); i++)
        if (Peers[i] == peer) return (int)i;
    return -1;
}

void NetplayLink::Send(int peer, int channel, std::vector<u8> data)
{
    if (data.empty() || data.size() > kMaxMessage) return;
    if (channel < 0 || channel >= kChannels) return;

    std::lock_guard<std::mutex> lk(OutLock);
    Outbox.push_back({peer, channel, false, 0, std::move(data)});
}

void NetplayLink::Disconnect(int peer, u32 reason)
{
    std::lock_guard<std::mutex> lk(OutLock);
    Outbox.push_back({peer, 0, true, reason, {}});
}

void NetplayLink::flushOutbox()
{
    std::deque<Outgoing> out;
    {
        std::lock_guard<std::mutex> lk(OutLock);
        out.swap(Outbox);
    }

    for (Outgoing& o : out)
    {
        if (o.Peer < 0 || o.Peer >= (int)Peers.size()) continue;
        ENetPeer* peer = (ENetPeer*)Peers[o.Peer];
        if (!peer) continue;

        if (o.Drop)
        {
            enet_peer_disconnect_later(peer, o.Reason);
            continue;
        }

        ENetPacket* pkt = enet_packet_create(o.Data.data(), o.Data.size(), ENET_PACKET_FLAG_RELIABLE);
        if (pkt && enet_peer_send(peer, (u8)o.Channel, pkt) != 0)
            enet_packet_destroy(pkt);
    }
}

bool NetplayLink::Service(Event& ev, int timeoutMs)
{
    ev = Event();
    if (!Handle) return false;
    ENetHost* host = (ENetHost*)Handle;

    flushOutbox();

    ENetEvent e;
    int res = enet_host_service(host, &e, timeoutMs < 0 ? 0 : timeoutMs);
    if (res <= 0) return false;

    switch (e.type)
    {
    case ENET_EVENT_TYPE_CONNECT:
        {
            int idx = peerIndex(e.peer);
            if (idx < 0)
            {
                for (size_t i = 0; i < Peers.size(); i++)
                {
                    if (!Peers[i]) { Peers[i] = e.peer; idx = (int)i; break; }
                }
            }
            if (idx < 0)
            {
                enet_peer_disconnect_now(e.peer, 0);
                return false;
            }
            ev.Kind = Event::Connected;
            ev.Peer = idx;
            return true;
        }

    case ENET_EVENT_TYPE_DISCONNECT:
        {
            int idx = peerIndex(e.peer);
            if (idx < 0) return false;
            // the client keeps its single peer slot, so a later connect works
            if (Peers.size() > 1) Peers[idx] = nullptr;
            ev.Kind = Event::Disconnected;
            ev.Peer = idx;
            ev.Reason = e.data;
            return true;
        }

    case ENET_EVENT_TYPE_RECEIVE:
        {
            int idx = peerIndex(e.peer);
            if (idx >= 0 && e.packet->dataLength > 0 && e.packet->dataLength <= kMaxMessage)
            {
                ev.Kind = Event::Message;
                ev.Peer = idx;
                ev.Channel = e.channelID;
                ev.Data.assign(e.packet->data, e.packet->data + e.packet->dataLength);
            }
            enet_packet_destroy(e.packet);
            return ev.Kind != Event::None;
        }

    default:
        return false;
    }
}

std::string NetplayLink::PeerAddress(int peer)
{
    if (peer < 0 || peer >= (int)Peers.size() || !Peers[peer]) return "";
    ENetPeer* p = (ENetPeer*)Peers[peer];
    char ip[64] = {0};
    if (enet_address_get_host_ip(&p->address, ip, sizeof(ip) - 1) != 0) return "";
    return std::string(ip) + ":" + std::to_string(p->address.port);
}

}
