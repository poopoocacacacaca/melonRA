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

#ifndef RA_LIBRARY_H
#define RA_LIBRARY_H

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QString>

class QThread;

namespace RA
{

class Manager;

// The games in the game list, matched to RetroAchievements, so the
// achievements window can show any of them without loading it.
//
// Each ROM is fingerprinted once (cached on disk), the fingerprints are looked
// up in RetroAchievements' list of DS fingerprints (one request), and the
// player's progress for every DS game comes in one more request. A game's full
// achievement list is fetched only when it's opened in the window.
class Library : public QObject
{
    Q_OBJECT

public:
    struct Game
    {
        // from the game list
        QString path;
        QString title;
        QImage icon;

        std::string hash;
        uint32_t gameId = 0;     // 0: not on RetroAchievements (or not matched yet)
        bool matched = false;    // looked up on RetroAchievements (gameId is final)

        QString raTitle;         // the title on RetroAchievements
        std::string badgeUrl;

        bool hasProgress = false;
        uint32_t numAchievements = 0;
        uint32_t numUnlocked = 0;
        uint32_t numUnlockedHardcore = 0;
    };

    struct Achievement
    {
        uint32_t id = 0;
        QString title;
        QString description;
        uint32_t points = 0;
        bool unofficial = false;
        std::string badgeUrl;
        std::string lockedBadgeUrl;
        float rarity = 0.f;
        float rarityHardcore = 0.f;
        bool unlocked = false;          // in either mode
        bool unlockedHardcore = false;
    };

    struct Leaderboard
    {
        uint32_t id = 0;
        QString title;
        QString description;
    };

    struct Details
    {
        bool loading = false;
        QString error;
        QString title;
        std::string badgeUrl;
        std::vector<Achievement> achievements;
        std::vector<Leaderboard> leaderboards;
    };

    explicit Library(Manager& manager);
    ~Library() override;

    // Picks up the current game list: fingerprints new ROMs, then matches
    // them and fetches progress. Cheap if nothing changed.
    void refresh();
    // The achievements window is open: allow talking to the server. Until
    // then only the (local) fingerprinting runs, so nothing is requested for
    // a window that's never opened.
    void activate();
    bool isActive() const { return active; }
    bool needsLogin() const { return waitingForLogin; }
    // fetches the player's progress again (after playing)
    void refreshProgress();

    const std::vector<Game>& games() const { return list; }
    // what's going on, for the window's status line; empty when done
    QString status() const { return statusText; }

    // Fetches the achievements and leaderboards of a game (once per refresh);
    // emits detailsChanged when they arrive.
    void fetchDetails(uint32_t gameId);
    const Details* details(uint32_t gameId) const;

signals:
    void changed();
    void detailsChanged(uint32_t gameId);

private:
    struct HashCacheEntry
    {
        qint64 size;
        qint64 modified;
        std::string hash;
    };

    void loadHashCache();
    void saveHashCache();
    void startHashing();
    void hashingFinished(int serial, std::map<QString, HashCacheEntry> results);
    void fetchHashLibrary();
    void match();
    void fetchTitles();
    void fetchProgress();
    void setStatus(const QString& text);

    Manager& m;
    std::vector<Game> list;
    QString statusText;

    std::map<QString, HashCacheEntry> hashCache;   // by ROM path
    std::map<std::string, uint32_t> hashLibrary;   // fingerprint -> game ID
    qint64 hashLibraryTime = 0;
    bool hashLibraryLoading = false;

    QPointer<QThread> hashThread;
    std::shared_ptr<std::atomic<bool>> hashCancel;
    int hashSerial = 0;

    std::map<uint32_t, Details> detailCache;
    bool active = false;
    bool waitingForLogin = false;
};

}

#endif // RA_LIBRARY_H
