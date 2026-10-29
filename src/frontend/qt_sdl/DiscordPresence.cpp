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

#include "DiscordPresence.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QPointer>
#include <QProcess>
#include <QSettings>
#include <QStringList>
#include <QUuid>
#include <QtEndian>

#include "Config.h"
#include "GameList/Covers.h"
#include "GameList/GameList.h"
#include "RetroAchievements/RAClient.h"
#include "Updater/Updater.h"

namespace Discord
{

namespace
{

// the "melonRA" application on Discord's developer portal
const char* const kApplicationId = "1565031795559501885";
const char* const kLogoUrl = "https://raw.githubusercontent.com/ppkkkkk/melonRA/master/res/melon512.png";

enum Opcode
{
    Op_Handshake = 0,
    Op_Frame = 1,
    Op_Close = 2,
    Op_Ping = 3,
    Op_Pong = 4,
};

constexpr int kRefreshMs = 15000;     // Rich Presence text changes as you play
constexpr int kReconnectMs = 20000;   // Discord not running (yet)
constexpr int kMinSendGapMs = 5000;   // Discord allows 5 updates per 20 s
constexpr int kMaxFrame = 64 * 1024;

// Discord wants 2 to 128 characters
QString fit(QString s, int max = 128)
{
    s = s.simplified();
    if (s.size() > max) s = s.left(max - 1) + QChar(0x2026);
    if (s.size() == 1) s += ' ';
    return s;
}

QStringList socketNames()
{
    QStringList names;
#ifdef _WIN32
    for (int i = 0; i < 10; i++)
        names << QString("discord-ipc-%1").arg(i);
#else
    // where Discord (and its Flatpak and Snap versions) put the socket
    QStringList dirs;
    for (const char* var : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"})
    {
        const QString d = qEnvironmentVariable(var);
        if (!d.isEmpty() && !dirs.contains(d)) dirs << d;
    }
    if (!dirs.contains("/tmp")) dirs << "/tmp";
    const QStringList subdirs = {"", "app/com.discordapp.Discord/", "snap.discord/",
                                 ".flatpak/dev.vencord.Vesktop/xdg-run/"};
    for (const QString& d : dirs)
        for (const QString& sub : subdirs)
            for (int i = 0; i < 10; i++)
                names << QDir(d).filePath(sub + QString("discord-ipc-%1").arg(i));
#endif
    return names;
}

}

Presence* Presence::instance = nullptr;

void Presence::create()
{
    if (!instance) instance = new Presence();
}

void Presence::destroy()
{
    delete instance;
    instance = nullptr;
}

bool Presence::isEnabled()
{
    return Config::GetGlobalTable().GetBool("Discord.Enabled");
}

bool Presence::showsAchievements()
{
    return Config::GetGlobalTable().GetBool("Discord.ShowAchievements");
}

bool Presence::showsAchievementCount()
{
    return Config::GetGlobalTable().GetBool("Discord.ShowAchievementCount");
}

bool Presence::showsEmulatorIcon()
{
    return Config::GetGlobalTable().GetBool("Discord.ShowEmulatorIcon");
}

Presence::Presence()
{
    reconnectTimer.setInterval(kReconnectMs);
    connect(&reconnectTimer, &QTimer::timeout, this, &Presence::connectToDiscord);

    refreshTimer.setInterval(kRefreshMs);
    connect(&refreshTimer, &QTimer::timeout, this, &Presence::refresh);

    sendTimer.setSingleShot(true);
    connect(&sendTimer, &QTimer::timeout, this, &Presence::sendActivity);

    if (RA::Manager* ra = RA::Manager::get())
        connect(ra, &RA::Manager::stateChanged, this, &Presence::refresh);

    if (isEnabled())
        registerLaunchCommand();

    reloadSettings();
}

Presence::~Presence()
{
    disconnectFromDiscord();
}

void Presence::reloadSettings()
{
    if (isEnabled())
    {
        registerLaunchCommand();
        if (!socket) connectToDiscord();
        if (playing) lookForCover();
        refresh();
    }
    else
    {
        // take it off the profile right away
        if (ready && !lastWasCleared)
        {
            lastActivity = {};
            sendFrame(Op_Frame, QJsonObject{
                {"cmd", "SET_ACTIVITY"},
                {"args", QJsonObject{{"pid", (qint64)QCoreApplication::applicationPid()}}},
                {"nonce", QString::number(++nonce)},
            });
            if (socket) socket->flush();
        }
        disconnectFromDiscord();
        reconnectTimer.stop();
    }
}

void Presence::gameStarted(const QString& newTitle, const QString& newSubtitle, const QString& newCode)
{
    if (newTitle.isEmpty())
    {
        gameStopped();
        return;
    }

    const bool sameGame = playing && newTitle == title && newCode == gameCode;
    if (!sameGame)
    {
        startTime = QDateTime::currentSecsSinceEpoch();
        coverUrl.clear();
        coverLooked = false;
    }

    playing = true;
    title = newTitle;
    subtitle = newSubtitle;
    gameCode = newCode;
    if (isEnabled()) lookForCover();
    refreshTimer.start();
    refresh();
}

void Presence::lookForCover()
{
    if (coverLooked) return;
    coverLooked = true;

    GameList::Game game;
    game.title = title;
    game.subtitle = subtitle;
    game.gameCode = gameCode;
    const QString t = title, c = gameCode;
    QPointer<Presence> self(this);
    GameList::Covers::get()->onlineCover(game, [self, t, c](const QString& url)
    {
        if (!self || !self->playing || self->title != t || self->gameCode != c) return;
        self->coverUrl = url;
        self->refresh();
    });
}

void Presence::gameStopped()
{
    playing = false;
    title.clear();
    subtitle.clear();
    gameCode.clear();
    coverUrl.clear();
    refreshTimer.stop();
    refresh();
}

QJsonObject Presence::buildActivity() const
{
    // in a netplay room before the game starts: the room itself
    if (!playing && inRoom)
    {
        QJsonObject activity{
            {"type", 0},
            {"status_display_type", 2},
            {"details", fit(room.Title)},
            {"state", "In a room"},
            {"timestamps", QJsonObject{{"start", roomSince}}},
            {"assets", QJsonObject{{"large_image", kLogoUrl}, {"large_text", fit(room.Title)}}},
            {"party", QJsonObject{{"id", room.PartyId}, {"size", QJsonArray{room.Players, room.MaxPlayers}}}},
        };
        if (!room.JoinSecret.isEmpty())
            activity["secrets"] = QJsonObject{{"join", room.JoinSecret}};
        return activity;
    }

    if (!playing) return {};

    QJsonObject activity;
    QJsonObject assets;
    QString details = title;
    QString state = subtitle;
    QString largeImage = coverUrl.isEmpty() ? kLogoUrl : coverUrl;
    QString largeText = subtitle.isEmpty() ? title : title + " " + subtitle;
    QString smallText = Updater::windowTitle();

    RA::Manager* ra = RA::Manager::get();
    if (ra && ra->isEnabled() && ra->isGameLoaded() && showsAchievements())
    {
        const QString raTitle = ra->gameTitle();
        if (!raTitle.isEmpty()) details = raTitle;

        state = ra->richPresence();

        const QString badge = QString::fromStdString(ra->gameBadgeUrl());
        if (badge.startsWith("https://"))
            largeImage = badge;
        largeText = details;

        // "Super Mario 64 DS (12/157 🏆)", or with a star once mastered
        const QString count = ra->achievementCount();
        if (!count.isEmpty() && showsAchievementCount())
        {
            const char* emoji = ra->isMastered() ? "\xE2\xAD\x90" : "\xF0\x9F\x8F\x86";
            details += " (" + count + " " + QString::fromUtf8(emoji) + ")";
        }

        smallText = ra->hardcoreStatusText() + " - " + Updater::windowTitle();

        if (const uint32_t id = ra->gameId())
        {
            activity["buttons"] = QJsonArray{QJsonObject{
                {"label", "View on RetroAchievements"},
                {"url", QString("https://retroachievements.org/game/%1").arg(id)},
            }};
        }
    }

    // netplay: "In a room (2 of 4)" under the title, as Dolphin does it; the
    // achievement status moves to the picture's hover text
    if (inRoom)
    {
        if (!state.trimmed().isEmpty() && state != subtitle)
            largeText += " - " + state;
        state = "In a room";
        activity["party"] = QJsonObject{
            {"id", room.PartyId},
            {"size", QJsonArray{room.Players, room.MaxPlayers}},
        };
        // Discord doesn't allow buttons next to a Join
        if (!room.JoinSecret.isEmpty())
        {
            activity.remove("buttons");
            activity["secrets"] = QJsonObject{{"join", room.JoinSecret}};
        }
    }

    activity["type"] = 0; // "Playing"
    // the members list says the game ("details") instead of "melonRA"
    activity["status_display_type"] = 2;
    activity["details"] = fit(details);
    if (!state.trimmed().isEmpty())
        activity["state"] = fit(state);
    activity["timestamps"] = QJsonObject{{"start", startTime}};

    assets["large_image"] = largeImage;
    assets["large_text"] = fit(largeText);
    if (largeImage != kLogoUrl && showsEmulatorIcon())
    {
        assets["small_image"] = kLogoUrl;
        assets["small_text"] = fit(smallText);
    }
    activity["assets"] = assets;
    return activity;
}

void Presence::setNetplayRoom(const Room& newRoom)
{
    if (!inRoom) roomSince = QDateTime::currentSecsSinceEpoch();
    inRoom = true;
    room = newRoom;
    room.Players = std::max(room.Players, 1);
    room.MaxPlayers = std::max(room.MaxPlayers, room.Players);
    refresh();
}

void Presence::clearNetplayRoom()
{
    if (!inRoom) return;
    inRoom = false;
    room = Room();
    refresh();
}

void Presence::answerJoinRequest(const QString& userId, bool accept)
{
    if (!ready || userId.isEmpty() || userId.size() > 32) return;
    sendFrame(Op_Frame, QJsonObject{
        {"cmd", accept ? "SEND_ACTIVITY_JOIN_INVITE" : "CLOSE_ACTIVITY_REQUEST"},
        {"args", QJsonObject{{"user_id", userId}}},
        {"nonce", QString::number(++nonce)},
    });
}

// Tells Discord how to start melonRA, for a friend who clicks Join while it's
// closed: the per-user registration Discord games make. melonRA is started
// with no arguments; Discord hands it the room once it's running.
void Presence::registerLaunchCommand()
{
    QString exe = QCoreApplication::applicationFilePath();
#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    // an AppImage runs from a temporary mount; the file itself is what to start
    const QString appimage = qEnvironmentVariable("APPIMAGE");
    if (!appimage.isEmpty()) exe = appimage;
#endif
    if (exe.isEmpty()) return;
    const QString scheme = QString("discord-%1").arg(kApplicationId);

#ifdef _WIN32
    QSettings reg("HKEY_CURRENT_USER\\Software\\Classes\\" + scheme, QSettings::NativeFormat);
    const QString command = "\"" + QDir::toNativeSeparators(exe) + "\"";
    if (reg.value("shell/open/command/Default").toString() == command) return;
    reg.setValue("Default", "URL:Run game " + QString(kApplicationId) + " protocol");
    reg.setValue("URL Protocol", "");
    reg.setValue("DefaultIcon/Default", QDir::toNativeSeparators(exe));
    reg.setValue("shell/open/command/Default", command);
#elif defined(__APPLE__)
    const QString dir = QDir::homePath() + "/Library/Application Support/discord/games";
    const QString path = dir + "/" + kApplicationId + ".json";
    const QByteArray json = QJsonDocument(QJsonObject{{"command", exe}}).toJson(QJsonDocument::Compact);
    QFile current(path);
    if (current.open(QIODevice::ReadOnly) && current.readAll() == json) return;
    current.close();
    QDir().mkpath(dir);
    QFile out(path);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) out.write(json);
#else
    const QString dir = QDir::homePath() + "/.local/share/applications";
    const QString file = scheme + ".desktop";
    QString quoted = exe;
    quoted.replace("\\", "\\\\").replace("\"", "\\\"");
    const QByteArray desktop = QString(
        "[Desktop Entry]\n"
        "Name=melonRA\n"
        "Exec=\"%1\"\n"
        "Type=Application\n"
        "NoDisplay=true\n"
        "Categories=Discord;Games;\n"
        "MimeType=x-scheme-handler/%2;\n").arg(quoted, scheme).toUtf8();
    QFile current(dir + "/" + file);
    if (current.open(QIODevice::ReadOnly) && current.readAll() == desktop) return;
    current.close();
    QDir().mkpath(dir);
    QFile out(dir + "/" + file);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    out.write(desktop);
    out.close();
    QProcess::startDetached("xdg-mime", {"default", file, "x-scheme-handler/" + scheme});
#endif
}

void Presence::subscribe()
{
    for (const char* evt : {"ACTIVITY_JOIN", "ACTIVITY_JOIN_REQUEST"})
    {
        sendFrame(Op_Frame, QJsonObject{
            {"cmd", "SUBSCRIBE"},
            {"evt", evt},
            {"nonce", QString::number(++nonce)},
        });
    }
}

void Presence::refresh()
{
    if (!isEnabled()) return;

    const QJsonObject activity = buildActivity();
    const bool clear = activity.isEmpty();
    if (clear ? lastWasCleared : activity == lastActivity)
        return;

    if (!ready || sendTimer.isActive()) return; // sent when connected / when the timer fires

    const qint64 since = QDateTime::currentMSecsSinceEpoch() - lastSend;
    if (since < kMinSendGapMs)
        sendTimer.start(int(kMinSendGapMs - since));
    else
        sendActivity();
}

void Presence::sendActivity()
{
    if (!ready || !isEnabled()) return;

    const QJsonObject activity = buildActivity();
    QJsonObject args{{"pid", (qint64)QCoreApplication::applicationPid()}};
    if (!activity.isEmpty())
        args["activity"] = activity;

    sendFrame(Op_Frame, QJsonObject{
        {"cmd", "SET_ACTIVITY"},
        {"args", args},
        {"nonce", QString::number(++nonce)},
    });

    lastActivity = activity;
    lastWasCleared = activity.isEmpty();
    lastSend = QDateTime::currentMSecsSinceEpoch();
}

void Presence::connectToDiscord()
{
    if (!isEnabled() || socket) return;

    for (const QString& name : socketNames())
    {
#ifndef _WIN32
        if (!QFile::exists(name)) continue;
#endif
        auto* s = new QLocalSocket(this);
        s->connectToServer(name);
        if (s->waitForConnected(200))
        {
            socket = s;
            break;
        }
        delete s;
    }

    if (!socket)
    {
        // not running; look again in a while
        if (!reconnectTimer.isActive()) reconnectTimer.start();
        return;
    }

    reconnectTimer.stop();
    connect(socket, &QLocalSocket::readyRead, this, &Presence::onReadyRead);
    connect(socket, &QLocalSocket::disconnected, this, [this]()
    {
        // Discord closed; wait for it to come back
        disconnectFromDiscord();
        if (isEnabled()) reconnectTimer.start();
    });

    sendFrame(Op_Handshake, QJsonObject{{"v", 1}, {"client_id", kApplicationId}});
}

void Presence::disconnectFromDiscord()
{
    const bool was = ready;
    ready = false;
    if (was) emit connectedChanged();
    incoming.clear();
    lastActivity = {};
    lastWasCleared = true;
    sendTimer.stop();
    if (!socket) return;

    QLocalSocket* s = socket;
    socket = nullptr;
    s->disconnect(this);
    s->abort();
    s->deleteLater();
}

void Presence::sendFrame(int op, const QJsonObject& payload)
{
    if (!socket) return;
    const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QByteArray frame(8, 0);
    qToLittleEndian<quint32>((quint32)op, frame.data());
    qToLittleEndian<quint32>((quint32)json.size(), frame.data() + 4);
    frame += json;
    socket->write(frame);
}

void Presence::onReadyRead()
{
    if (!socket) return;
    incoming += socket->readAll();

    while (incoming.size() >= 8)
    {
        const quint32 op = qFromLittleEndian<quint32>(incoming.constData());
        const quint32 len = qFromLittleEndian<quint32>(incoming.constData() + 4);
        if (len > (quint32)kMaxFrame)
        {
            disconnectFromDiscord();
            reconnectTimer.start();
            return;
        }
        if ((quint32)incoming.size() < 8 + len) return;

        const QByteArray body = incoming.mid(8, len);
        incoming.remove(0, 8 + len);
        const QJsonObject msg = QJsonDocument::fromJson(body).object();

        switch (op)
        {
        case Op_Frame:
        {
            const QString evt = msg.value("evt").toString();
            const QJsonObject data = msg.value("data").toObject();
            if (evt == "READY")
            {
                ready = true;
                subscribe();
                refresh();
                emit connectedChanged();
            }
            else if (evt == "ACTIVITY_JOIN")
            {
                // this player clicked Join, or accepted an invite
                const QString secret = data.value("secret").toString();
                if (!secret.isEmpty() && secret.size() <= 128)
                    emit joinRequested(secret);
            }
            else if (evt == "ACTIVITY_JOIN_REQUEST")
            {
                const QJsonObject user = data.value("user").toObject();
                const QString id = user.value("id").toString();
                QString name = user.value("global_name").toString();
                if (name.isEmpty()) name = user.value("username").toString();

                // the name is only shown: no line breaks, direction marks or other invisible characters
                QString shown;
                for (QChar c : name)
                {
                    const QChar::Category cat = c.category();
                    if (cat == QChar::Other_Control || cat == QChar::Other_Format ||
                        cat == QChar::Other_Surrogate || cat == QChar::Other_PrivateUse ||
                        cat == QChar::Other_NotAssigned || cat == QChar::Separator_Line ||
                        cat == QChar::Separator_Paragraph)
                        continue;
                    shown += c;
                }
                shown = shown.simplified().left(32);
                if (shown.isEmpty()) shown = tr("A Discord friend");

                bool digits = !id.isEmpty() && id.size() <= 32;
                for (QChar c : id) digits = digits && c.isDigit() && c.unicode() < 0x80;
                if (digits)
                    emit joinAsked(id, shown);
            }
            break;
        }
        case Op_Ping:
            sendFrame(Op_Pong, msg);
            break;
        case Op_Close:
            disconnectFromDiscord();
            reconnectTimer.start();
            return;
        default:
            break;
        }
    }
}

}
