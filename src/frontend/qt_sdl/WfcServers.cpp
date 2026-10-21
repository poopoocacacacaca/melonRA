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

#include "WfcServers.h"

#include <QHostAddress>

#include "Config.h"
#include "SPI_Firmware.h"

namespace WfcServers
{

const std::vector<Server>& servers()
{
    // Addresses as published by each service. If one changes, "Custom DNS
    // server" works until melonRA is updated.
    static const std::vector<Server> list = {
        {"wiimmfi", "Wiimmfi", "167.235.229.36",
         "The largest Nintendo WFC replacement, through the WiiLink DNS."},
        {"kaeru", "Kaeru WFC", "178.62.43.212",
         "A Nintendo WFC replacement made for DS and DSi games."},
    };
    return list;
}

static Config::Table lanConfig()
{
    return Config::GetGlobalTable().GetTable("LAN");
}

QString selected()
{
    return lanConfig().GetQString("WFCServer");
}

QString customDns()
{
    return lanConfig().GetQString("WFCCustomDNS");
}

void setSelection(const QString& id, const QString& dns)
{
    Config::Table cfg = lanConfig();
    cfg.SetQString("WFCServer", id);
    cfg.SetQString("WFCCustomDNS", dns.trimmed());
}

bool isValidAddress(const QString& text)
{
    QHostAddress addr;
    return addr.setAddress(text.trimmed()) && addr.protocol() == QAbstractSocket::IPv4Protocol;
}

QString selectedDns()
{
    const QString id = selected();
    if (id.isEmpty()) return {};
    if (id == "custom")
    {
        const QString dns = customDns();
        return isValidAddress(dns) ? dns.trimmed() : QString();
    }
    for (const Server& s : servers())
        if (id == s.id) return s.dns;
    return {};
}

void apply(melonDS::Firmware& firmware)
{
    const QString dns = selectedDns();
    if (dns.isEmpty()) return;

    const quint32 ip = QHostAddress(dns).toIPv4Address();
    const melonDS::IpAddress addr = {(melonDS::u8)(ip >> 24), (melonDS::u8)(ip >> 16),
                                               (melonDS::u8)(ip >> 8), (melonDS::u8)ip};

    // connections 1-3, which DS games use; only ones that are set up
    for (auto& ap : firmware.GetAccessPoints())
    {
        if (ap.Status != melonDS::Firmware::AccessPointStatus::Normal) continue;
        // both entries, so a failing server never falls back to Nintendo's
        ap.PrimaryDns = addr;
        ap.SecondaryDns = addr;
        ap.UpdateChecksum();
    }
    firmware.UpdateChecksums();
}

}
