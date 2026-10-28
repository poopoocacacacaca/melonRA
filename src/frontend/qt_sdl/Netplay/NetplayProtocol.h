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

#ifndef NETPLAYPROTOCOL_H
#define NETPLAYPROTOCOL_H

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

#include "types.h"

// Messages between netplay players. Every message starts with its type byte;
// all numbers are little-endian. Everything read from the network goes
// through Reader, which never reads past the end: a short or malformed
// message just fails to parse and the sender is disconnected.
namespace Netplay
{
using melonDS::u8;
using melonDS::u16;
using melonDS::u32;
using melonDS::u64;

constexpr u32 kMagic = 0x504E524D;  // "MRNP"
constexpr u16 kProtocolVersion = 1;
constexpr u16 kDefaultPort = 7066;
constexpr int kMaxPlayers = 4;

constexpr int kMaxName = 24;          // bytes of UTF-8
constexpr int kMaxTitle = 128;
constexpr int kMaxBuild = 64;
constexpr int kMaxNickname = 10;      // UTF-16 units, as in the DS firmware
constexpr int kMaxMessage = 26;
constexpr u32 kMaxSave = 32 * 1024 * 1024;
constexpr u32 kSaveChunk = 32 * 1024;
constexpr int kMaxInputDelay = 15;
constexpr int kMaxInputBatch = 32;
constexpr int kHashInterval = 60;     // frames between desync checks

enum MsgType : u8
{
    Msg_Hello = 1,      // client -> host: magic, version, build, name
    Msg_Room,           // host -> client: the game and room settings
    Msg_HaveROM,        // client -> host: hash of the ROM it found
    Msg_Reject,         // host -> client: why it can't join
    Msg_Roster,         // host -> all: who is in the room
    Msg_Console,        // player -> host -> all: that player's DS settings and save size
    Msg_SaveChunk,      // player -> host -> all: part of that player's save
    Msg_Start,          // host -> all: start the game
    Msg_Loaded,         // player -> host: consoles are set up
    Msg_Go,             // host -> all: everyone is loaded
    Msg_Input,          // player -> host -> all: inputs for a range of frames
    Msg_Hash,           // player -> host: state hashes of every console
    Msg_Desync,         // host -> all: consoles stopped matching
    Msg_Bye,            // anyone: leaving, with a reason
};

enum RejectReason : u8
{
    Reject_Version = 1,   // different melonRA build
    Reject_ROM,           // doesn't have the same game
    Reject_Full,
    Reject_Started,       // the game already started
    Reject_Protocol,      // malformed messages
};

enum ByeReason : u8
{
    Bye_Left = 0,
    Bye_Closed,       // the host closed the room
    Bye_Kicked,
};

struct ConsoleInfo
{
    std::u16string Nickname;
    std::u16string Message;
    u8 Language = 1;          // English
    u8 FavoriteColor = 0;
    u8 BirthdayMonth = 1;
    u8 BirthdayDay = 1;
    u32 SaveSize = 0;
};

struct StartInfo
{
    u16 Year = 2000;
    u8 Month = 1, Day = 1, Hour = 0, Minute = 0, Second = 0;
    u8 InputDelay = 3;
    u8 Players = 2;
    bool JIT = false;
    u8 JITBlockSize = 32;
    bool JITLiteral = true;
    bool JITBranch = true;
    bool JITFastMem = true;
};

struct InputFrame
{
    u16 Keys = 0xFFF;     // DS key bits as the core expects them (1 = released)
    bool Touching = false;
    u8 TouchX = 0;
    u8 TouchY = 0;

    bool operator==(const InputFrame& o) const
    {
        return Keys == o.Keys && Touching == o.Touching && TouchX == o.TouchX && TouchY == o.TouchY;
    }
};

class Writer
{
public:
    explicit Writer(MsgType type) { Data.push_back(type); }

    Writer& U8(u8 v) { Data.push_back(v); return *this; }
    Writer& U16(u16 v) { U8(v & 0xFF); return U8(v >> 8); }
    Writer& U32(u32 v) { U16(v & 0xFFFF); return U16(v >> 16); }
    Writer& U64(u64 v) { U32((u32)v); return U32((u32)(v >> 32)); }
    Writer& Bytes(const u8* p, size_t n) { Data.insert(Data.end(), p, p + n); return *this; }

    // length-prefixed, cut to max bytes (UTF-8 is cut on a character boundary)
    Writer& Str(const std::string& s, size_t max)
    {
        size_t n = std::min(s.size(), max);
        while (n > 0 && n < s.size() && ((u8)s[n] & 0xC0) == 0x80) n--;
        U8((u8)n);
        return Bytes((const u8*)s.data(), n);
    }

    Writer& Str16(const std::u16string& s, size_t max)
    {
        size_t n = std::min(s.size(), max);
        U8((u8)n);
        for (size_t i = 0; i < n; i++) U16(s[i]);
        return *this;
    }

    std::vector<u8> Data;
};

class Reader
{
public:
    Reader(const std::vector<u8>& d) : P(d.data()), End(d.data() + d.size()) {}

    bool Ok() const { return Good; }
    bool AtEnd() const { return Good && P == End; }
    size_t Left() const { return Good ? (size_t)(End - P) : 0; }

    u8 U8()
    {
        if (!Good || P >= End) { Good = false; return 0; }
        return *P++;
    }
    u16 U16() { u16 lo = U8(); return lo | (u16)(U8() << 8); }
    u32 U32() { u32 lo = U16(); return lo | ((u32)U16() << 16); }
    u64 U64() { u64 lo = U32(); return lo | ((u64)U32() << 32); }

    bool Bytes(u8* out, size_t n)
    {
        if (!Good || (size_t)(End - P) < n) { Good = false; return false; }
        memcpy(out, P, n);
        P += n;
        return true;
    }

    // a length-prefixed string; longer than max fails the whole message
    std::string Str(size_t max)
    {
        size_t n = U8();
        if (!Good || n > max || (size_t)(End - P) < n) { Good = false; return ""; }
        std::string s((const char*)P, n);
        P += n;
        return s;
    }

    std::u16string Str16(size_t max)
    {
        size_t n = U8();
        if (!Good || n > max || (size_t)(End - P) < n * 2) { Good = false; return u""; }
        std::u16string s;
        for (size_t i = 0; i < n; i++) s.push_back(U16());
        return s;
    }

private:
    const u8* P;
    const u8* End;
    bool Good = true;
};

// what a peer chose as its name, made safe to show: valid UTF-8 only, no
// control or formatting characters, trimmed, never empty
inline std::string CleanName(const std::string& in, const std::string& fallback)
{
    std::string out;
    size_t i = 0;
    while (i < in.size() && out.size() < (size_t)kMaxName)
    {
        u8 c = (u8)in[i];
        size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (len == 0 || i + len > in.size()) break;

        u32 cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
        bool valid = true;
        for (size_t k = 1; k < len; k++)
        {
            u8 cc = (u8)in[i + k];
            if ((cc & 0xC0) != 0x80) { valid = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!valid) break;

        bool control = cp < 0x20 || (cp >= 0x7F && cp < 0xA0) ||
                       (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) ||
                       (cp >= 0x2066 && cp <= 0x2069) || cp == 0xFEFF || cp > 0x10FFFF ||
                       (cp >= 0xD800 && cp <= 0xDFFF);
        if (!control && out.size() + len <= (size_t)kMaxName)
            out.append(in, i, len);
        i += len;
    }

    while (!out.empty() && out.back() == ' ') out.pop_back();
    size_t start = out.find_first_not_of(' ');
    out = (start == std::string::npos) ? "" : out.substr(start);
    return out.empty() ? fallback : out;
}

// the DS nickname and message only hold printable text
inline std::u16string CleanDSText(const std::u16string& in, size_t max)
{
    std::u16string out;
    for (char16_t c : in)
    {
        if (out.size() >= max) break;
        if (c < 0x20 || (c >= 0x7F && c < 0xA0) || (c >= 0xD800 && c <= 0xDFFF)) continue;
        out.push_back(c);
    }
    return out;
}

// a save file is passed on byte for byte, as the emulator loads it
inline bool ValidSaveSize(u32 size)
{
    return size <= kMaxSave;
}

}

#endif // NETPLAYPROTOCOL_H
