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

#ifndef RA_CLIENT_H
#define RA_CLIENT_H

#include <atomic>
#include <functional>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <QObject>
#include <QString>

#include "version.h"

// Name and version reported to the RetroAchievements server in the
// User-Agent (MELONRA_VERSION lives in version.h.in)
#define MELONDS_RA_VERSION MELONRA_VERSION
#define MELONDS_RA_PRODUCT "melonRA"

typedef struct rc_client_t rc_client_t;
typedef struct rc_client_event_t rc_client_event_t;
typedef struct rc_api_request_t rc_api_request_t;
typedef struct rc_api_server_response_t rc_api_server_response_t;

class EmuInstance;
class QWidget;

namespace RA
{

class Http;
class ImageCache;
class Overlay;
class PendingQueue;

// Owns the rc_client instance and connects it to melonDS.
//
// Achievements are only processed for the first emulator instance (the one
// whose window the user normally plays in); additional local-multiplayer
// instances run without RetroAchievements.
//
// Threading:
//  - settings/account functions and dialogs: GUI thread
//  - frame processing, ROM identification, savestates: emulator thread
//  - HTTP completions: HTTP worker threads (rc_client is thread-safe)
// Signals are emitted from whichever thread raised them; GUI receivers get
// them queued automatically.
class Manager : public QObject
{
    Q_OBJECT

public:
    static void create();
    static void destroy();
    static Manager* get() { return instance; }

    // --- emulator instance binding ---
    void attachInstance(EmuInstance* inst);
    void detachInstance(EmuInstance* inst);
    bool isAttached(const EmuInstance* inst) const { return inst && inst == emuInstance; }

    // --- settings (GUI thread) ---
    bool isEnabled() const;
    void setEnabled(bool enabled);

    bool isHardcorePreferred() const;    // the user's setting
    void setHardcorePreferred(bool enabled);

    bool isEncoreEnabled() const;
    void setEncoreEnabled(bool enabled);
    bool isUnpromotedEnabled() const;
    void setUnpromotedEnabled(bool enabled);
    bool isSpectatorEnabled() const;
    void setSpectatorEnabled(bool enabled);

    // RAIntegration: the RetroAchievements developer toolkit DLL (Windows only).
    // Loaded once the main window exists, if enabled in the settings and
    // RA_Integration(-x64).dll is found next to the executable.
    bool isRAIntegrationEnabled() const;
    void setRAIntegrationEnabled(bool enabled); // takes effect on restart
    bool isRAIntegrationLoaded() const { return raIntegrationLoaded; }
    void initRAIntegration(QWidget* mainWindow);
    // Windows x86/x64 only: the toolkit DLL has no ARM64 build
    static bool raIntegrationSupported();
    // path the DLL is loaded from (next to the executable)
    static QString raIntegrationDllPath();
    // Downloads the latest RA_Integration DLL from retroachievements.org next
    // to the executable. done(ok, message) runs on the GUI thread.
    void downloadRAIntegration(std::function<void(bool, QString)> done);
    struct ToolkitMenuItem
    {
        uint32_t id;      // 0 = separator
        QString label;
        bool checked;
        bool enabled;
    };
    std::vector<ToolkitMenuItem> toolkitMenu() const;
    void activateToolkitMenuItem(uint32_t id);
    // true if the toolkit has unsaved changes the user should be asked about
    bool toolkitHasModifications() const;

    // re-read notification/overlay options from the config
    void reloadDisplayOptions();
    bool soundsEnabled() const;

    // --- account (GUI thread) ---
    bool isLoggedIn() const;
    bool isLoginPending() const { return loginPending.load(); }
    QString userName() const;
    QString userDisplayName() const;
    uint32_t userScore() const;
    uint32_t userCasualScore() const;
    std::string userAvatarUrl() const;

    void loginWithPassword(const QString& username, const QString& password);
    void logout();

    // --- hardcore ---
    // True while emulator features that give an advantage must be blocked.
    bool hardcoreActive() const;

    // Called when the user tries to do something hardcore forbids. Asks
    // whether to switch to casual mode; returns true if the action may proceed.
    bool requestHardcoreExemption(QWidget* parent, const QString& action);

    // Shows the standard "not available in hardcore" OSD message.
    void notifyBlocked(const QString& action);

    // --- emulator hooks (emulator thread) ---
    // Called from EmuInstance::loadROM with the (decompressed) ROM image,
    // before the cartridge object takes ownership of it.
    void identifyROM(const uint8_t* data, uint32_t len, const std::string& name = std::string());
    // A ROM was booted (or the cart was hot-swapped while running).
    void onROMStarted();
    // The console was booted without a cart, the cart was ejected, or
    // emulation stopped.
    void onGameUnloaded();
    void onReset();

    void doFrame();
    void idle();

    // Returns false (and shows a message) if hardcore pause throttling
    // forbids pausing right now.
    bool canPause();

    // --- savestates (emulator thread) ---
    std::vector<uint8_t> serializeProgress();
    void deserializeProgress(const uint8_t* data, size_t len);

    // --- data for the UI ---
    rc_client_t* client() const { return rcClient; }
    ImageCache& images() { return *imageCache; }
    Overlay& overlay() { return *ovl; }

    bool isGameLoaded() const;
    QString gameTitle() const;
    std::string gameBadgeUrl() const;
    QString richPresence() const;
    QString hardcoreStatusText() const;
    // unlocks/leaderboard entries saved because the server couldn't be reached
    size_t pendingSubmissions() const;

signals:
    // login/logout, game load/unload, hardcore toggled
    void stateChanged();
    void loginFinished(bool success, QString message);
    // rc_client asks for the emulated system to be reset (enabling hardcore)
    void resetRequested();
    // a badge or avatar finished downloading
    void imagesUpdated();
    void playSound(int sound);
    // RAIntegration menu items were added/removed/changed
    void toolkitMenuChanged();
    // RAIntegration asks for emulation to be paused
    void pauseRequested();

public:
    enum Sound
    {
        Sound_Unlock,
        Sound_Mastery,
        Sound_Leaderboard,
    };

private:
    Manager();
    ~Manager() override;

    void applyClientSettings();
    void loginWithStoredToken();
    // re-submits unlocks/leaderboard entries saved from earlier sessions
    void submitSavedQueue();
    std::string buildUserAgent() const;
    void osd(unsigned int color, const QString& text);

    void showGamePlacard();
    void handleEvent(const rc_client_event_t* event);

    // C callbacks handed to rc_client (defined in RAClient.cpp)
    friend struct ClientCallbacks;

    static Manager* instance;

    rc_client_t* rcClient = nullptr;
    std::unique_ptr<Http> http;
    std::unique_ptr<ImageCache> imageCache;
    std::unique_ptr<Overlay> ovl;
    std::unique_ptr<PendingQueue> pending;

    EmuInstance* emuInstance = nullptr;

    std::atomic<bool> loginPending{false};
    bool raIntegrationLoaded = false;
    bool raIntegrationLoading = false;
    QWidget* raIntegrationWindow = nullptr;
    void offerRAIntegrationDownload(const QString& reason);

    // hash of the ROM most recently passed to identifyROM, and of the game
    // currently loaded into rc_client
    std::mutex hashLock;
    std::string pendingHash;
    std::string romName;
    std::string loadedHash;
    bool gameLoadRequested = false;
};

}

#endif // RA_CLIENT_H
