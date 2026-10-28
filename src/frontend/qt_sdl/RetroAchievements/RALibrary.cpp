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

#include "RALibrary.h"
#include "RAClient.h"
#include "RAHttp.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <set>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#include "rc_api_runtime.h"
#include "rc_api_user.h"
#include "rc_client.h"
#include "rc_consoles.h"
#include "rc_hash.h"

#include "GameList/GameList.h"
#include "Platform.h"

namespace RA
{

namespace
{

constexpr qint64 kHashLibraryMaxAgeMs = 60 * 60 * 1000;
constexpr uint32_t kTitlesPerRequest = 100;

QString hashCachePath()
{
    return QString::fromStdString(melonDS::Platform::GetLocalFilePath("retroachievements/library_hashes.json"));
}

// rc_hash reads ROMs through these, so paths with any characters work
void* RC_CCONV fileOpen(const char* path)
{
    auto* f = new QFile(QString::fromUtf8(path));
    if (!f->open(QIODevice::ReadOnly))
    {
        delete f;
        return nullptr;
    }
    return f;
}

void RC_CCONV fileSeek(void* handle, int64_t offset, int origin)
{
    auto* f = static_cast<QFile*>(handle);
    qint64 base = origin == SEEK_CUR ? f->pos() : origin == SEEK_END ? f->size() : 0;
    f->seek(base + offset);
}

int64_t RC_CCONV fileTell(void* handle)
{
    return static_cast<QFile*>(handle)->pos();
}

size_t RC_CCONV fileRead(void* handle, void* buffer, size_t size)
{
    qint64 n = static_cast<QFile*>(handle)->read(static_cast<char*>(buffer), (qint64)size);
    return n < 0 ? 0 : (size_t)n;
}

void RC_CCONV fileClose(void* handle)
{
    delete static_cast<QFile*>(handle);
}

std::string hashFile(const QString& path)
{
    rc_hash_iterator_t it;
    QByteArray utf8 = path.toUtf8();
    rc_hash_initialize_iterator(&it, utf8.constData(), nullptr, 0);
    it.callbacks.filereader.open = fileOpen;
    it.callbacks.filereader.seek = fileSeek;
    it.callbacks.filereader.tell = fileTell;
    it.callbacks.filereader.read = fileRead;
    it.callbacks.filereader.close = fileClose;

    char hash[33] = {};
    bool ok = rc_hash_generate(hash, RC_CONSOLE_NINTENDO_DS, &it);
    rc_hash_destroy_iterator(&it);
    return ok ? std::string(hash) : std::string();
}

std::string badgeUrl(const char* url, const char* name, bool locked)
{
    if (url && *url) return url;
    if (!name || !*name) return {};
    return std::string("https://media.retroachievements.org/Badge/") + name + (locked ? "_lock.png" : ".png");
}

// carries a QPointer through rc_client's C callbacks, which run on HTTP threads
struct CallbackContext
{
    QPointer<Library> library;
};

}

Library::Library(Manager& manager) : m(manager)
{
    loadHashCache();
    connect(GameList::notifier(), &GameList::Notifier::gamesChanged, this, &Library::refresh);
    // waiting for the login (at startup, or after logging in later)
    connect(&m, &Manager::loginFinished, this, [this](bool success, QString)
    {
        if (success && active && waitingForLogin) refresh();
    });
}

Library::~Library()
{
    if (hashThread)
    {
        *hashCancel = true;
        hashThread->wait(2000);
    }
}

void Library::setStatus(const QString& text)
{
    statusText = text;
    emit changed();
}

void Library::activate()
{
    active = true;
    refresh();
}

void Library::refreshProgress()
{
    if (!active || hashLibrary.empty() || !m.isLoggedIn()) return;
    detailCache.clear();
    fetchProgress();
}

void Library::refresh()
{
    // keep what's known about games still in the list
    std::map<QString, Game> old;
    for (Game& g : list) old.emplace(g.path, std::move(g));
    list.clear();

    for (const GameList::Game& gl : GameList::scannedGames())
    {
        Game g;
        auto it = old.find(gl.path);
        if (it != old.end()) g = std::move(it->second);
        g.path = gl.path;
        g.title = gl.title;
        g.icon = gl.icon;
        list.push_back(std::move(g));
    }

    detailCache.clear(); // progress may have changed since

    if (list.empty())
    {
        setStatus({});
        return;
    }
    // ROMs are only read once the achievements window asks for them, so
    // nothing touches the disk while the game list is loading
    if (!active) return;
    startHashing();
}

void Library::loadHashCache()
{
    QFile f(hashCachePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.begin(); it != root.end(); ++it)
    {
        QJsonObject e = it.value().toObject();
        hashCache[it.key()] = {(qint64)e["size"].toDouble(), (qint64)e["modified"].toDouble(),
                               e["hash"].toString().toStdString()};
    }
}

void Library::saveHashCache()
{
    QJsonObject root;
    for (const auto& [path, e] : hashCache)
    {
        QJsonObject o;
        o["size"] = (double)e.size;
        o["modified"] = (double)e.modified;
        o["hash"] = QString::fromStdString(e.hash);
        root[path] = o;
    }
    QDir().mkpath(QFileInfo(hashCachePath()).absolutePath());
    QFile f(hashCachePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void Library::startHashing()
{
    if (hashCancel) *hashCancel = true;

    // only ROMs that are new or changed since they were last fingerprinted
    std::vector<QString> todo;
    for (Game& g : list)
    {
        QFileInfo info(g.path);
        auto it = hashCache.find(g.path);
        if (it != hashCache.end() && it->second.size == info.size() &&
            it->second.modified == info.lastModified().toMSecsSinceEpoch())
            g.hash = it->second.hash;
        else
            todo.push_back(g.path);
    }

    if (todo.empty())
    {
        fetchHashLibrary();
        return;
    }

    setStatus(QString("Reading %1 game%2...").arg(todo.size()).arg(todo.size() == 1 ? "" : "s"));

    const int serial = ++hashSerial;
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    hashCancel = cancel;
    QPointer<Library> self(this);

    QThread* thread = QThread::create([self, serial, cancel, todo]()
    {
        std::map<QString, HashCacheEntry> results;
        for (const QString& path : todo)
        {
            if (*cancel) return;
            QFileInfo info(path);
            results[path] = {info.size(), info.lastModified().toMSecsSinceEpoch(), hashFile(path)};
        }
        QMetaObject::invokeMethod(qApp, [self, serial, results]()
        {
            if (self) self->hashingFinished(serial, results);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    hashThread = thread;
    thread->start(QThread::LowPriority);
}

void Library::hashingFinished(int serial, std::map<QString, HashCacheEntry> results)
{
    if (serial != hashSerial) return;

    for (auto& [path, e] : results)
        hashCache[path] = e;
    saveHashCache();

    for (Game& g : list)
    {
        auto it = hashCache.find(g.path);
        if (it != hashCache.end()) g.hash = it->second.hash;
    }
    fetchHashLibrary();
}

void Library::fetchHashLibrary()
{
    waitingForLogin = false;
    if (!active)
    {
        setStatus({});
        return;
    }
    if (!m.isLoggedIn())
    {
        waitingForLogin = true;
        setStatus("Log in to see the achievements of your other games.");
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!hashLibrary.empty() && now - hashLibraryTime < kHashLibraryMaxAgeMs)
    {
        match();
        return;
    }
    if (hashLibraryLoading) return;
    hashLibraryLoading = true;
    setStatus("Looking up your games on RetroAchievements...");

    auto callback = [](int result, const char* error, rc_client_hash_library_t* lib, rc_client_t*, void* userdata)
    {
        auto* ctx = static_cast<CallbackContext*>(userdata);
        std::map<std::string, uint32_t> map;
        QString err;
        if (result == RC_OK && lib)
        {
            for (uint32_t i = 0; i < lib->num_entries; i++)
            {
                std::string h = lib->entries[i].hash;
                std::transform(h.begin(), h.end(), h.begin(), ::tolower);
                map[h] = lib->entries[i].game_id;
            }
        }
        else
            err = QString::fromUtf8(error ? error : rc_error_str(result));
        if (lib) rc_client_destroy_hash_library(lib);

        QPointer<Library> self = ctx->library;
        delete ctx;
        QMetaObject::invokeMethod(qApp, [self, map, err]()
        {
            if (!self) return;
            self->hashLibraryLoading = false;
            if (!err.isEmpty())
            {
                self->setStatus("Couldn't reach RetroAchievements: " + err);
                return;
            }
            self->hashLibrary = map;
            self->hashLibraryTime = QDateTime::currentMSecsSinceEpoch();
            self->match();
        }, Qt::QueuedConnection);
    };
    rc_client_begin_fetch_hash_library(m.client(), RC_CONSOLE_NINTENDO_DS, callback, new CallbackContext{this});
}

void Library::match()
{
    for (Game& g : list)
    {
        std::string h = g.hash;
        std::transform(h.begin(), h.end(), h.begin(), ::tolower);
        auto it = hashLibrary.find(h);
        g.gameId = (it != hashLibrary.end()) ? it->second : 0;
        g.matched = true; // no fingerprint: not a game RetroAchievements could know
    }
    setStatus({});
    fetchTitles();
    fetchProgress();
}

void Library::fetchTitles()
{
    std::set<uint32_t> needed;
    for (const Game& g : list)
        if (g.gameId && g.raTitle.isEmpty()) needed.insert(g.gameId);
    if (needed.empty()) return;

    std::vector<uint32_t> ids(needed.begin(), needed.end());
    for (size_t start = 0; start < ids.size(); start += kTitlesPerRequest)
    {
        uint32_t count = (uint32_t)std::min<size_t>(kTitlesPerRequest, ids.size() - start);

        auto callback = [](int result, const char*, rc_client_game_title_list_t* titles, rc_client_t*, void* userdata)
        {
            auto* ctx = static_cast<CallbackContext*>(userdata);
            struct Entry { uint32_t id; QString title; std::string badge; };
            std::vector<Entry> entries;
            if (result == RC_OK && titles)
            {
                for (uint32_t i = 0; i < titles->num_entries; i++)
                {
                    const rc_client_game_title_entry_t& e = titles->entries[i];
                    std::string badge = e.badge_url ? e.badge_url : "";
                    if (badge.empty() && e.badge_name[0])
                        badge = std::string("https://media.retroachievements.org/Images/") + e.badge_name + ".png";
                    entries.push_back({e.game_id, QString::fromUtf8(e.title ? e.title : ""), badge});
                }
            }
            if (titles) rc_client_destroy_game_title_list(titles);

            QPointer<Library> self = ctx->library;
            delete ctx;
            QMetaObject::invokeMethod(qApp, [self, entries]()
            {
                if (!self) return;
                for (const Entry& e : entries)
                    for (Game& g : self->list)
                        if (g.gameId == e.id) { g.raTitle = e.title; g.badgeUrl = e.badge; }
                emit self->changed();
            }, Qt::QueuedConnection);
        };
        rc_client_begin_fetch_game_titles(m.client(), ids.data() + start, count, callback, new CallbackContext{this});
    }
}

void Library::fetchProgress()
{
    auto callback = [](int result, const char*, rc_client_all_user_progress_t* progress, rc_client_t*, void* userdata)
    {
        auto* ctx = static_cast<CallbackContext*>(userdata);
        std::vector<rc_client_all_user_progress_entry_t> entries;
        if (result == RC_OK && progress)
            entries.assign(progress->entries, progress->entries + progress->num_entries);
        if (progress) rc_client_destroy_all_user_progress(progress);

        QPointer<Library> self = ctx->library;
        delete ctx;
        QMetaObject::invokeMethod(qApp, [self, entries]()
        {
            if (!self) return;
            for (Game& g : self->list)
            {
                if (!g.gameId) continue;
                g.hasProgress = true;
                g.numAchievements = g.numUnlocked = g.numUnlockedHardcore = 0;
                for (const auto& e : entries)
                {
                    if (e.game_id != g.gameId) continue;
                    g.numAchievements = e.num_achievements;
                    g.numUnlocked = e.num_unlocked_achievements;
                    g.numUnlockedHardcore = e.num_unlocked_achievements_hardcore;
                }
            }
            emit self->changed();
        }, Qt::QueuedConnection);
    };
    rc_client_begin_fetch_all_user_progress(m.client(), RC_CONSOLE_NINTENDO_DS, callback, new CallbackContext{this});
}

const Library::Details* Library::details(uint32_t gameId) const
{
    auto it = detailCache.find(gameId);
    return it == detailCache.end() ? nullptr : &it->second;
}

void Library::fetchDetails(uint32_t gameId)
{
    if (!gameId) return;
    auto existing = detailCache.find(gameId);
    if (existing != detailCache.end() && (existing->second.loading || existing->second.error.isEmpty()))
    {
        emit detailsChanged(gameId);
        return;
    }

    const rc_client_user_t* user = rc_client_get_user_info(m.client());
    if (!user || !user->username || !user->token)
        return;

    Details& d = detailCache[gameId];
    d = Details();
    d.loading = true;
    emit detailsChanged(gameId);

    // three requests: the game's achievements and leaderboards, and the
    // player's hardcore and casual unlocks
    struct Pending
    {
        QPointer<Library> library;
        uint32_t gameId;
        int remaining = 3;
        Details result;
        std::set<uint32_t> unlockedHardcore, unlockedCasual;
    };
    auto pending = std::make_shared<Pending>();
    pending->library = this;
    pending->gameId = gameId;
    auto mutex = std::make_shared<std::mutex>();

    auto finishOne = [pending, mutex]()
    {
        std::lock_guard<std::mutex> guard(*mutex);
        if (--pending->remaining > 0) return;

        Details res = pending->result;
        for (Achievement& a : res.achievements)
        {
            a.unlockedHardcore = pending->unlockedHardcore.count(a.id) != 0;
            a.unlocked = a.unlockedHardcore || pending->unlockedCasual.count(a.id) != 0;
        }
        QPointer<Library> self = pending->library;
        uint32_t id = pending->gameId;
        QMetaObject::invokeMethod(qApp, [self, id, res]()
        {
            if (!self) return;
            Details& d = self->detailCache[id];
            d = res;
            d.loading = false;
            emit self->detailsChanged(id);
        }, Qt::QueuedConnection);
    };

    auto toServerResponse = [](const HttpResponse& r, rc_api_server_response_t& resp)
    {
        memset(&resp, 0, sizeof(resp));
        if (r.status == 0)
        {
            resp.body = r.error.c_str();
            resp.body_length = r.error.size();
            resp.http_status_code = RC_API_SERVER_RESPONSE_CLIENT_ERROR;
        }
        else
        {
            resp.body = r.body.data();
            resp.body_length = r.body.size();
            resp.http_status_code = r.status;
        }
    };

    // the game
    {
        rc_api_fetch_game_data_request_t params;
        memset(&params, 0, sizeof(params));
        params.username = user->username;
        params.api_token = user->token;
        params.game_id = gameId;
        rc_api_request_t req;
        if (rc_api_init_fetch_game_data_request(&req, &params) == RC_OK)
        {
            m.sendRequest(req.url, req.post_data ? req.post_data : "", req.content_type ? req.content_type : "",
                [pending, mutex, finishOne, toServerResponse](const HttpResponse& r)
            {
                rc_api_server_response_t resp;
                toServerResponse(r, resp);
                rc_api_fetch_game_data_response_t data;
                int ok = rc_api_process_fetch_game_data_server_response(&data, &resp);
                {
                    std::lock_guard<std::mutex> guard(*mutex);
                    Details& d = pending->result;
                    if (ok == RC_OK && data.response.succeeded)
                    {
                        d.title = QString::fromUtf8(data.title ? data.title : "");
                        if (data.image_url && *data.image_url) d.badgeUrl = data.image_url;
                        else if (data.image_name && *data.image_name)
                            d.badgeUrl = std::string("https://media.retroachievements.org/Images/") + data.image_name + ".png";
                        for (uint32_t i = 0; i < data.num_achievements; i++)
                        {
                            const rc_api_achievement_definition_t& a = data.achievements[i];
                            Achievement ach;
                            ach.id = a.id;
                            ach.title = QString::fromUtf8(a.title ? a.title : "");
                            ach.description = QString::fromUtf8(a.description ? a.description : "");
                            ach.points = a.points;
                            ach.unofficial = a.category == RC_ACHIEVEMENT_CATEGORY_UNPROMOTED;
                            ach.badgeUrl = badgeUrl(a.badge_url, a.badge_name, false);
                            ach.lockedBadgeUrl = badgeUrl(a.badge_locked_url, a.badge_name, true);
                            ach.rarity = a.rarity;
                            ach.rarityHardcore = a.rarity_hardcore;
                            d.achievements.push_back(std::move(ach));
                        }
                        for (uint32_t i = 0; i < data.num_leaderboards; i++)
                        {
                            const rc_api_leaderboard_definition_t& lb = data.leaderboards[i];
                            if (lb.hidden) continue;
                            d.leaderboards.push_back({lb.id, QString::fromUtf8(lb.title ? lb.title : ""),
                                                      QString::fromUtf8(lb.description ? lb.description : "")});
                        }
                    }
                    else
                    {
                        d.error = QString::fromUtf8(data.response.error_message ? data.response.error_message
                                                    : (r.status == 0 ? r.error.c_str() : rc_error_str(ok)));
                    }
                }
                rc_api_destroy_fetch_game_data_response(&data);
                finishOne();
            });
            rc_api_destroy_request(&req);
        }
        else
            finishOne();
    }

    // the player's unlocks, in each mode
    for (int hardcore = 0; hardcore < 2; hardcore++)
    {
        rc_api_fetch_user_unlocks_request_t params;
        memset(&params, 0, sizeof(params));
        params.username = user->username;
        params.api_token = user->token;
        params.game_id = gameId;
        params.hardcore = (uint32_t)hardcore;
        rc_api_request_t req;
        if (rc_api_init_fetch_user_unlocks_request(&req, &params) != RC_OK)
        {
            finishOne();
            continue;
        }
        m.sendRequest(req.url, req.post_data ? req.post_data : "", req.content_type ? req.content_type : "",
            [pending, mutex, finishOne, toServerResponse, hardcore](const HttpResponse& r)
        {
            rc_api_server_response_t resp;
            toServerResponse(r, resp);
            rc_api_fetch_user_unlocks_response_t data;
            if (rc_api_process_fetch_user_unlocks_server_response(&data, &resp) == RC_OK && data.response.succeeded)
            {
                std::lock_guard<std::mutex> guard(*mutex);
                auto& set = hardcore ? pending->unlockedHardcore : pending->unlockedCasual;
                set.insert(data.achievement_ids, data.achievement_ids + data.num_achievement_ids);
            }
            rc_api_destroy_fetch_user_unlocks_response(&data);
            finishOne();
        });
        rc_api_destroy_request(&req);
    }
}

}
