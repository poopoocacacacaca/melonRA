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

#include "RAClient.h"
#include "RAHttp.h"
#include "RAImageCache.h"
#include "RALibrary.h"
#include "RAOverlay.h"
#include "RAPendingQueue.h"
#include "RAOfflineCache.h"

#include <cstring>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMessageBox>
#include <QMetaObject>
#include <QRandomGenerator>
#include <QSysInfo>

#include "rc_client.h"
#include "rc_client_raintegration.h"
#include "rc_consoles.h"
#include "rc_hash.h"

#include "Config.h"
#include "EmuInstance.h"
#include "MemConstants.h"
#include "NDS.h"
#include "Platform.h"
#include "version.h"

#include <QWidget>

using namespace melonDS;

namespace RA
{

Manager* Manager::instance = nullptr;

// RetroAchievements address space for DS/DSi (see rc_consoleinfo.c):
//   $0000000-$03FFFFF  main RAM (DS)
//   $0000000-$0FFFFFF  main RAM (DSi, padding on DS)
//   $1000000-$1003FFF  ARM9 data TCM
static constexpr uint32_t kRADTCMBase = 0x1000000;

static constexpr unsigned kColorInfo = 0xA0C8FF;
static constexpr unsigned kColorWarn = 0xFFC040;
static constexpr unsigned kColorError = 0xFFA0A0;

static Config::Table raConfig()
{
    return Config::GetGlobalTable().GetTable("RetroAchievements");
}

// Display options are read from the emulator thread while events are being
// processed, so keep a lock-free copy instead of touching the config there.
static std::atomic<bool> optPopups{true};
static std::atomic<bool> optLeaderboardPopups{true};
static std::atomic<bool> optChallenges{true};
static std::atomic<bool> optProgress{true};
static std::atomic<bool> optTrackers{true};
static std::atomic<bool> optSounds{true};
static std::atomic<bool> optAccountOnStart{true};
static std::atomic<bool> optOfflineSessions{true};
static std::atomic<bool> optEnabled{true};


struct ClientCallbacks
{
    static Manager* self(const rc_client_t* client)
    {
        return static_cast<Manager*>(rc_client_get_userdata(client));
    }

    static uint32_t RC_CCONV readMemory(uint32_t address, uint8_t* buffer, uint32_t numBytes, rc_client_t* client)
    {
        // Background reads are disabled, so this only ever runs from
        // rc_client_do_frame/rc_client_idle on the emulator thread, where the
        // NDS object can't be swapped out from under us.
        Manager* m = self(client);
        if (!m || !m->emuInstance) return 0;
        NDS* nds = m->emuInstance->getNDS();
        if (!nds || !nds->MainRAM) return 0;

        const uint32_t ramSize = nds->MainRAMMask + 1;
        for (uint32_t i = 0; i < numBytes; i++)
        {
            uint32_t addr = address + i;
            if (addr < ramSize)
                buffer[i] = nds->MainRAM[addr];
            else if (addr < kRADTCMBase)
                buffer[i] = 0; // DSi-only RAM while running in DS mode
            else if (addr < kRADTCMBase + DTCMPhysicalSize && nds->ARM9.DTCM)
                buffer[i] = nds->ARM9.DTCM[addr - kRADTCMBase];
            else
                return i;
        }
        return numBytes;
    }

    static void RC_CCONV serverCall(const rc_api_request_t* request, rc_client_server_callback_t callback,
                                    void* callbackData, rc_client_t* client)
    {
        Manager* m = self(client);
        std::string url = request->url ? request->url : "";
        std::string post = request->post_data ? request->post_data : "";
        std::string ctype = request->content_type ? request->content_type : "";

        m->send(url, post, ctype, [callback, callbackData](const HttpResponse& r)
        {
            rc_api_server_response_t resp;
            memset(&resp, 0, sizeof(resp));

            if (r.status == 0)
            {
                resp.body = r.error.c_str();
                resp.body_length = r.error.size();
                resp.http_status_code = r.cancelled ? RC_API_SERVER_RESPONSE_CLIENT_ERROR
                                                    : RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
            }
            else
            {
                resp.body = r.body.data();
                resp.body_length = r.body.size();
                resp.http_status_code = r.status;
            }

            callback(&resp, callbackData);
        });
    }

    static void RC_CCONV event(const rc_client_event_t* ev, rc_client_t* client)
    {
        if (Manager* m = self(client))
            m->handleEvent(ev);
    }

    static void RC_CCONV log(const char* message, const rc_client_t* client)
    {
        Platform::Log(Platform::LogLevel::Info, "[RetroAchievements] %s\n", message);
    }

    static void RC_CCONV loginDone(int result, const char* errorMessage, rc_client_t* client, void* userdata)
    {
        Manager* m = self(client);
        bool fromToken = userdata != nullptr;
        m->loginPending = false;

        QString msg;
        bool ok = (result == RC_OK);
        std::string username, token;

        if (ok)
        {
            const rc_client_user_t* user = rc_client_get_user_info(client);
            username = user->username;
            token = user->token;
            msg = QString("Logged in as %1 (%2 points, %3 casual)")
                      .arg(QString::fromUtf8(user->display_name))
                      .arg(user->score).arg(user->score_casual);
            m->osd(kColorInfo, QString("RetroAchievements: %1").arg(msg));
            if (user->num_unread_messages)
                m->osd(kColorInfo, QString("RetroAchievements: you have %1 unread message(s)")
                                       .arg(user->num_unread_messages));

            m->submitSavedQueue();
        }
        else
        {
            msg = QString::fromUtf8(errorMessage ? errorMessage : rc_error_str(result));
            m->osd(kColorError, QString("RetroAchievements login failed: %1").arg(msg));
        }

        bool clearToken = !ok && fromToken &&
                          (result == RC_INVALID_CREDENTIALS || result == RC_EXPIRED_TOKEN ||
                           result == RC_ACCESS_DENIED);

        // persist on the GUI thread, which owns the config
        QMetaObject::invokeMethod(m, [m, ok, clearToken, username, token]()
        {
            Config::Table cfg = raConfig();
            if (ok)
            {
                cfg.SetString("Username", username);
                cfg.SetString("Token", token);
                Config::Save();
            }
            else if (clearToken)
            {
                cfg.SetString("Token", "");
                Config::Save();
            }
        }, Qt::QueuedConnection);

        emit m->loginFinished(ok, msg);
        emit m->stateChanged();

        // A game that started before we were logged in couldn't be loaded.
        // Load it now; in hardcore this has to go through a reset.
        if (ok)
        {
            bool hasGame;
            {
                std::lock_guard<std::mutex> guard(m->hashLock);
                hasGame = m->gameLoadRequested && !m->loadedHash.empty();
            }
            int loadState = rc_client_get_load_game_state(client);
            bool idle = loadState == RC_CLIENT_LOAD_GAME_STATE_NONE || loadState == RC_CLIENT_LOAD_GAME_STATE_ABORTED;
            if (hasGame && idle && !rc_client_is_game_loaded(client))
            {
                if (rc_client_get_hardcore_enabled(client))
                    emit m->resetRequested();
                else
                    m->onROMStarted();
            }
        }
    }

#ifdef RC_CLIENT_SUPPORTS_RAINTEGRATION
    static void RC_CCONV writeMemory(uint32_t address, uint8_t* buffer, uint32_t numBytes, rc_client_t* client)
    {
        // used by the toolkit's memory inspector (casual mode only)
        Manager* m = self(client);
        if (!m || !m->emuInstance) return;
        NDS* nds = m->emuInstance->getNDS();
        if (!nds || !nds->MainRAM) return;

        const uint32_t ramSize = nds->MainRAMMask + 1;
        for (uint32_t i = 0; i < numBytes; i++)
        {
            uint32_t addr = address + i;
            if (addr < ramSize)
                nds->MainRAM[addr] = buffer[i];
            else if (addr >= kRADTCMBase && addr < kRADTCMBase + DTCMPhysicalSize && nds->ARM9.DTCM)
                nds->ARM9.DTCM[addr - kRADTCMBase] = buffer[i];
        }
    }

    static void RC_CCONV gameName(char* buffer, uint32_t size, rc_client_t* client)
    {
        Manager* m = self(client);
        std::string name;
        if (m)
        {
            std::lock_guard<std::mutex> guard(m->hashLock);
            name = m->romName;
        }
        snprintf(buffer, size, "%s", name.c_str());
    }

    static void RC_CCONV toolkitEvent(const rc_client_raintegration_event_t* ev, rc_client_t* client)
    {
        Manager* m = self(client);
        if (!m) return;

        switch (ev->type)
        {
        case RC_CLIENT_RAINTEGRATION_EVENT_MENUITEM_CHECKED_CHANGED:
        case RC_CLIENT_RAINTEGRATION_EVENT_MENU_CHANGED:
            emit m->toolkitMenuChanged();
            break;

        case RC_CLIENT_RAINTEGRATION_EVENT_HARDCORE_CHANGED:
        {
            // the toolkit toggled hardcore itself (e.g. memory inspector opened)
            bool hc = rc_client_get_hardcore_enabled(client) != 0;
            QMetaObject::invokeMethod(m, [m, hc]()
            {
                raConfig().SetBool("Hardcore", hc);
                Config::Save();
                m->reloadDisplayOptions();
                emit m->stateChanged();
            }, Qt::QueuedConnection);
            break;
        }

        case RC_CLIENT_RAINTEGRATION_EVENT_PAUSE:
            emit m->pauseRequested();
            break;
        }
    }

    static void RC_CCONV toolkitLoaded(int result, const char* errorMessage, rc_client_t* client, void* userdata)
    {
        Manager* m = self(client);
        QMetaObject::invokeMethod(m, [m, result, err = std::string(errorMessage ? errorMessage : "")]()
        {
            m->raIntegrationLoading = false;
            if (result == RC_OK)
            {
                m->raIntegrationLoaded = true;
                rc_client_raintegration_set_event_handler(m->rcClient, ClientCallbacks::toolkitEvent);
                rc_client_raintegration_set_write_memory_function(m->rcClient, ClientCallbacks::writeMemory);
                rc_client_raintegration_set_get_game_name_function(m->rcClient, ClientCallbacks::gameName);
                rc_client_raintegration_set_console_id(m->rcClient, RC_CONSOLE_NINTENDO_DS);
                m->osd(kColorInfo, "RetroAchievements: developer toolkit (RAIntegration) loaded");
                emit m->toolkitMenuChanged();
            }
            else
            {
                QString msg = QString::fromStdString(err.empty() ? rc_error_str(result) : err);
                m->osd(kColorError, QString("RetroAchievements: could not load RAIntegration: %1").arg(msg));
                // most likely an outdated DLL: offer to fetch the current one
                if (msg.contains("minimum version"))
                    m->offerRAIntegrationDownload(QString("Your RA_Integration DLL is out of date (%1).").arg(msg));
            }

            // continue with the normal startup login either way
            if (m->isEnabled())
                m->loginWithStoredToken();
            emit m->stateChanged();
        }, Qt::QueuedConnection);
    }
#endif

    static void RC_CCONV gameLoaded(int result, const char* errorMessage, rc_client_t* client, void* userdata)
    {
        Manager* m = self(client);

        if (result == RC_OK)
        {
            m->showGamePlacard();
        }
        else if (result == RC_NO_GAME_LOADED)
        {
            m->ovl->addPopup(Overlay::PopupKind::Info, "", "RetroAchievements",
                             "This game is not recognized.",
                             "No achievements are available for this ROM.", 5000);
        }
        else if (result == RC_LOGIN_REQUIRED)
        {
            // not logged in: nothing to report, the user hasn't set RA up
        }
        else if (result != RC_ABORTED)
        {
            QString err = QString::fromUtf8(errorMessage ? errorMessage : rc_error_str(result));
            m->ovl->addPopup(Overlay::PopupKind::Error, "", "RetroAchievements",
                             "Failed to load game data.", err, 6000);
        }

        emit m->stateChanged();
    }
};


void Manager::create()
{
    if (!instance) instance = new Manager();
}

void Manager::destroy()
{
    delete instance;
    instance = nullptr;
}

Manager::Manager()
{
    http = std::make_unique<Http>(3);

    imageCache = std::make_unique<ImageCache>(*http, Platform::GetLocalFilePath("retroachievements/cache"));
    ovl = std::make_unique<Overlay>(*imageCache);

    // per-installation key for the saved-submission integrity check
    {
        Config::Table cfg = raConfig();
        QByteArray key = QByteArray::fromHex(QByteArray::fromStdString(cfg.GetString("QueueKey")));
        if (key.size() < 32)
        {
            key.resize(32);
            for (int i = 0; i < 32; i += 4)
            {
                quint32 r = QRandomGenerator::system()->generate();
                memcpy(key.data() + i, &r, 4);
            }
            cfg.SetString("QueueKey", key.toHex().toStdString());
            Config::Save();
        }
        pending = std::make_unique<PendingQueue>(*http,
            Platform::GetLocalFilePath("retroachievements/pending_submissions.json"), key);
        offlineCache = std::make_unique<OfflineCache>(Platform::GetLocalFilePath("retroachievements/offline"));
    }

    imageCache->setLoadedCallback([this]()
    {
        ovl->invalidate();
        emit imagesUpdated();
    });

    rcClient = rc_client_create(ClientCallbacks::readMemory, ClientCallbacks::serverCall);
    rc_client_set_userdata(rcClient, this);
    rc_client_set_event_handler(rcClient, ClientCallbacks::event);
    rc_client_enable_logging(rcClient, RC_CLIENT_LOG_LEVEL_INFO, ClientCallbacks::log);
    // memory may only be read from the emulator thread
    rc_client_set_allow_background_memory_reads(rcClient, 0);

    http->setUserAgent(buildUserAgent());
    Platform::Log(Platform::LogLevel::Info, "[RetroAchievements] User-Agent: %s\n", buildUserAgent().c_str());

    applyClientSettings();
    reloadDisplayOptions();

    lib = std::make_unique<Library>(*this);

    // With RAIntegration enabled, logging in waits until the DLL has been
    // loaded (see initRAIntegration), since it takes over the server calls.
    if (isEnabled() && !isRAIntegrationEnabled())
        loginWithStoredToken();
}

Manager::~Manager()
{
    lib.reset();

    // Stop the network first: any request still queued completes with a
    // non-retryable error while rc_client is still alive.
    http->shutdown();

    if (rcClient)
    {
#ifdef RC_CLIENT_SUPPORTS_RAINTEGRATION
        if (raIntegrationLoaded)
            rc_client_unload_raintegration(rcClient);
#endif
        rc_client_destroy(rcClient);
        rcClient = nullptr;
    }
}

std::string Manager::buildUserAgent() const
{
    // <product>/<version> (<os>) <extensions>
    std::string os = QSysInfo::prettyProductName().toStdString();
    for (char& c : os)
        if (c == '(' || c == ')') c = ' ';

    std::string melon = MELONDS_VERSION;
    for (char& c : melon)
        if (c == ' ') c = '-';

    std::string ua = std::string(MELONDS_RA_PRODUCT "/" MELONDS_RA_VERSION " (") + os + ") melonDS/" + melon;

    // rcheevos/x.y clause
    char clause[64] = {0};
    if (rcClient && rc_client_get_user_agent_clause(rcClient, clause, sizeof(clause)) > 0)
        ua += std::string(" ") + clause;
    return ua;
}

void Manager::applyClientSettings()
{
    Config::Table cfg = raConfig();
    rc_client_set_hardcore_enabled(rcClient, cfg.GetBool("Hardcore") ? 1 : 0);
    rc_client_set_encore_mode_enabled(rcClient, cfg.GetBool("Encore") ? 1 : 0);
    rc_client_set_unpromoted_enabled(rcClient, cfg.GetBool("Unpromoted") ? 1 : 0);
    rc_client_set_spectator_mode_enabled(rcClient, cfg.GetBool("Spectator") ? 1 : 0);
}

void Manager::reloadDisplayOptions()
{
    Config::Table cfg = raConfig();
    optEnabled = cfg.GetBool("Enabled");
    optPopups = cfg.GetBool("Notifications");
    optLeaderboardPopups = cfg.GetBool("LeaderboardNotifications");
    optChallenges = cfg.GetBool("ChallengeIndicators");
    optProgress = cfg.GetBool("ProgressIndicators");
    optTrackers = cfg.GetBool("LeaderboardTrackers");
    optSounds = cfg.GetBool("Sounds");
    optAccountOnStart = cfg.GetBool("ShowAccountOnStart");
    optOfflineSessions = cfg.GetBool("OfflineSessions");

    // Leaderboards can't be turned off in hardcore; keep their trackers
    // visible there so players always see what is being submitted.
    bool trackers = optTrackers || (rcClient && rc_client_get_hardcore_enabled(rcClient));
    ovl->setOptions(optPopups, optChallenges, optProgress, trackers);
    ovl->setStyle(loadOverlayStyle());
}

static const char* const kAnchorNames[Overlay::kNumAnchors] = {
    "top-left", "top-center", "top-right", "bottom-left", "bottom-center", "bottom-right",
};

const char* Manager::overlayElementKey(Overlay::Element element)
{
    switch (element)
    {
    case Overlay::Element::Notifications:            return "Notifications";
    case Overlay::Element::LeaderboardNotifications: return "LeaderboardNotifications";
    case Overlay::Element::Challenges:               return "ChallengeIndicators";
    case Overlay::Element::Progress:                 return "ProgressIndicators";
    case Overlay::Element::Trackers:                 return "LeaderboardTrackers";
    }
    return "Notifications";
}

Overlay::Style Manager::loadOverlayStyle()
{
    Config::Table cfg = raConfig();
    Overlay::Style st = Overlay::Style::defaults();

    for (int e = 0; e < Overlay::kNumElements; e++)
    {
        std::string key = overlayElementKey((Overlay::Element)e);
        Overlay::ElementStyle& es = st.elements[e];

        std::string pos = cfg.GetString(key + "Position");
        for (int a = 0; a < Overlay::kNumAnchors; a++)
            if (pos == kAnchorNames[a]) es.anchor = (Overlay::Anchor)a;
        // the middle of the screen was an option once; it covered too much of the game
        if (pos == "center") es.anchor = Overlay::Anchor::BottomCenter;

        es.rounded = !cfg.GetBool(key + "Square");
        es.bold = cfg.GetBool(key + "Bold");
        es.border = cfg.GetBool(key + "Border");
    }

    st.colored = !cfg.GetBool("OverlayBlackAndWhite");

    int scale = cfg.GetInt("OverlayScale");
    st.scale = (scale >= 50 && scale <= 200) ? scale / 100.f : 1.f;
    return st;
}

void Manager::saveOverlayStyle(const Overlay::Style& st)
{
    Config::Table cfg = raConfig();

    for (int e = 0; e < Overlay::kNumElements; e++)
    {
        std::string key = overlayElementKey((Overlay::Element)e);
        const Overlay::ElementStyle& es = st.elements[e];
        cfg.SetString(key + "Position", kAnchorNames[(int)es.anchor]);
        cfg.SetBool(key + "Square", !es.rounded);
        cfg.SetBool(key + "Bold", es.bold);
        cfg.SetBool(key + "Border", es.border);
    }
    cfg.SetInt("OverlayScale", (int)(st.scale * 100.f + 0.5f));
    cfg.SetBool("OverlayBlackAndWhite", !st.colored);
    Config::Save();
}

bool Manager::soundsEnabled() const
{
    return optSounds;
}

void Manager::osd(unsigned int color, const QString& text)
{
    if (emuInstance)
        emuInstance->osdAddMessage(color, "%s", text.toUtf8().constData());
}


// ---------------------------------------------------------------------------
// RAIntegration (developer toolkit)

bool Manager::isRAIntegrationEnabled() const
{
#ifdef RC_CLIENT_SUPPORTS_RAINTEGRATION
    return raConfig().GetBool("UseRAIntegration");
#else
    return false;
#endif
}

void Manager::setRAIntegrationEnabled(bool enabled)
{
    raConfig().SetBool("UseRAIntegration", enabled);
    Config::Save();
}

void Manager::initRAIntegration(QWidget* mainWindow)
{
#ifdef RC_CLIENT_SUPPORTS_RAINTEGRATION
    if (!isRAIntegrationEnabled() || raIntegrationLoaded || raIntegrationLoading || !mainWindow)
    {
        if (!isRAIntegrationEnabled() && isEnabled() && !isLoggedIn() && !loginPending)
            loginWithStoredToken();
        return;
    }

    raIntegrationWindow = mainWindow;

    if (!raIntegrationSupported())
    {
        osd(kColorWarn, "RetroAchievements: the RAIntegration toolkit is not available for this CPU architecture");
        if (isEnabled()) loginWithStoredToken();
        return;
    }

    QString dir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    if (!QFileInfo::exists(raIntegrationDllPath()))
    {
        // log in normally now; the toolkit can be loaded after downloading
        if (isEnabled()) loginWithStoredToken();
        offerRAIntegrationDownload("The RAIntegration developer toolkit is enabled, but "
                                   "the RA_Integration DLL was not found next to melonRA.exe.");
        return;
    }

    raIntegrationLoading = true;
    std::wstring wdir = dir.toStdWString();
    HWND hwnd = reinterpret_cast<HWND>(mainWindow->winId());
    rc_client_begin_load_raintegration(rcClient, wdir.c_str(), hwnd, MELONDS_RA_PRODUCT, MELONDS_RA_VERSION,
                                       ClientCallbacks::toolkitLoaded, nullptr);
#else
    (void)mainWindow;
#endif
}

bool Manager::raIntegrationSupported()
{
#if defined(RC_CLIENT_SUPPORTS_RAINTEGRATION) && (defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__))
    return true;
#else
    return false;
#endif
}

QString Manager::raIntegrationDllPath()
{
#if defined(_M_X64) || defined(__x86_64__)
    const char* name = "RA_Integration-x64.dll";
#else
    const char* name = "RA_Integration.dll";
#endif
    return QDir(QCoreApplication::applicationDirPath()).filePath(name);
}

void Manager::offerRAIntegrationDownload(const QString& reason)
{
    QWidget* parent = raIntegrationWindow;
    QMetaObject::invokeMethod(this, [this, parent, reason]()
    {
        auto res = QMessageBox::question(parent, "RAIntegration",
            reason + "\n\nDownload the latest version from retroachievements.org now?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (res != QMessageBox::Yes) return;

        osd(kColorInfo, "RetroAchievements: downloading RAIntegration...");
        downloadRAIntegration([this](bool ok, QString msg)
        {
            osd(ok ? kColorInfo : kColorError, QString("RetroAchievements: %1").arg(msg));
            if (ok && !raIntegrationLoaded && raIntegrationWindow)
            {
                // load it right away; the session already logged in normally,
                // so log out first and let the toolkit take over
                if (isLoggedIn()) rc_client_logout(rcClient);
                initRAIntegration(raIntegrationWindow);
            }
        });
    }, Qt::QueuedConnection);
}

void Manager::downloadRAIntegration(std::function<void(bool, QString)> done)
{
    auto finish = [this, done](bool ok, QString msg)
    {
        QMetaObject::invokeMethod(this, [done, ok, msg]() { if (done) done(ok, msg); }, Qt::QueuedConnection);
    };

    if (!raIntegrationSupported())
    {
        finish(false, "RAIntegration is only available for x86/x64 Windows builds");
        return;
    }

    // same lookup the official RAInterface performs
    http->request("https://retroachievements.org/dorequest.php", "r=latestintegration",
                  "application/x-www-form-urlencoded", [this, finish](const HttpResponse& resp)
    {
        if (resp.status != 200)
        {
            finish(false, QString("could not check the latest RAIntegration version (%1)")
                              .arg(resp.status ? QString("HTTP %1").arg(resp.status)
                                               : QString::fromStdString(resp.error)));
            return;
        }

        QJsonObject obj = QJsonDocument::fromJson(QByteArray::fromStdString(resp.body)).object();
#if defined(_M_X64) || defined(__x86_64__)
        QString url = obj.value("LatestVersionUrlX64").toString();
#else
        QString url = obj.value("LatestVersionUrl").toString();
#endif
        QString version = obj.value("LatestVersion").toString();
        if (url.isEmpty())
        {
            finish(false, "the server did not return a download link for RAIntegration");
            return;
        }

        http->request(url.toStdString(), "", "", [finish, version](const HttpResponse& dl)
        {
            // a PE file starts with "MZ"
            if (dl.status != 200 || dl.body.size() < 4096 || dl.body[0] != 'M' || dl.body[1] != 'Z')
            {
                finish(false, QString("RAIntegration download failed (%1)")
                                  .arg(dl.status ? QString("HTTP %1").arg(dl.status)
                                                 : QString::fromStdString(dl.error)));
                return;
            }

            QString path = raIntegrationDllPath();
            QString tmp = path + ".download";
            QFile file(tmp);
            if (!file.open(QIODevice::WriteOnly) ||
                file.write(dl.body.data(), (qint64)dl.body.size()) != (qint64)dl.body.size())
            {
                file.remove();
                finish(false, QString("could not write %1 (is the melonRA folder writable?)").arg(path));
                return;
            }
            file.close();

            QFile::remove(path);
            if (!QFile::rename(tmp, path))
            {
                QFile::remove(tmp);
                finish(false, QString("could not replace %1 (is it in use?)").arg(path));
                return;
            }

            finish(true, version.isEmpty() ? QString("RAIntegration downloaded")
                                           : QString("RAIntegration %1 downloaded").arg(version));
        });
    });
}

std::vector<Manager::ToolkitMenuItem> Manager::toolkitMenu() const
{
    std::vector<ToolkitMenuItem> items;
#ifdef RC_CLIENT_SUPPORTS_RAINTEGRATION
    if (!raIntegrationLoaded) return items;
    const rc_client_raintegration_menu_t* menu = rc_client_raintegration_get_menu(rcClient);
    if (!menu) return items;
    for (uint32_t i = 0; i < menu->num_items; i++)
    {
        const rc_client_raintegration_menu_item_t& it = menu->items[i];
        items.push_back({it.label ? it.id : 0u, QString::fromUtf8(it.label ? it.label : ""),
                         it.checked != 0, it.enabled != 0});
    }
#endif
    return items;
}

void Manager::activateToolkitMenuItem(uint32_t id)
{
#ifdef RC_CLIENT_SUPPORTS_RAINTEGRATION
    if (raIntegrationLoaded)
        rc_client_raintegration_activate_menu_item(rcClient, id);
#else
    (void)id;
#endif
}

bool Manager::toolkitHasModifications() const
{
#ifdef RC_CLIENT_SUPPORTS_RAINTEGRATION
    return raIntegrationLoaded && rc_client_raintegration_has_modifications(rcClient);
#else
    return false;
#endif
}


// ---------------------------------------------------------------------------
// instance binding

void Manager::attachInstance(EmuInstance* inst)
{
    if (!emuInstance) emuInstance = inst;
}

void Manager::detachInstance(EmuInstance* inst)
{
    if (emuInstance != inst) return;

    rc_client_unload_game(rcClient);
    {
        std::lock_guard<std::mutex> guard(hashLock);
        loadedHash.clear();
        gameLoadRequested = false;
    }
    ovl->clearAll();
    emuInstance = nullptr;
}


// ---------------------------------------------------------------------------
// settings

bool Manager::isEnabled() const
{
    return optEnabled;
}

void Manager::setEnabled(bool enabled)
{
    Config::Table cfg = raConfig();
    cfg.SetBool("Enabled", enabled);
    Config::Save();
    optEnabled = enabled;

    if (!enabled)
    {
        rc_client_unload_game(rcClient);
        rc_client_logout(rcClient);
        {
            std::lock_guard<std::mutex> guard(hashLock);
            loadedHash.clear();
        }
        ovl->clearAll();
    }
    else
    {
        loginWithStoredToken();
    }

    emit stateChanged();
}

bool Manager::isHardcorePreferred() const
{
    return rc_client_get_hardcore_enabled(rcClient) != 0;
}

void Manager::setHardcorePreferred(bool enabled)
{
    Config::Table cfg = raConfig();
    cfg.SetBool("Hardcore", enabled);
    Config::Save();

    // Enabling hardcore with a game loaded makes rc_client raise
    // RC_CLIENT_EVENT_RESET, which resets the emulator (required: switching
    // from casual to hardcore mid-session is not allowed).
    rc_client_set_hardcore_enabled(rcClient, enabled ? 1 : 0);

    osd(enabled ? kColorWarn : kColorInfo,
        enabled ? "RetroAchievements: Hardcore mode enabled"
                : "RetroAchievements: Hardcore mode disabled (casual mode)");

    reloadDisplayOptions();
    emit stateChanged();
}

bool Manager::isEncoreEnabled() const { return rc_client_get_encore_mode_enabled(rcClient) != 0; }
bool Manager::isUnpromotedEnabled() const { return rc_client_get_unpromoted_enabled(rcClient) != 0; }
bool Manager::isSpectatorEnabled() const { return rc_client_get_spectator_mode_enabled(rcClient) != 0; }

void Manager::setEncoreEnabled(bool enabled)
{
    raConfig().SetBool("Encore", enabled);
    Config::Save();
    rc_client_set_encore_mode_enabled(rcClient, enabled ? 1 : 0);
}

void Manager::setUnpromotedEnabled(bool enabled)
{
    raConfig().SetBool("Unpromoted", enabled);
    Config::Save();
    rc_client_set_unpromoted_enabled(rcClient, enabled ? 1 : 0);
}

void Manager::setSpectatorEnabled(bool enabled)
{
    raConfig().SetBool("Spectator", enabled);
    Config::Save();
    rc_client_set_spectator_mode_enabled(rcClient, enabled ? 1 : 0);
}


// ---------------------------------------------------------------------------
// account

bool Manager::isLoggedIn() const
{
    return rc_client_get_user_info(rcClient) != nullptr;
}

QString Manager::userName() const
{
    const rc_client_user_t* user = rc_client_get_user_info(rcClient);
    if (user) return QString::fromUtf8(user->username);
    return QString::fromStdString(raConfig().GetString("Username"));
}

QString Manager::userDisplayName() const
{
    const rc_client_user_t* user = rc_client_get_user_info(rcClient);
    return user ? QString::fromUtf8(user->display_name) : QString();
}

uint32_t Manager::userScore() const
{
    const rc_client_user_t* user = rc_client_get_user_info(rcClient);
    return user ? user->score : 0;
}

uint32_t Manager::userCasualScore() const
{
    const rc_client_user_t* user = rc_client_get_user_info(rcClient);
    return user ? user->score_casual : 0;
}

std::string Manager::userAvatarUrl() const
{
    const rc_client_user_t* user = rc_client_get_user_info(rcClient);
    if (!user) return {};
    if (user->avatar_url) return user->avatar_url;

    char buf[512];
    if (rc_client_user_get_image_url(user, buf, sizeof(buf)) == RC_OK) return buf;
    return {};
}

void Manager::loginWithStoredToken()
{
    if (isLoggedIn() || loginPending) return;

    Config::Table cfg = raConfig();
    std::string user = cfg.GetString("Username");
    std::string token = cfg.GetString("Token");
    if (user.empty() || token.empty()) return;

    loginPending = true;
    // non-null userdata marks a token login
    rc_client_begin_login_with_token(rcClient, user.c_str(), token.c_str(),
                                     ClientCallbacks::loginDone, (void*)1);
}

void Manager::sendRequest(const std::string& url, const std::string& post, const std::string& contentType,
                          std::function<void(const HttpResponse&)> callback)
{
    send(url, post, contentType, std::move(callback));
}

void Manager::send(const std::string& url, const std::string& post, const std::string& contentType,
                   std::function<void(const HttpResponse&)> callback)
{
    // unlocks and leaderboard entries are saved to disk until the server
    // has accepted (or definitively rejected) them
    const uint64_t handle = pending->track(post);
    offlineCache->noteRequest(post);
    const std::string key = OfflineCache::keyFor(post);

    http->request(url, post, contentType, [this, handle, key, post, callback](const HttpResponse& r)
    {
        pending->resolve(handle, r);

        // no answer, or the server is down: use the saved answer if there is one
        const bool unreachable = (r.status == 0 && !r.cancelled) || r.status >= 500;
        if (!unreachable)
        {
            if (r.status == 200) offlineCache->store(key, post, r.body);
            if (r.status > 0) setOffline(false);
            callback(r);
            return;
        }

        std::string saved;
        if (optOfflineSessions && offlineCache->lookup(key, post, saved))
        {
            setOffline(true);
            HttpResponse cached;
            cached.status = 200;
            cached.body = std::move(saved);
            callback(cached);
            return;
        }
        callback(r);
    });
}

void Manager::setOffline(bool on)
{
    if (offline.exchange(on) == on) return;

    // called from HTTP worker threads
    QMetaObject::invokeMethod(this, [this, on]()
    {
        if (on)
        {
            osd(kColorWarn, "RetroAchievements: offline, using saved achievement data. "
                            "Unlocks will be submitted when you're back online.");
            ovl->addPopup(Overlay::PopupKind::Info, "", "Playing offline",
                          "Achievements work as usual.",
                          "Unlocks are saved and sent once you're back online.", 6000);
        }
        else
        {
            osd(kColorInfo, "RetroAchievements: back online");
            submitSavedQueue();
        }
        emit stateChanged();
    }, Qt::QueuedConnection);
}

void Manager::submitSavedQueue()
{
    const rc_client_user_t* user = rc_client_get_user_info(rcClient);
    if (!user || !user->username || !user->token) return;
    if (rc_client_get_spectator_mode_enabled(rcClient)) return;
    // nothing can be delivered yet; this runs again once the server answers
    if (offline) return;

    std::string name = user->username;
    size_t n = pending->count(name);
    if (!n) return;

    osd(kColorInfo, QString("RetroAchievements: submitting %1 saved unlock(s) from an earlier session").arg(n));
    pending->replay(name, user->token, [this](int submitted, int remaining)
    {
        if (remaining)
            osd(kColorWarn, QString("RetroAchievements: %1 saved unlock(s) submitted, %2 still pending "
                                    "(will retry later)").arg(submitted).arg(remaining));
        else if (submitted)
            osd(kColorInfo, QString("RetroAchievements: %1 saved unlock(s) submitted").arg(submitted));
        emit stateChanged();
    });
}

void Manager::loginWithPassword(const QString& username, const QString& password)
{
    if (!isEnabled())
    {
        Config::Table cfg = raConfig();
        cfg.SetBool("Enabled", true);
        optEnabled = true;
    }

    if (isLoggedIn())
        rc_client_logout(rcClient);

    loginPending = true;
    QByteArray u = username.trimmed().toUtf8();
    QByteArray p = password.toUtf8();
    rc_client_begin_login_with_password(rcClient, u.constData(), p.constData(),
                                        ClientCallbacks::loginDone, nullptr);
}

void Manager::logout()
{
    rc_client_unload_game(rcClient);
    rc_client_logout(rcClient);
    {
        std::lock_guard<std::mutex> guard(hashLock);
        loadedHash.clear();
    }
    ovl->clearAll();

    Config::Table cfg = raConfig();
    cfg.SetString("Token", "");
    Config::Save();

    osd(kColorInfo, "RetroAchievements: logged out");
    emit stateChanged();
}


// ---------------------------------------------------------------------------
// hardcore

bool Manager::hardcoreActive() const
{
    if (!optEnabled || !rcClient) return false;
    if (!rc_client_get_hardcore_enabled(rcClient)) return false;

    // While a token login is in flight we don't know yet whether the session
    // will be hardcore, so err on the side of restricting.
    return isLoggedIn() || loginPending;
}

void Manager::notifyBlocked(const QString& action)
{
    osd(kColorError, QString("%1 is disabled in RetroAchievements Hardcore mode").arg(action));
}

bool Manager::requestHardcoreExemption(QWidget* parent, const QString& action)
{
    if (!hardcoreActive()) return true;

    QMessageBox::StandardButton res = QMessageBox::question(parent, "RetroAchievements - Hardcore Mode",
        QString("%1 is not available in Hardcore mode.\n\n"
                "Switch to Casual mode? Hardcore unlocks will not be awarded until "
                "Hardcore mode is enabled again, which resets the game.").arg(action),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (res != QMessageBox::Yes) return false;

    setHardcorePreferred(false);
    return true;
}


// ---------------------------------------------------------------------------
// emulator hooks

void Manager::identifyROM(const uint8_t* data, uint32_t len, const std::string& name)
{
    char hash[33] = {0};
    // DS and DSi titles use the same hashing method
    bool ok = data && len && rc_hash_generate_from_buffer(hash, RC_CONSOLE_NINTENDO_DS, data, len);

    std::lock_guard<std::mutex> guard(hashLock);
    pendingHash = ok ? hash : "";
    romName = name;
    if (ok)
        Platform::Log(Platform::LogLevel::Info, "[RetroAchievements] ROM hash: %s\n", hash);
}

void Manager::onROMStarted()
{
    if (!rcClient) return;

    std::string hash;
    bool sameGame;
    {
        std::lock_guard<std::mutex> guard(hashLock);
        hash = pendingHash;
        sameGame = !hash.empty() && hash == loadedHash && gameLoadRequested;
        gameLoadRequested = !hash.empty();
        loadedHash = hash;
    }

    ovl->clearIndicators();

    if (!optEnabled) return;

    if (hash.empty())
    {
        rc_client_unload_game(rcClient);
        emit stateChanged();
        return;
    }

    if (sameGame && rc_client_is_game_loaded(rcClient))
    {
        // same ROM rebooted: just reset the runtime
        rc_client_reset(rcClient);
        return;
    }

    if (!isLoggedIn() && !loginPending) return; // will load after login

    rc_client_unload_game(rcClient);
    rc_client_begin_load_game(rcClient, hash.c_str(), ClientCallbacks::gameLoaded, nullptr);
    emit stateChanged();
}

void Manager::onGameUnloaded()
{
    if (!rcClient) return;

    bool hadGame;
    {
        std::lock_guard<std::mutex> guard(hashLock);
        hadGame = gameLoadRequested;
        loadedHash.clear();
        gameLoadRequested = false;
    }

    ovl->clearIndicators();
    if (hadGame || rc_client_is_game_loaded(rcClient))
    {
        rc_client_unload_game(rcClient);
        emit stateChanged();
    }
}

void Manager::onReset()
{
    if (!rcClient) return;
    rc_client_reset(rcClient);
    ovl->clearIndicators();
}

void Manager::doFrame()
{
    if (rcClient && optEnabled)
        rc_client_do_frame(rcClient);
}

void Manager::idle()
{
    if (rcClient && optEnabled)
        rc_client_idle(rcClient);
}

bool Manager::canPause()
{
    if (!hardcoreActive() || !rc_client_is_game_loaded(rcClient)) return true;

    uint32_t frames = 0;
    if (rc_client_can_pause(rcClient, &frames)) return true;

    osd(kColorWarn, QString("Hardcore mode: you can pause again in %1 s")
                        .arg(frames / 60.0, 0, 'f', 1));
    return false;
}


// ---------------------------------------------------------------------------
// savestates

std::vector<uint8_t> Manager::serializeProgress()
{
    std::vector<uint8_t> out;
    if (!rcClient || !rc_client_is_game_loaded(rcClient)) return out;

    size_t size = rc_client_progress_size(rcClient);
    if (!size) return out;

    out.resize(size);
    if (rc_client_serialize_progress_sized(rcClient, out.data(), out.size()) != RC_OK)
        out.clear();
    return out;
}

void Manager::deserializeProgress(const uint8_t* data, size_t len)
{
    if (!rcClient || !rc_client_is_game_loaded(rcClient)) return;

    ovl->clearIndicators();
    if (!data || !len)
        rc_client_deserialize_progress_sized(rcClient, nullptr, 0);
    else if (rc_client_deserialize_progress_sized(rcClient, data, len) != RC_OK)
        rc_client_deserialize_progress_sized(rcClient, nullptr, 0);
}


// ---------------------------------------------------------------------------
// UI data

bool Manager::isGameLoaded() const
{
    return rcClient && rc_client_is_game_loaded(rcClient);
}

QString Manager::gameTitle() const
{
    const rc_client_game_t* game = rc_client_get_game_info(rcClient);
    return game && game->title ? QString::fromUtf8(game->title) : QString();
}

std::string Manager::gameBadgeUrl() const
{
    const rc_client_game_t* game = rc_client_get_game_info(rcClient);
    if (!game) return {};
    if (game->badge_url) return game->badge_url;

    char buf[512];
    if (rc_client_game_get_image_url(game, buf, sizeof(buf)) == RC_OK) return buf;
    return {};
}

QString Manager::richPresence() const
{
    char buf[256] = {0};
    if (rc_client_has_rich_presence(rcClient) &&
        rc_client_get_rich_presence_message(rcClient, buf, sizeof(buf)) > 0)
        return QString::fromUtf8(buf);
    return QString();
}

QString Manager::hardcoreStatusText() const
{
    return rc_client_get_hardcore_enabled(rcClient) ? "Hardcore Mode" : "Casual Mode";
}

void Manager::showAccountPopup()
{
    const rc_client_user_t* user = rc_client_get_user_info(rcClient);
    if (!user) return;

    QLocale loc;
    QString name = QString::fromUtf8(user->display_name ? user->display_name : user->username);
    ovl->addPopup(Overlay::PopupKind::Account, userAvatarUrl(), name,
                  QString("%1 points").arg(loc.toString(user->score)),
                  QString("%1 casual points").arg(loc.toString(user->score_casual)), 5000);
}

void Manager::showGamePlacard()
{
    const rc_client_game_t* game = rc_client_get_game_info(rcClient);
    if (!game) return;

    // like DuckStation: who's playing, at the start of the session
    if (optAccountOnStart && optPopups)
        showAccountPopup();

    rc_client_user_game_summary_t summary;
    rc_client_get_user_game_summary(rcClient, &summary);

    QString line1;
    if (summary.num_promoted_achievements == 0)
    {
        line1 = "This game has no achievements yet.";
    }
    else
    {
        line1 = QString("%1/%2 achievements, %3/%4 points")
                    .arg(summary.num_unlocked_achievements)
                    .arg(summary.num_promoted_achievements)
                    .arg(summary.points_unlocked)
                    .arg(summary.points_available);
    }

    QString line2 = hardcoreStatusText();
    if (summary.num_unsupported_achievements)
        line2 += QString(" - %1 unsupported").arg(summary.num_unsupported_achievements);

    // count additional achievement sets (subsets) loaded alongside the core set
    if (rc_client_subset_list_t* subsets = rc_client_create_subset_list(rcClient))
    {
        if (subsets->num_subsets > 1)
            line2 += QString(" - +%1 bonus set%2").arg(subsets->num_subsets - 1).arg(subsets->num_subsets > 2 ? "s" : "");
        rc_client_destroy_subset_list(subsets);
    }

    ovl->addPopup(Overlay::PopupKind::GameStart, gameBadgeUrl(),
                  QString::fromUtf8(game->title), line1, line2, 6000);

    // hardcore state must be clearly indicated when the game starts
    osd(rc_client_get_hardcore_enabled(rcClient) ? kColorWarn : kColorInfo,
        QString("RetroAchievements: %1").arg(hardcoreStatusText()));
}

static std::string achievementBadgeUrl(const rc_client_achievement_t* ach, bool locked)
{
    const char* url = locked ? ach->badge_locked_url : ach->badge_url;
    if (url) return url;

    char buf[512];
    int state = locked ? RC_CLIENT_ACHIEVEMENT_STATE_ACTIVE : RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED;
    if (rc_client_achievement_get_image_url(ach, state, buf, sizeof(buf)) == RC_OK) return buf;
    return {};
}

void Manager::handleEvent(const rc_client_event_t* ev)
{
    switch (ev->type)
    {
    case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
    {
        const rc_client_achievement_t* ach = ev->achievement;
        QString title = (ach->category & RC_CLIENT_ACHIEVEMENT_CATEGORY_UNPROMOTED)
                            ? "Unofficial Achievement Unlocked" : "Achievement Unlocked";
        if (rc_client_get_encore_mode_enabled(rcClient)) title += " (Encore)";

        ovl->addPopup(Overlay::PopupKind::Unlock, achievementBadgeUrl(ach, false), title,
                      QString::fromUtf8(ach->title),
                      QString("%1 (%2 points)").arg(QString::fromUtf8(ach->description)).arg(ach->points),
                      5000);
        if (optSounds) emit playSound(Sound_Unlock);
        emit stateChanged();
        break;
    }

    case RC_CLIENT_EVENT_GAME_COMPLETED:
    {
        const rc_client_game_t* game = rc_client_get_game_info(rcClient);
        rc_client_user_game_summary_t summary;
        rc_client_get_user_game_summary(rcClient, &summary);

        QString title = QString("%1 %2")
                            .arg(rc_client_get_hardcore_enabled(rcClient) ? "Mastered" : "Completed")
                            .arg(game ? QString::fromUtf8(game->title) : QString());
        ovl->addPopup(Overlay::PopupKind::Mastery, gameBadgeUrl(), title, userDisplayName(),
                      QString("%1 achievements, %2 points").arg(summary.num_unlocked_achievements)
                                                           .arg(summary.points_unlocked),
                      8000);
        if (optSounds) emit playSound(Sound_Mastery);
        break;
    }

    case RC_CLIENT_EVENT_SUBSET_COMPLETED:
    {
        const rc_client_subset_t* subset = ev->subset;
        std::string badge;
        if (subset->badge_url) badge = subset->badge_url;

        QString title = QString("%1 %2")
                            .arg(rc_client_get_hardcore_enabled(rcClient) ? "Mastered" : "Completed")
                            .arg(QString::fromUtf8(subset->title));
        ovl->addPopup(Overlay::PopupKind::Mastery, badge, title, userDisplayName(),
                      QString("%1 achievements").arg(subset->num_achievements), 8000);
        if (optSounds) emit playSound(Sound_Mastery);
        break;
    }

    case RC_CLIENT_EVENT_LEADERBOARD_STARTED:
        if (optLeaderboardPopups)
            ovl->addPopup(Overlay::PopupKind::Leaderboard, "", "Leaderboard attempt started",
                          QString::fromUtf8(ev->leaderboard->title),
                          QString::fromUtf8(ev->leaderboard->description), 3500);
        break;

    case RC_CLIENT_EVENT_LEADERBOARD_FAILED:
        if (optLeaderboardPopups)
            ovl->addPopup(Overlay::PopupKind::Leaderboard, "", "Leaderboard attempt failed",
                          QString::fromUtf8(ev->leaderboard->title), QString(), 3000);
        break;

    case RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED:
        if (optLeaderboardPopups)
            ovl->addPopup(Overlay::PopupKind::Leaderboard, "", "Leaderboard entry submitted",
                          QString("%1: %2").arg(QString::fromUtf8(ev->leaderboard->title))
                                           .arg(QString::fromUtf8(ev->leaderboard->tracker_value)),
                          QString(), 4000);
        if (optSounds) emit playSound(Sound_Leaderboard);
        break;

    case RC_CLIENT_EVENT_LEADERBOARD_SCOREBOARD:
        if (optLeaderboardPopups)
        {
            const rc_client_leaderboard_scoreboard_t* sb = ev->leaderboard_scoreboard;
            QString lbTitle = ev->leaderboard ? QString::fromUtf8(ev->leaderboard->title) : "Leaderboard";
            ovl->addPopup(Overlay::PopupKind::Leaderboard, "", lbTitle,
                          QString("Rank %1 of %2").arg(sb->new_rank).arg(sb->num_entries),
                          QString("Submitted %1 - best %2").arg(QString::fromUtf8(sb->submitted_score))
                                                           .arg(QString::fromUtf8(sb->best_score)),
                          5000);
        }
        break;

    case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW:
        ovl->showChallenge(ev->achievement->id, achievementBadgeUrl(ev->achievement, false),
                           QString("%1 (%2 points)").arg(QString::fromUtf8(ev->achievement->title))
                                                    .arg(ev->achievement->points),
                           QString::fromUtf8(ev->achievement->description));
        break;

    case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_HIDE:
        ovl->hideChallenge(ev->achievement->id);
        break;

    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW:
    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE:
        ovl->showProgress(achievementBadgeUrl(ev->achievement, true),
                          QString::fromUtf8(ev->achievement->measured_progress));
        break;

    case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_HIDE:
        ovl->hideProgress();
        break;

    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW:
    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE:
        ovl->showTracker(ev->leaderboard_tracker->id, QString::fromUtf8(ev->leaderboard_tracker->display));
        break;

    case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_HIDE:
        ovl->hideTracker(ev->leaderboard_tracker->id);
        break;

    case RC_CLIENT_EVENT_RESET:
        emit resetRequested();
        break;

    case RC_CLIENT_EVENT_SERVER_ERROR:
    {
        const rc_client_server_error_t* err = ev->server_error;
        ovl->addPopup(Overlay::PopupKind::Error, "", "RetroAchievements server error",
                      QString::fromUtf8(err->error_message ? err->error_message : "unknown error"),
                      QString::fromUtf8(err->api ? err->api : ""), 6000);
        break;
    }

    case RC_CLIENT_EVENT_DISCONNECTED:
        osd(kColorWarn, "RetroAchievements: connection lost. Unlocks are saved and will be "
                        "submitted when it returns, even if you close melonRA.");
        break;

    case RC_CLIENT_EVENT_RECONNECTED:
        osd(kColorInfo, "RetroAchievements: reconnected, pending unlocks submitted");
        // also flush anything left over from previous sessions
        submitSavedQueue();
        break;

    default:
        break;
    }
}

}
