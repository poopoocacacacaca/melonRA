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

#ifndef NETPLAYCONSOLE_H
#define NETPLAYCONSOLE_H

#include <string>
#include <vector>

#include "NetplayProtocol.h"

namespace Netplay
{
class Session;

// What an emulator instance needs to run one console of a netplay session.
// Set when the instance is created and never changed while it runs.
struct Console
{
    Session* session = nullptr;
    int index = 0;            // console number in the session (= its link ID)
    int count = 2;            // consoles in the session
    bool local = false;       // this computer's own console: shown and played here
    StartInfo start;
    std::vector<u8> save;     // the owner's save when the session started
    std::string savePath;     // local console: the save file it writes to

    // emulation thread only
    u32 frame = 0;            // next frame to run
    bool stalled = false;     // waiting for another player's input
};

// MAC address of a console: the same on every computer, different per console
inline std::string ConsoleMAC(int index)
{
    static const char* kMacs[kMaxPlayers] = {
        "00:09:BF:4E:50:10", "00:09:BF:4E:50:24", "00:09:BF:4E:50:38", "00:09:BF:4E:50:4C",
    };
    return kMacs[index & (kMaxPlayers - 1)];
}

}

#endif // NETPLAYCONSOLE_H
