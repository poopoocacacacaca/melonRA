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

#ifndef LOCKSTEPMP_H
#define LOCKSTEPMP_H

#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

#include "types.h"
#include "MPInterface.h"

namespace melonDS
{

// Local wireless link between consoles in the same process whose results
// depend only on emulated time, never on how fast each console's thread runs.
//
// Every packet carries the sender's system clock (33 MHz cycles since boot).
// A console polling at clock t first waits until every other console has
// reached t, then receives the packets sent before t, in clock order. While a
// host waits for MP replies it promises not to transmit before the end of the
// reply window, so clients can catch up and answer.
//
// Used for netplay, where every computer runs all the consoles and they have
// to stay identical.
class LockstepMP : public MPInterface
{
public:
    using ClockFn = std::function<u64(int inst)>;

    // reply window after an MP command, in system clock cycles (256 us)
    static constexpr u64 kReplyWindow = 8580;

    // packets nobody read within this much emulated time are dropped (1 s)
    static constexpr u64 kStale = 33513982;

    LockstepMP() noexcept;
    ~LockstepMP() noexcept override;

    // set up a session: which instances take part, and where their clocks
    // are read from (called from the instance's own emulation thread)
    void Configure(u16 members, ClockFn clock);

    // report how far an instance has run, e.g. at the end of each frame
    void Publish(int inst, u64 clock);

    // an instance stopped for good; nobody waits for it anymore
    void Leave(int inst);

    // end the session: every pending and future wait returns at once
    void Shutdown();

    void Process() override {}

    void Begin(int inst) override;
    void End(int inst) override;

    int SendPacket(int inst, u8* data, int len, u64 timestamp) override;
    int RecvPacket(int inst, u8* data, u64* timestamp) override;
    int SendCmd(int inst, u8* data, int len, u64 timestamp) override;
    int SendReply(int inst, u8* data, int len, u64 timestamp, u16 aid) override;
    int SendAck(int inst, u8* data, int len, u64 timestamp) override;
    int RecvHostPacket(int inst, u8* data, u64* timestamp) override;
    u16 RecvReplies(int inst, u8* data, u64 timestamp, u16 aidmask) override;

private:
    struct Key
    {
        u64 Clock;
        u16 Sender;
        u32 Seq;
        bool operator<(const Key& o) const noexcept
        {
            if (Clock != o.Clock) return Clock < o.Clock;
            if (Sender != o.Sender) return Sender < o.Sender;
            return Seq < o.Seq;
        }
    };

    struct Packet
    {
        u32 Type;       // 0=regular 1=CMD 2=reply 3=ack, aid in the upper half for replies
        u64 Timestamp;  // the sender's wifi timestamp, passed through
        std::vector<u8> Data;
    };

    struct PowerChange
    {
        u64 Clock;
        bool On;
    };

    static constexpr int kMax = 16;
    static constexpr u64 kGone = ~0ULL;

    u64 now(int inst);
    void publishLocked(int inst, u64 clock);
    void waitOthers(std::unique_lock<std::mutex>& lk, int inst, u64 clock);
    bool poweredAt(int inst, u64 clock) const;
    int send(int inst, u32 type, u8* data, int len, u64 timestamp);
    int recv(int inst, u8* data, u64* timestamp, bool host);

    std::mutex Lock;
    std::condition_variable Changed;

    u16 Members = 0;
    ClockFn Clock;
    bool Closed = false;

    u64 Clocks[kMax] {};
    u64 Floor[kMax] {};
    u32 Seq = 0;

    std::vector<PowerChange> Power[kMax];
    u64 OnSince[kMax] {};
    int LastHost[kMax] {};
    u64 CmdClock[kMax] {};

    std::map<Key, Packet> Inbox[kMax];
    std::map<Key, Packet> Replies[kMax];
};

}

#endif // LOCKSTEPMP_H
