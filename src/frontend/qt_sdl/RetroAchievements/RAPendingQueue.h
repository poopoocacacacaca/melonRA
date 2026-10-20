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

#ifndef RA_PENDINGQUEUE_H
#define RA_PENDINGQUEUE_H

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <QByteArray>

namespace RA
{

class Http;
struct HttpResponse;

// Persistent queue of achievement unlocks and leaderboard submissions that
// have not reached the server yet.
//
// rc_client already retries failed submissions, but only in memory: if the
// emulator is closed while offline, they would be lost. Every unlock and
// leaderboard request passing through the server callback is recorded here
// before it is sent, and removed once the server has given a definitive
// answer. Whatever is left is re-submitted the next time the same user logs
// in (or when the connection comes back).
//
// Entries store what was earned and when, not the raw request: the request
// signature covers the time elapsed since the unlock, so replays are rebuilt
// and re-signed with rcheevos' own request builders.
//
// Each entry carries an HMAC-SHA256 under a random per-installation key kept
// in the config, so corrupted or hand-edited entries are discarded rather
// than submitted.
//
// All functions are thread-safe.
class PendingQueue
{
public:
    PendingQueue(Http& http, const std::string& path, const QByteArray& key);

    // Inspects an outgoing API request. If it submits an unlock or a
    // leaderboard entry, records it and returns a non-zero handle to pass to
    // resolve(); otherwise returns 0.
    uint64_t track(const std::string& postData);

    // Called with the response for a tracked request. Removes the entry
    // unless the request failed in a way that will be retried.
    void resolve(uint64_t handle, const HttpResponse& response);

    // Number of saved submissions for the given user.
    size_t count(const std::string& username);

    // Re-submits saved entries for this user. done(submitted, failed) is
    // called on an HTTP worker thread once every request has finished;
    // entries that still could not be delivered stay in the queue.
    void replay(const std::string& username, const std::string& token,
                std::function<void(int submitted, int remaining)> done);

private:
    enum class Kind { Unlock, Leaderboard };

    struct Entry
    {
        uint64_t handle;
        Kind kind;
        std::string user;
        uint32_t id;          // achievement or leaderboard id
        int32_t value;        // hardcore flag (unlock) or score (leaderboard)
        std::string gameHash;
        int64_t time;         // unix time the unlock/submission happened
        bool replaying;
    };

    static bool isDefinitive(const HttpResponse& response);
    std::string sign(const Entry& e) const;
    void load();
    void saveLocked();
    Entry* findDuplicateLocked(const Entry& e);

    Http& http;
    std::string path;
    QByteArray key;

    std::mutex lock;
    std::vector<Entry> entries;
    uint64_t nextHandle = 1;
};

}

#endif // RA_PENDINGQUEUE_H
