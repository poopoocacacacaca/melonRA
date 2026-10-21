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
#include <memory>

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

    // the cover, or a null image if there's none (yet; coverReady follows)
    QImage cover(const Game& game);
    // identifies a game's cover
    static QString keyFor(const Game& game);

    // forgets covers that weren't found, so they're looked for again
    void retryMissing();
    // reloads everything, after the settings changed
    void reset();

    QString cacheFolder() const;

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

private:
    Covers();

    struct Job
    {
        QString key;
        QStringList urls;      // still to try
        QStringList names;     // titles to look up in the libretro index
        QString region;        // preferred libretro region tag ("USA", ...)
        bool libretroDone = false;
        // a request failed for a reason other than "not there" (offline,
        // server trouble): only skip the game for this session
        bool networkError = false;
    };

    QImage loadFile(const QString& path) const;
    void enqueue(const Game& game, const QString& key);
    void pump();
    void tryNext(std::shared_ptr<Job> job);
    void finish(const std::shared_ptr<Job>& job, const QByteArray& data);
    bool resolveLibretro(Job& job);
    void loadLibretroIndex();

    std::unique_ptr<RA::Http> http;
    QCache<QString, QImage> memory;
    QSet<QString> missing;
    QSet<QString> queued;
    std::deque<std::shared_ptr<Job>> queue;
    int running = 0;

    QStringList libretroIndex;
    bool libretroIndexLoaded = false;
    bool libretroIndexLoading = false;
    std::deque<std::shared_ptr<Job>> waitingForIndex;
};

}

#endif // GAMELIST_COVERS_H
