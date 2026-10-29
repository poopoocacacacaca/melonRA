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

#ifndef NETPLAYRUNNER_H
#define NETPLAYRUNNER_H

#include <QObject>
#include <QStringList>

#include "NetplaySession.h"
#include "PortMapper.h"

class MainWindow;
class EmuInstance;

namespace Netplay
{

// Runs a netplay session on this computer: the room, then one console per
// player (this player's in the main window, the others hidden), and putting
// everything back afterwards. At most one exists at a time.
class Runner : public QObject
{
    Q_OBJECT

public:
    static Runner* current() { return Current; }

    // checks that a session can start here; empty if it can
    static QString cantStart(EmuInstance* inst);

    explicit Runner(MainWindow* win);
    ~Runner() override;

    // openPort: ask the router to forward the port (UPnP)
    bool host(const QStringList& romPath, const QString& name, u16 port, int inputDelay,
              bool openPort, QString& error);
    bool join(const QString& address, u16 port, const QString& name, QString& error);

    // joining from a Discord invite: the room's join secret, checked strictly
    static bool parseJoinSecret(const QString& secret, std::vector<melonDS::NetplayLink::Target>& targets);
    bool joinFromDiscord(const QString& secret, const QString& name, QString& error);

    // host: the router forwarding and the public address
    const PortMapper* portMapper() const { return Mapper; }

    // client, after roomReceived: use this ROM (false if it isn't the room's)
    bool useROM(const QStringList& romPath, QString& error);

    // the room's game among the scanned games, if there is one
    QStringList findROM(const RoomInfo& room);

    void startGame();
    void leave();

    Session* session() { return &Sess; }
    bool playing() const { return ConsolesUp; }

signals:
    void finished(const QString& reason);   // the session is over (reason empty: left on purpose)
    void needROM(const Netplay::RoomInfo& room);
    void addressChanged();                  // host: the public address or forwarding changed
    void joinAsked(const QString& userId, const QString& name);  // a Discord friend asks to join

private slots:
    void onStarted();
    void onGo();
    void onEnded(const QString& reason);
    void onDesync(int frame, int console);

private:
    bool readGame(const QStringList& romPath, std::vector<u8>& rom, RoomInfo& room, QString& error);
    ConsoleInfo myConsoleInfo(const QString& name);
    std::vector<u8> mySave(const QStringList& romPath);
    void tearDown();
    void updatePresence();
    void onAddressChanged();
    QString buildJoinSecret() const;

    static Runner* Current;

    MainWindow* Win;
    EmuInstance* Inst;
    Session Sess;
    QStringList ROMPath;
    bool ConsolesUp = false;
    bool TornDown = false;
    QString MyName;
    PortMapper* Mapper = nullptr;
    QString PartyId;
};

}

#endif // NETPLAYRUNNER_H
