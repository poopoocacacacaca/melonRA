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

#include "RAPendingQueue.h"
#include "RAHttp.h"

#include <atomic>
#include <ctime>
#include <memory>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QSaveFile>
#include <QUrlQuery>

#include "rc_api_runtime.h"

namespace RA
{

// two submissions of the same leaderboard score within this many seconds
// are the same submission being retried
static constexpr int64_t kLeaderboardDedupeWindow = 120;

static int64_t unixNow()
{
    return (int64_t)std::time(nullptr);
}

PendingQueue::PendingQueue(Http& http, const std::string& path, const QByteArray& key)
    : http(http), path(path), key(key)
{
    QDir().mkpath(QFileInfo(QString::fromStdString(path)).absolutePath());
    load();
}

std::string PendingQueue::sign(const Entry& e) const
{
    QByteArray msg;
    msg += (e.kind == Kind::Unlock ? "a|" : "l|");
    msg += QByteArray::fromStdString(e.user) + "|";
    msg += QByteArray::number(e.id) + "|" + QByteArray::number(e.value) + "|";
    msg += QByteArray::fromStdString(e.gameHash) + "|" + QByteArray::number((qlonglong)e.time);
    return QMessageAuthenticationCode::hash(msg, key, QCryptographicHash::Sha256).toHex().toStdString();
}

void PendingQueue::load()
{
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonArray arr = QJsonDocument::fromJson(file.readAll()).array();
    std::lock_guard<std::mutex> guard(lock);
    for (const QJsonValue& v : arr)
    {
        QJsonObject o = v.toObject();
        Entry e;
        e.handle = nextHandle++;
        e.kind = (o.value("type").toString() == "leaderboard") ? Kind::Leaderboard : Kind::Unlock;
        e.user = o.value("user").toString().toStdString();
        e.id = (uint32_t)o.value("id").toInteger();
        e.value = (int32_t)o.value("value").toInteger();
        e.gameHash = o.value("hash").toString().toStdString();
        e.time = (int64_t)o.value("time").toInteger();
        e.replaying = false;

        // drop anything malformed or tampered with
        if (e.user.empty() || e.id == 0 || e.time <= 0) continue;
        if (o.value("mac").toString().toStdString() != sign(e)) continue;

        entries.push_back(e);
    }
}

void PendingQueue::saveLocked()
{
    QString qpath = QString::fromStdString(path);
    if (entries.empty())
    {
        QFile::remove(qpath);
        return;
    }

    QJsonArray arr;
    for (const Entry& e : entries)
    {
        QJsonObject o;
        o["type"] = (e.kind == Kind::Leaderboard) ? "leaderboard" : "unlock";
        o["user"] = QString::fromStdString(e.user);
        o["id"] = (qint64)e.id;
        o["value"] = (qint64)e.value;
        o["hash"] = QString::fromStdString(e.gameHash);
        o["time"] = (qint64)e.time;
        o["mac"] = QString::fromStdString(sign(e));
        arr.append(o);
    }

    // write-then-rename, so a crash mid-write can't destroy the queue
    QSaveFile file(qpath);
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
        file.commit();
    }
}

PendingQueue::Entry* PendingQueue::findDuplicateLocked(const Entry& e)
{
    for (Entry& x : entries)
    {
        if (x.kind != e.kind || x.user != e.user || x.id != e.id || x.value != e.value) continue;
        if (e.kind == Kind::Unlock) return &x;
        int64_t dt = x.time - e.time;
        if (dt < 0) dt = -dt;
        if (dt <= kLeaderboardDedupeWindow) return &x;
    }
    return nullptr;
}

uint64_t PendingQueue::track(const std::string& postData)
{
    if (postData.empty()) return 0;

    QUrlQuery q(QString::fromStdString(postData));
    QString r = q.queryItemValue("r");

    Entry e;
    if (r == "awardachievement")
    {
        e.kind = Kind::Unlock;
        e.id = q.queryItemValue("a").toUInt();
        e.value = q.queryItemValue("h").toInt() ? 1 : 0;
    }
    else if (r == "submitlbentry")
    {
        e.kind = Kind::Leaderboard;
        e.id = q.queryItemValue("i").toUInt();
        e.value = q.queryItemValue("s").toInt();
    }
    else
    {
        return 0;
    }

    e.user = q.queryItemValue("u", QUrl::FullyDecoded).toStdString();
    e.gameHash = q.queryItemValue("m", QUrl::FullyDecoded).toStdString();
    e.time = unixNow() - q.queryItemValue("o").toLongLong();
    e.replaying = false;
    if (e.user.empty() || e.id == 0) return 0;

    std::lock_guard<std::mutex> guard(lock);

    // rc_client retries rebuild the request; map them onto the saved entry
    if (Entry* existing = findDuplicateLocked(e))
        return existing->handle;

    e.handle = nextHandle++;
    entries.push_back(e);
    saveLocked();
    return e.handle;
}

bool PendingQueue::isDefinitive(const HttpResponse& response)
{
    // no answer, server errors and rate limiting are worth retrying; anything
    // else (including "already unlocked") means the server has dealt with it
    if (response.cancelled || response.status == 0) return false;
    if (response.status >= 500 || response.status == 429 || response.status == 408) return false;
    return true;
}

void PendingQueue::resolve(uint64_t handle, const HttpResponse& response)
{
    if (!handle || !isDefinitive(response)) return;

    std::lock_guard<std::mutex> guard(lock);
    for (auto it = entries.begin(); it != entries.end(); ++it)
    {
        if (it->handle == handle)
        {
            entries.erase(it);
            saveLocked();
            return;
        }
    }
}

size_t PendingQueue::count(const std::string& username)
{
    std::lock_guard<std::mutex> guard(lock);
    size_t n = 0;
    for (const Entry& e : entries)
        if (e.user == username) n++;
    return n;
}

void PendingQueue::replay(const std::string& username, const std::string& token,
                          std::function<void(int, int)> done)
{
    struct Job
    {
        uint64_t handle;
        std::string url, post, contentType;
    };
    std::vector<Job> jobs;

    {
        std::lock_guard<std::mutex> guard(lock);
        int64_t now = unixNow();
        for (Entry& e : entries)
        {
            if (e.user != username || e.replaying) continue;

            int64_t elapsed = now - e.time;
            if (elapsed < 1) elapsed = 1;
            if (elapsed > 0x7FFFFFFF) elapsed = 0x7FFFFFFF;

            rc_api_request_t req;
            int result;
            if (e.kind == Kind::Unlock)
            {
                rc_api_award_achievement_request_t params = {};
                params.username = e.user.c_str();
                params.api_token = token.c_str();
                params.achievement_id = e.id;
                params.hardcore = e.value ? 1 : 0;
                params.game_hash = e.gameHash.c_str();
                params.seconds_since_unlock = (uint32_t)elapsed;
                result = rc_api_init_award_achievement_request(&req, &params);
            }
            else
            {
                rc_api_submit_lboard_entry_request_t params = {};
                params.username = e.user.c_str();
                params.api_token = token.c_str();
                params.leaderboard_id = e.id;
                params.score = e.value;
                params.game_hash = e.gameHash.c_str();
                params.seconds_since_completion = (uint32_t)elapsed;
                result = rc_api_init_submit_lboard_entry_request(&req, &params);
            }

            if (result == RC_OK && req.url && req.post_data)
            {
                e.replaying = true;
                jobs.push_back({e.handle, req.url, req.post_data, req.content_type ? req.content_type : ""});
            }
            rc_api_destroy_request(&req);
        }
    }

    if (jobs.empty())
    {
        if (done) done(0, 0);
        return;
    }

    struct Progress
    {
        std::atomic<int> outstanding;
        std::atomic<int> submitted{0};
        std::atomic<int> failed{0};
    };
    auto progress = std::make_shared<Progress>();
    progress->outstanding = (int)jobs.size();

    for (const Job& job : jobs)
    {
        uint64_t handle = job.handle;
        http.request(job.url, job.post, job.contentType, [this, handle, progress, done](const HttpResponse& resp)
        {
            if (isDefinitive(resp))
            {
                resolve(handle, resp);
                progress->submitted++;
            }
            else
            {
                std::lock_guard<std::mutex> guard(lock);
                for (Entry& e : entries)
                    if (e.handle == handle) e.replaying = false;
                progress->failed++;
            }

            if (--progress->outstanding == 0 && done)
                done(progress->submitted, progress->failed);
        });
    }
}

}
