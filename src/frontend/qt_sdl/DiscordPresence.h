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

#ifndef DISCORDPRESENCE_H
#define DISCORDPRESENCE_H

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>

class QLocalSocket;

namespace Discord
{

// Shows the game being played on the player's Discord profile ("Playing
// melonRA"), with how long it's been played. For games with achievements,
// what RetroAchievements says about the game (its Rich Presence) and the
// game's icon are shown instead.
//
// Talks to the Discord app running on this computer through its local
// socket; nothing is sent anywhere else. Does nothing if Discord isn't running.
class Presence : public QObject
{
    Q_OBJECT
public:
    static void create();
    static void destroy();
    static Presence* get() { return instance; }

    static bool isEnabled();          // Interface Settings
    static bool showsAchievements();  // RetroAchievements status instead of just the game
    static bool showsAchievementCount(); // "(12/50 🏆)" after the title
    static bool showsEmulatorIcon();  // the small melonRA icon on the game's image

    // the settings changed
    void reloadSettings();

    // A game started or stopped running in the main window. A game that
    // starts again (reset) keeps counting from when it first started.
    void gameStarted(const QString& title, const QString& subtitle, const QString& gameCode);
    void gameStopped();

    // something shown may have changed (achievements game loaded, ...)
    void refresh();

    // A netplay room: shown as "In a room (2 of 4)" under the title, also
    // before the game starts. With a join secret, Discord offers Join to
    // friends (and "Invite to play" in chats); joining hands the secret to
    // the friend's melonRA (joinRequested).
    struct Room
    {
        QString Title;
        int Players = 0;
        int MaxPlayers = 0;
        QString PartyId;
        QString JoinSecret;   // empty: no Join (game started, room full)
    };
    void setNetplayRoom(const Room& room);
    void clearNetplayRoom();

    // talking to the Discord app right now
    bool isConnected() const { return ready; }

signals:
    // this player chose Join on a friend's room (or accepted an invite)
    void joinRequested(const QString& joinSecret);
    // Discord was found or lost
    void connectedChanged();

public:

private:
    Presence();
    ~Presence() override;

    void connectToDiscord();
    void disconnectFromDiscord();
    void onReadyRead();
    void sendFrame(int op, const QJsonObject& payload);
    QJsonObject buildActivity() const;
    void sendActivity();
    void lookForCover();

    static Presence* instance;

    QLocalSocket* socket = nullptr;
    bool ready = false;          // handshake done
    QByteArray incoming;
    QTimer reconnectTimer;
    QTimer refreshTimer;
    QTimer sendTimer;            // keeps updates within Discord's rate limit
    qint64 lastSend = 0;
    QJsonObject lastActivity;
    bool lastWasCleared = true;
    int nonce = 0;

    bool playing = false;
    QString title;
    QString subtitle;
    QString gameCode;
    QString coverUrl;
    bool coverLooked = false;
    qint64 startTime = 0;        // seconds since the epoch

    bool inRoom = false;
    Room room;
    qint64 roomSince = 0;

    void subscribe();
    void declineJoinRequest(const QString& userId);
    void registerLaunchCommand();
};

}

#endif // DISCORDPRESENCE_H
