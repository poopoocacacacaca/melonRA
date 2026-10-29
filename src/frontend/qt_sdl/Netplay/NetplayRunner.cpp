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

#include <QCryptographicHash>
#include <QDateTime>
#include <QUuid>
#include <QFile>
#include <QHostAddress>

#include "NetplayRunner.h"
#include "NetplayConsole.h"
#include "main.h"
#include "EmuInstance.h"
#include "Window.h"
#include "Config.h"
#include "LockstepMP.h"
#include "GameList/GameList.h"
#include "DiscordPresence.h"
#include "RetroAchievements/RAClient.h"

using namespace melonDS;

namespace Netplay
{

Runner* Runner::Current = nullptr;

QString Runner::cantStart(EmuInstance* inst)
{
    if (Current)
        return tr("A netplay session is already open.");
    if (numEmuInstances() > 1)
        return tr("Close the other melonRA windows (local multiplayer) before starting netplay.");
    if (RA::Manager* ra = RA::Manager::get(); ra && ra->hardcoreActive())
        return tr("Netplay doesn't work in Hardcore mode: RetroAchievements hasn't said yet whether "
                  "it's allowed. To use it, switch to Casual mode in RetroAchievements Settings.");
    (void)inst;
    return QString();
}

Runner::Runner(MainWindow* win) : QObject(win), Win(win), Inst(win->getEmuInstance())
{
    Current = this;

    connect(&Sess, &Session::started, this, &Runner::onStarted);
    connect(&Sess, &Session::go, this, &Runner::onGo);
    connect(&Sess, &Session::ended, this, &Runner::onEnded);
    connect(&Sess, &Session::desync, this, &Runner::onDesync);
    connect(&Sess, &Session::roomReceived, this, &Runner::needROM);
    connect(&Sess, &Session::rosterChanged, this, &Runner::updatePresence);
    connect(&Sess, &Session::inviteChanged, this, &Runner::updatePresence);
    connect(&Sess, &Session::started, this, &Runner::updatePresence);

    // friends asking to join on Discord: only the host decides, and only
    // while the room is open
    if (Discord::Presence* presence = Discord::Presence::get())
    {
        connect(presence, &Discord::Presence::joinAsked, this, [this](const QString& id, const QString& name)
        {
            Discord::Presence* p = Discord::Presence::get();
            if (Sess.isHost() && Sess.state() == Session::State::Lobby && Sess.players() < kMaxPlayers)
                emit joinAsked(id, name);
            else if (p)
                p->answerJoinRequest(id, false);
        });
    }

    // while playing, the room is out of sight: chat and delay changes show
    // up on the game screen
    connect(&Sess, &Session::chat, this, [this](const QString& name, const QString& text)
    {
        if (ConsolesUp)
            Inst->osdAddMessage(0, "%s: %s", name.toUtf8().constData(), text.toUtf8().constData());
    });
    connect(&Sess, &Session::delayChanged, this, [this](int frames)
    {
        if (ConsolesUp)
            Inst->osdAddMessage(0, "Netplay input delay: %d frames", frames);
    });
}

Runner::~Runner()
{
    // normally the session is over by now; if the window is being destroyed
    // with it still open, at least stop waiting on the network
    if (!TornDown)
    {
        Sess.leave();
        if (MPInterface::GetType() == MPInterface_Netplay)
            static_cast<LockstepMP&>(MPInterface::Get()).Shutdown();
    }
    if (Current == this) Current = nullptr;
}

// --- the game ---

bool Runner::readGame(const QStringList& romPath, std::vector<u8>& rom, RoomInfo& room, QString& error)
{
    std::string romname;
    if (!Inst->readROMFile(romPath, rom, romname) || rom.size() < 0x200)
    {
        error = tr("Couldn't read the ROM.");
        return false;
    }

    QByteArray sha = QCryptographicHash::hash(
        QByteArray::fromRawData((const char*)rom.data(), (qsizetype)rom.size()), QCryptographicHash::Sha1);
    memcpy(room.ROMHash.data(), sha.constData(), 20);
    room.ROMSize = (u32)rom.size();

    GameList::Game game;
    if (GameList::readGame(QByteArray::fromRawData((const char*)rom.data(), (qsizetype)rom.size()),
                           QString::fromStdString(romname), game))
    {
        room.GameCode = game.gameCode.toStdString();
        room.Title = game.title.toStdString();
    }
    if (room.Title.empty()) room.Title = romname;

    // the game code goes over the network as-is; keep it to what it should be
    bool codeOk = room.GameCode.size() == 4;
    for (char c : room.GameCode) codeOk = codeOk && isalnum((unsigned char)c);
    if (!codeOk) room.GameCode.clear();
    return true;
}

ConsoleInfo Runner::myConsoleInfo(const QString& name)
{
    ConsoleInfo c;
    Config::Table& cfg = Inst->getLocalConfig();

    QString nick = name;
    if (cfg.GetBool("Firmware.OverrideSettings"))
    {
        QString user = cfg.GetQString("Firmware.Username");
        if (!user.isEmpty()) nick = user;

        int lang = cfg.GetInt("Firmware.Language");
        if (lang >= 0 && lang <= 7) c.Language = (u8)lang;
        int color = cfg.GetInt("Firmware.FavouriteColour");
        if (color >= 0 && color <= 15) c.FavoriteColor = (u8)color;
        int month = cfg.GetInt("Firmware.BirthdayMonth");
        int day = cfg.GetInt("Firmware.BirthdayDay");
        if (month >= 1 && month <= 12) c.BirthdayMonth = (u8)month;
        if (day >= 1 && day <= 31) c.BirthdayDay = (u8)day;

        c.Message = CleanDSText(cfg.GetQString("Firmware.Message").toStdU16String(), kMaxMessage);
    }

    c.Nickname = CleanDSText(nick.toStdU16String(), kMaxNickname);
    if (c.Nickname.empty()) c.Nickname = u"Player";
    return c;
}

std::vector<u8> Runner::mySave(const QStringList& romPath)
{
    QFile f(QString::fromStdString(Inst->saveFileFor(romPath)));
    if (!f.open(QIODevice::ReadOnly) || f.size() > kMaxSave) return {};
    QByteArray data = f.readAll();
    return std::vector<u8>(data.begin(), data.end());
}

QStringList Runner::findROM(const RoomInfo& room)
{
    if (room.GameCode.empty()) return {};
    const QString code = QString::fromStdString(room.GameCode);

    for (const GameList::Game& g : GameList::scannedGames())
    {
        if (g.notRom || g.gameCode != code || (u64)g.size != room.ROMSize) continue;

        QStringList path = Win->splitArchivePath(g.path, true);
        std::vector<u8> rom;
        RoomInfo info;
        QString err;
        if (readGame(path, rom, info, err) && info.ROMHash == room.ROMHash)
            return path;
    }
    return {};
}

// --- hosting and joining ---

bool Runner::host(const QStringList& romPath, const QString& name, u16 port, int inputDelay,
                  bool openPort, QString& error)
{
    std::vector<u8> rom;
    RoomInfo room;
    if (!readGame(romPath, rom, room, error)) return false;
    room.InputDelay = (u8)std::clamp(inputDelay, 1, kMaxInputDelay);

    std::vector<u8> save = mySave(romPath);
    MyName = name;
    ROMPath = romPath;

    // the consoles' clock starts at the host's current date and time
    QDateTime now = QDateTime::currentDateTime();
    Sess.setStartTime(now.date().year(), now.date().month(), now.date().day(),
                      now.time().hour(), now.time().minute(), now.time().second());

    // the JIT is part of what has to match; everyone uses the host's choice
    Config::Table& g = Inst->getGlobalConfig();
    Sess.setJIT(g.GetBool("JIT.Enable"), g.GetInt("JIT.MaxBlockSize"),
                g.GetBool("JIT.LiteralOptimisations"), g.GetBool("JIT.BranchOptimisations"),
                g.GetBool("JIT.FastMemory"));

    if (!Sess.host(port, name, room, myConsoleInfo(name), std::move(save), error))
        return false;

    // players on the internet: open the port on the router and find the
    // public address; both go into the Discord invite
    PartyId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    Mapper = new PortMapper(this);
    connect(Mapper, &PortMapper::changed, this, &Runner::onAddressChanged);
    Mapper->start(port, openPort);
    onAddressChanged();
    return true;
}

bool Runner::join(const QString& address, u16 port, const QString& name, QString& error)
{
    MyName = name;
    if (!Sess.join(address, port, name, error)) return false;
    updatePresence();
    return true;
}

// "mr1|<public address>:<port>|<home network address>:<port>", either part
// may be empty; nothing but IPv4 addresses and ports is accepted
bool Runner::parseJoinSecret(const QString& secret, std::vector<melonDS::NetplayLink::Target>& targets)
{
    std::vector<melonDS::NetplayLink::Target> found;
    targets.clear();
    if (secret.size() > kMaxJoinSecret) return false;
    const QStringList parts = secret.split('|');
    if (parts.size() != 3 || parts[0] != "mr1") return false;

    for (int i = 1; i < 3; i++)
    {
        if (parts[i].isEmpty()) continue;
        const int colon = parts[i].lastIndexOf(':');
        if (colon <= 0) return false;
        QHostAddress addr;
        if (!addr.setAddress(parts[i].left(colon)) || addr.protocol() != QAbstractSocket::IPv4Protocol)
            return false;
        const QString digits = parts[i].mid(colon + 1);
        if (digits.isEmpty() || digits.size() > 5) return false;
        for (QChar c : digits)
            if (c < '0' || c > '9') return false;
        const uint port = digits.toUInt();
        if (port == 0 || port > 65535) return false;
        found.push_back({addr.toString().toStdString(), (u16)port});
    }
    targets = std::move(found);
    return !targets.empty();
}

bool Runner::joinFromDiscord(const QString& secret, const QString& name, QString& error)
{
    std::vector<melonDS::NetplayLink::Target> targets;
    if (!parseJoinSecret(secret, targets))
    {
        error = tr("That Discord invite isn't a melonRA netplay room.");
        return false;
    }
    MyName = name;
    if (!Sess.join(targets, name, error)) return false;
    updatePresence();
    return true;
}

QString Runner::buildJoinSecret() const
{
    if (!Mapper) return QString();
    QString pub;
    if (!Mapper->publicAddress().isEmpty() && !Mapper->behindSharedAddress())
        pub = QString("%1:%2").arg(Mapper->publicAddress()).arg(Mapper->publicPort());
    QString local;
    if (!Mapper->localAddress().isEmpty())
        local = QString("%1:%2").arg(Mapper->localAddress()).arg(Sess.port());
    if (pub.isEmpty() && local.isEmpty()) return QString();
    return QString("mr1|%1|%2").arg(pub, local);
}

void Runner::onAddressChanged()
{
    Sess.setInvite(PartyId, buildJoinSecret());
    updatePresence();
    emit addressChanged();
}

// what Discord shows: the room, with Join while there's space and the game
// hasn't started
void Runner::updatePresence()
{
    Discord::Presence* presence = Discord::Presence::get();
    if (!presence || TornDown) return;

    const QString party = Sess.isHost() ? PartyId : Sess.partyId();
    if (party.isEmpty())
    {
        presence->clearNetplayRoom();
        return;
    }

    Discord::Presence::Room room;
    room.Title = QString::fromStdString(Sess.room().Title);
    room.Players = std::max(Sess.players(), 1);
    room.MaxPlayers = kMaxPlayers;
    room.PartyId = party;
    if (Sess.state() == Session::State::Lobby && room.Players < room.MaxPlayers)
        room.JoinSecret = Sess.isHost() ? buildJoinSecret() : Sess.joinSecret();
    presence->setNetplayRoom(room);
}

bool Runner::useROM(const QStringList& romPath, QString& error)
{
    std::vector<u8> rom;
    RoomInfo info;
    if (!readGame(romPath, rom, info, error)) return false;

    if (info.ROMHash != Sess.room().ROMHash)
    {
        error = tr("That isn't the same ROM as the host's. Netplay needs an identical copy of the game "
                   "(same version and region, unpatched).");
        return false;
    }

    ROMPath = romPath;
    Sess.provideROM(info.ROMHash, myConsoleInfo(MyName), mySave(romPath));
    return true;
}

void Runner::startGame()
{
    Sess.start();
}

void Runner::leave()
{
    Sess.leave();
    tearDown();
    emit finished(QString());
}

// --- the consoles ---

void Runner::onStarted()
{
    const std::vector<int> order = Sess.order();
    const int n = (int)order.size();
    const int mine = Sess.myConsole();
    const StartInfo st = Sess.startInfo();

    auto fail = [&](const QString& why)
    {
        Sess.leave();
        tearDown();
        emit finished(why);
    };

    if (n < 2 || mine < 0 || ROMPath.isEmpty())
    {
        fail(tr("The session couldn't start."));
        return;
    }

    // whatever was running stops; the session's game starts fresh
    EmuThread* thread0 = Inst->getEmuThread();
    if (thread0->emuIsActive())
        thread0->emuStop(true);
    Config::SetActiveGame("");

    // settings that have to be the same on every computer, for every console
    QVariantMap ov;
    ov["Emu.ConsoleType"] = 0;
    ov["Emu.ExternalBIOSEnable"] = false;
    ov["Emu.DirectBoot"] = true;
    ov["3D.Renderer"] = 0;
    ov["Screen.UseGL"] = false;
    ov["JIT.Enable"] = st.JIT;
    ov["JIT.MaxBlockSize"] = (int)st.JITBlockSize;
    ov["JIT.LiteralOptimisations"] = st.JITLiteral;
    ov["JIT.BranchOptimisations"] = st.JITBranch;
    ov["JIT.FastMemory"] = st.JITFastMem;
    ov["Audio.Interpolation"] = 0;
    ov["Audio.BitDepth"] = 0;
    ov["Mic.InputType"] = (int)micInputType_Silence;
    ov["LimitFPS"] = true;
    ov["KeepRunningInSettings"] = true;
    ov["PauseLostFocus"] = false;

    // instance k runs console (mine + k) % n: this player's is always the
    // first instance, in the main window
    for (int k = 0; k < n; k++)
    {
        int c = (mine + k) % n;
        ConsoleInfo info = Sess.consoleOf(c);
        const QString p = QString("Instance%1.").arg(k);
        ov[p + "Firmware.OverrideSettings"] = true;
        ov[p + "Firmware.Username"] = QString::fromStdU16String(info.Nickname);
        ov[p + "Firmware.Message"] = QString::fromStdU16String(info.Message);
        ov[p + "Firmware.Language"] = (int)info.Language;
        ov[p + "Firmware.FavouriteColour"] = (int)info.FavoriteColor;
        ov[p + "Firmware.BirthdayMonth"] = (int)info.BirthdayMonth;
        ov[p + "Firmware.BirthdayDay"] = (int)info.BirthdayDay;
        ov[p + "Firmware.MAC"] = QString::fromStdString(ConsoleMAC(c));
        ov[p + "EnableCheats"] = false;
        ov[p + "Gdb.Enabled"] = false;
        ov[p + "RTC.SyncToHost"] = false;
        ov[p + "DS.Battery.LevelOkay"] = true;
    }

    const bool glBefore = Inst->usesOpenGL();
    Config::SetSessionOverrides(ov);
    Win->onGameSettingsApplied(glBefore != Inst->usesOpenGL());

    // the consoles' local wireless runs on emulated time
    setMPInterface(MPInterface_Netplay);
    auto byConsole = std::make_shared<std::vector<EmuInstance*>>(n, nullptr);
    static_cast<LockstepMP&>(MPInterface::Get()).Configure((u16)((1 << n) - 1),
        [byConsole](int c) -> u64
        {
            EmuInstance* inst = (c >= 0 && c < (int)byConsole->size()) ? (*byConsole)[c] : nullptr;
            NDS* nds = inst ? inst->getNDS() : nullptr;
            return nds ? nds->GetSysTimestamp() : 0;
        });

    auto spec = [&](int c, bool local)
    {
        auto con = std::make_unique<Console>();
        con->session = &Sess;
        con->index = c;
        con->count = n;
        con->local = local;
        con->start = st;
        con->save = Sess.saveOf(c);
        if (local) con->savePath = Inst->saveFileFor(ROMPath);
        return con;
    };

    (*byConsole)[mine] = Inst;
    Inst->pendingNetplay = spec(mine, true);

    for (int k = 1; k < n; k++)
    {
        int c = (mine + k) % n;
        EmuInstance* inst = createNetplayInstance(spec(c, false));
        if (!inst || inst->getInstanceID() != k)
        {
            fail(tr("Couldn't set up the other players' consoles."));
            return;
        }
        (*byConsole)[c] = inst;
    }
    ConsolesUp = true;

    for (int k = 0; k < n; k++)
    {
        EmuInstance* inst = emuInstanceAt(k);
        QString err;
        if (!inst || !inst->getEmuThread()->netplayBoot(ROMPath, err))
        {
            fail(tr("Couldn't start the game: %1").arg(err));
            return;
        }
    }

    Sess.loaded();
}

void Runner::onGo()
{
    if (!ConsolesUp) return;

    for (int k = 0; k < kMaxPlayers; k++)
        if (EmuInstance* inst = emuInstanceAt(k))
            inst->getEmuThread()->emuRun();

    updatePresence();

    Inst->osdAddMessage(0, "Netplay started. Open the game's wireless multiplayer mode:");
    Inst->osdAddMessage(0, "one player creates a group, the others join it.");

    Win->onNetplayStarted();
}

void Runner::onEnded(const QString& reason)
{
    tearDown();
    emit finished(reason);
}

void Runner::onDesync(int frame, int console)
{
    QString who = Sess.nameOf(console);
    Inst->osdAddMessage(0xFFA040, "Netplay: the consoles stopped matching (%s's console)",
                        who.toUtf8().constData());
    (void)frame;
}

void Runner::tearDown()
{
    if (TornDown) return;
    TornDown = true;

    // nothing may wait on the network or the other consoles from here on
    Sess.leave();
    if (Mapper) Mapper->stop();
    if (MPInterface::GetType() == MPInterface_Netplay)
        static_cast<LockstepMP&>(MPInterface::Get()).Shutdown();

    if (ConsolesUp)
    {
        for (int k = kMaxPlayers - 1; k >= 1; k--)
        {
            if (EmuInstance* inst = emuInstanceAt(k); inst && inst->netplay)
                deleteEmuInstance(k);
        }

        EmuThread* thread0 = Inst->getEmuThread();
        if (thread0->emuIsActive())
            thread0->emuStop(true);
        ConsolesUp = false;
    }

    Inst->getEmuThread()->netplayEnd();

    if (Discord::Presence* presence = Discord::Presence::get())
        presence->clearNetplayRoom();

    if (MPInterface::GetType() == MPInterface_Netplay)
        setMPInterface(MPInterface_Local);

    if (Config::SessionActive())
    {
        const bool glBefore = Inst->usesOpenGL();
        Config::ClearSessionOverrides();
        Win->onGameSettingsApplied(glBefore != Inst->usesOpenGL());
    }

    Win->onNetplayEnded();
}

}
