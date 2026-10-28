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

#ifndef GAMELIST_COVERS_H
#define GAMELIST_COVERS_H

#include <deque>
#include <functional>
#include <memory>
#include <vector>

#include <QCache>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace RA { class Http; }

namespace GameList
{

struct Game;

// Box art for the game list's grid view.
//
// Looked up in this order:
//  1. a custom covers folder, with files named by game code (ADAE.png) or
//     after the ROM file (Game.nds.png or Game.png)
//  2. covers downloaded before, kept in the covers folder
//  3. GameTDB, by game code and region (exact)
//  4. the libretro thumbnails, by title (for homebrew and anything GameTDB
//     doesn't have)
// Downloads happen in the background, one game at a time; covers that can't
// be found are remembered for a week so they aren't requested again.
class Covers : public QObject
{
    Q_OBJECT
public:
    static Covers* get();
    ~Covers() override;

    // The cover if it's loaded. Otherwise a null image, and the cover is
    // looked for on disk in the background (coverReady follows if found).
    // Never downloads anything, so it's cheap to call while painting.
    QImage cover(const Game& game);

    // Downloads the covers of the given games that aren't on disk yet, in the
    // background. `retry` also tries games whose cover wasn't found recently.
    void downloadMissing(const std::vector<Game>& games, bool retry = false);
    void stopDownloads();
    bool isDownloading() const { return batchTotal > 0; }
    // how many of the given games have no cover on disk
    int countMissing(const std::vector<Game>& games) const;

    // Whether covers may be downloaded without asking: "" (not asked yet),
    // "yes" or "no".
    static QString downloadConsent();
    static void setDownloadConsent(const QString& answer);
    // identifies a game's cover
    static QString keyFor(const Game& game);

    // forgets covers that weren't found, so they're looked for again
    void retryMissing();
    // reloads everything, after the settings changed
    void reset();

    QString cacheFolder() const;

    // A web address of the game's cover, for showing it elsewhere (Discord):
    // where its downloaded cover came from or, if downloads are allowed, the
    // first GameTDB cover that exists. Empty if there is none. `done` runs on
    // this thread, possibly right away.
    void onlineCover(const Game& game, std::function<void(const QString&)> done);

    // settings
    static bool useGameTDB();
    static void setUseGameTDB(bool on);
    static bool useLibretro();
    static void setUseLibretro(bool on);
    static QString customFolder();
    static void setCustomFolder(const QString& folder);

signals:
    void coverReady(const QString& key);
    void coversReset();
    // a batch of downloads: `done` of `total` games handled so far
    void downloadProgress(int done, int total);
    void downloadsFinished();

private:
    Covers();

    struct Job
    {
        QString key;
        QStringList urls;      // still to try
        QStringList names;     // titles to look up in the libretro index
        QString region;        // preferred libretro region tag ("USA", ...)
        QString url;           // being tried now
        bool libretroDone = false;
        // a request failed for a reason other than "not there" (offline,
        // server trouble): only skip the game for this session
        bool networkError = false;
    };

    static QImage loadFile(const QString& path);
    QStringList diskCandidates(const Game& game, const QString& key) const;
    bool onDisk(const Game& game, const QString& key) const;
    void enqueue(const Game& game, const QString& key);
    void pump();
    void tryNext(std::shared_ptr<Job> job);
    void finish(const std::shared_ptr<Job>& job, const QByteArray& data);
    bool resolveLibretro(Job& job);
    void loadLibretroIndex();
    QStringList gameTDBUrls(const Game& game) const;
    void probeOnline(const QString& key, QStringList urls, std::function<void(const QString&)> done);

    std::unique_ptr<RA::Http> http;
    QCache<QString, QImage> memory;
    QSet<QString> missing;      // not found online recently
    QSet<QString> queued;       // waiting to be downloaded
    QSet<QString> loading;      // being read from disk
    QSet<QString> notOnDisk;    // looked for on disk, not there
    int batchDone = 0;
    int batchTotal = 0;
    std::deque<std::shared_ptr<Job>> queue;
    int running = 0;

    QStringList libretroIndex;
    bool libretroIndexLoaded = false;
    bool libretroIndexLoading = false;
    std::deque<std::shared_ptr<Job>> waitingForIndex;
};

}

#endif // GAMELIST_COVERS_H
