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
#include <cstring>

#include "LockstepMP.h"
#include "LocalMP.h"

namespace melonDS
{

LockstepMP::LockstepMP() noexcept
{
    for (int i = 0; i < kMax; i++)
        LastHost[i] = -1;
}

LockstepMP::~LockstepMP() noexcept
{
    Shutdown();
}

void LockstepMP::Configure(u16 members, ClockFn clock)
{
    std::lock_guard<std::mutex> lk(Lock);
    Members = members;
    Clock = std::move(clock);
    Closed = false;
    Seq = 0;
    for (int i = 0; i < kMax; i++)
    {
        Clocks[i] = 0;
        Floor[i] = 0;
        Power[i].clear();
        OnSince[i] = 0;
        LastHost[i] = -1;
        CmdClock[i] = 0;
        Inbox[i].clear();
        Replies[i].clear();
    }
}

void LockstepMP::Publish(int inst, u64 clock)
{
    std::lock_guard<std::mutex> lk(Lock);
    if (Closed) return;
    publishLocked(inst, clock);
}

void LockstepMP::Leave(int inst)
{
    std::lock_guard<std::mutex> lk(Lock);
    if (inst < 0 || inst >= kMax) return;
    if (Clocks[inst] == kGone) return;

    // leaving counts as switching the wireless off at the last reported
    // clock, so the others see it at the same emulated moment every time
    Power[inst].push_back({Clocks[inst], false});
    Clocks[inst] = kGone;
    Inbox[inst].clear();
    Replies[inst].clear();
    Changed.notify_all();
}

void LockstepMP::Shutdown()
{
    std::lock_guard<std::mutex> lk(Lock);
    Closed = true;
    // the consoles behind the clocks may be going away
    Clock = nullptr;
    Changed.notify_all();
}

u64 LockstepMP::now(int inst)
{
    u64 clock = Clock ? Clock(inst) : 0;
    return std::max(clock, Floor[inst]);
}

void LockstepMP::publishLocked(int inst, u64 clock)
{
    if (inst < 0 || inst >= kMax) return;
    if (Clocks[inst] == kGone) return;
    clock = std::max(clock, Floor[inst]);
    if (clock > Clocks[inst])
    {
        Clocks[inst] = clock;
        Changed.notify_all();
    }

    // drop what can never be delivered anymore, by emulated time only:
    // - packets older than kStale before this instance's clock
    // - while its wireless is off, everything older than its clock, since
    //   powering on skips those anyway
    // - replies from before its clock, which its next RecvReplies ignores
    u64 cutoff = (clock > kStale) ? clock - kStale : 0;
    if (!Power[inst].empty() && !Power[inst].back().On)
        cutoff = clock;

    auto& box = Inbox[inst];
    while (!box.empty() && box.begin()->first.Clock < cutoff)
        box.erase(box.begin());

    auto& rep = Replies[inst];
    while (!rep.empty() && rep.begin()->first.Clock < std::min(clock, CmdClock[inst]))
        rep.erase(rep.begin());
}

void LockstepMP::waitOthers(std::unique_lock<std::mutex>& lk, int inst, u64 clock)
{
    Changed.wait(lk, [&]
    {
        if (Closed) return true;
        for (int i = 0; i < kMax; i++)
        {
            if (i == inst || !(Members & (1 << i))) continue;
            if (Clocks[i] < clock) return false;
        }
        return true;
    });
}

bool LockstepMP::poweredAt(int inst, u64 clock) const
{
    bool on = false;
    for (const PowerChange& p : Power[inst])
    {
        if (p.Clock >= clock) break;
        on = p.On;
    }
    return on;
}

void LockstepMP::Begin(int inst)
{
    if (inst < 0 || inst >= kMax) return;
    std::lock_guard<std::mutex> lk(Lock);
    if (Closed) return;
    u64 t = now(inst);
    publishLocked(inst, t);
    Power[inst].push_back({t, true});
    OnSince[inst] = t;
    LastHost[inst] = -1;

    auto& box = Inbox[inst];
    while (!box.empty() && box.begin()->first.Clock < t)
        box.erase(box.begin());
}

void LockstepMP::End(int inst)
{
    if (inst < 0 || inst >= kMax) return;
    std::lock_guard<std::mutex> lk(Lock);
    if (Closed) return;
    u64 t = now(inst);
    publishLocked(inst, t);
    Power[inst].push_back({t, false});
    Changed.notify_all();
}

int LockstepMP::send(int inst, u32 type, u8* data, int len, u64 timestamp)
{
    if (inst < 0 || inst >= kMax) return 0;
    if (len < 0 || (u32)len > kMaxFrameSize) return 0;

    std::lock_guard<std::mutex> lk(Lock);
    if (Closed) return len;

    u64 t = now(inst);
    publishLocked(inst, t);

    Key key {t, (u16)inst, Seq++};
    if ((type & 0xFFFF) == 1)
        CmdClock[inst] = t;
    Packet pkt;
    pkt.Type = type;
    pkt.Timestamp = timestamp;
    if (len) pkt.Data.assign(data, data + len);

    if ((type & 0xFFFF) == 2)
    {
        // replies go to the host whose command this instance received last
        int host = LastHost[inst];
        if (host >= 0 && host < kMax && host != inst)
            Replies[host].emplace(key, std::move(pkt));
    }
    else
    {
        for (int i = 0; i < kMax; i++)
        {
            if (i == inst || !(Members & (1 << i))) continue;
            if (Clocks[i] == kGone) continue;
            Inbox[i].emplace(key, pkt);
        }
    }

    return len;
}

int LockstepMP::recv(int inst, u8* data, u64* timestamp, bool host)
{
    if (inst < 0 || inst >= kMax) return 0;

    std::unique_lock<std::mutex> lk(Lock);
    if (Closed) return 0;

    u64 t = now(inst);
    publishLocked(inst, t);
    waitOthers(lk, inst, t);
    if (Closed) return 0;

    if (host)
    {
        // the host that sent our last command switched its wireless off
        int h = LastHost[inst];
        if (h >= 0 && !poweredAt(h, t))
            return -1;
    }

    auto& box = Inbox[inst];
    while (!box.empty())
    {
        auto it = box.begin();
        if (it->first.Clock >= t) return 0;

        if (it->first.Clock < OnSince[inst] || it->first.Clock + kStale < t)
        {
            box.erase(it);
            continue;
        }

        const Packet& pkt = it->second;
        int len = (int)pkt.Data.size();
        if (len) memcpy(data, pkt.Data.data(), len);
        if (timestamp) *timestamp = pkt.Timestamp;
        if ((pkt.Type & 0xFFFF) == 1)
            LastHost[inst] = it->first.Sender;

        box.erase(it);
        return len;
    }

    return 0;
}

int LockstepMP::SendPacket(int inst, u8* data, int len, u64 timestamp)
{
    return send(inst, 0, data, len, timestamp);
}

int LockstepMP::RecvPacket(int inst, u8* data, u64* timestamp)
{
    return recv(inst, data, timestamp, false);
}

int LockstepMP::SendCmd(int inst, u8* data, int len, u64 timestamp)
{
    return send(inst, 1, data, len, timestamp);
}

int LockstepMP::SendReply(int inst, u8* data, int len, u64 timestamp, u16 aid)
{
    return send(inst, 2 | ((u32)aid << 16), data, len, timestamp);
}

int LockstepMP::SendAck(int inst, u8* data, int len, u64 timestamp)
{
    return send(inst, 3, data, len, timestamp);
}

int LockstepMP::RecvHostPacket(int inst, u8* data, u64* timestamp)
{
    return recv(inst, data, timestamp, true);
}

u16 LockstepMP::RecvReplies(int inst, u8* data, u64 timestamp, u16 aidmask)
{
    if (inst < 0 || inst >= kMax) return 0;

    std::unique_lock<std::mutex> lk(Lock);
    if (Closed) return 0;

    aidmask &= 0xFFFE;

    // a client may answer while the command is still in the air, so the
    // window opens when the command was sent
    const u64 start = CmdClock[inst];
    const u64 called = now(inst);
    const u64 limit = called + kMaxReplyWait;

    // the expected clients that answered before `until`, and their answers
    auto answered = [&](u64 until) -> u16
    {
        u16 got = 0;
        for (const auto& [key, pkt] : Replies[inst])
        {
            if (key.Clock >= until) break;
            if (key.Clock < start || pkt.Data.empty()) continue;
            u32 aid = pkt.Type >> 16;
            if (aid >= 1 && aid <= 15 && (aidmask & (1 << aid))) got |= (1 << aid);
        }
        return got;
    };

    // Clients answer when their own (slightly behind) wireless clock says so,
    // which can be well after the host starts listening. melonDS's usual link
    // simply waits for them in real time; here the window grows in steps of
    // emulated time instead, until every expected client has answered or
    // `limit` is reached. Each step depends only on what the clients did
    // before it, so every computer ends up with the same window. Nothing is
    // transmitted before the window's end, so the other consoles may run up to
    // it.
    u64 end = called + kReplyWindow;
    for (;;)
    {
        Floor[inst] = std::max(Floor[inst], end);
        publishLocked(inst, end);

        Changed.wait(lk, [&]
        {
            if (Closed) return true;
            if (aidmask && answered(end) == aidmask) return true;
            for (int i = 0; i < kMax; i++)
            {
                if (i == inst || !(Members & (1 << i))) continue;
                if (Clocks[i] < end) return false;
            }
            return true;
        });
        if (Closed) return 0;

        if (answered(end) == aidmask || end >= limit) break;
        end = std::min(end + kReplyStep, limit);
    }

    u16 ret = 0;
    auto& box = Replies[inst];
    while (!box.empty())
    {
        auto it = box.begin();
        if (it->first.Clock >= end) break;

        const Packet& pkt = it->second;
        u32 aid = pkt.Type >> 16;
        if (it->first.Clock >= start && !pkt.Data.empty() && aid >= 1 && aid <= 15 &&
            (aidmask & (1 << aid)) && !(ret & (1 << aid)))
        {
            size_t len = std::min<size_t>(pkt.Data.size(), 1024);
            memcpy(&data[(aid - 1) * 1024], pkt.Data.data(), len);
            ret |= (1 << aid);
        }

        box.erase(it);
    }

    return ret;
}

}
