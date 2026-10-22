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

#ifndef WFCSERVERS_H
#define WFCSERVERS_H

#include <vector>

#include <QString>

namespace melonDS { class Firmware; }

// Online play after the end of Nintendo Wi-Fi Connection: community servers
// that DS games reach through their DNS server. The choice is written into
// the firmware's Wi-Fi connections (1-3, used by DS games) when a game starts.
namespace WfcServers
{

struct Server
{
    const char* id;      // stored in the config
    const char* name;    // shown in Wi-Fi Settings
    const char* dns;     // the DNS server games are pointed at
    const char* about;   // one line for the settings
};

// the known servers, in the order they're offered
const std::vector<Server>& servers();

// config: "" (leave the firmware's settings alone), a server id, "custom",
// or "off" (no online play; offered in Game Settings)
QString selected();
QString customDns();
void setSelection(const QString& id, const QString& customDns);

// the DNS address for the current choice; empty for "leave alone" or an
// invalid custom address
QString selectedDns();
bool isValidAddress(const QString& text);

// Points every configured Wi-Fi connection of the firmware at the chosen
// server. Does nothing when the firmware's own settings are kept.
void apply(melonDS::Firmware& firmware);

}

#endif // WFCSERVERS_H
