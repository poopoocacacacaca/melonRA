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

#ifndef RA_OFFLINECACHE_H
#define RA_OFFLINECACHE_H

#include <cstdint>
#include <ctime>
#include <map>
#include <mutex>
#include <string>

#include <QString>

namespace RA
{

// Lets achievement sessions start without an internet connection.
//
// The answers to the requests a session needs (login, game identification,
// the achievement sets, the session start with your unlocks, and the game
// library shown in the achievements window) are saved whenever the server
// gives them. When the server can't be reached, the saved answer is used
// instead, so a game you have played online before can be played offline
// with its achievements.
//
// Unlocks earned while offline are recorded here too and merged into the
// saved session start, so they don't trigger again in a later offline
// session. Delivering them to the server is the job of PendingQueue.
//
// All functions are thread-safe.
class OfflineCache
{
public:
    explicit OfflineCache(const std::string& folder);

    // Identifies a request whose answer can be replayed offline; empty for
    // requests that must reach the server (unlocks, pings, ...).
    static std::string keyFor(const std::string& postData);

    // Saves a successful answer to a request with this key.
    void store(const std::string& key, const std::string& postData, const std::string& body);

    // The saved answer for this request, adjusted for now (unlocks earned
    // offline, the current time). False if there is none.
    bool lookup(const std::string& key, const std::string& postData, std::string& body);

    // Records an unlock request as it is sent, so offline sessions know about it.
    void noteRequest(const std::string& postData);

    // Deletes the saved login (and its token) for this user, on logout.
    void forgetLogin(const std::string& username);

private:
    struct Unlock
    {
        bool hardcore = false;
        int64_t when = 0;
    };
    using UnlockMap = std::map<uint32_t, Unlock>;

    QString pathFor(const std::string& key) const;
    QString unlocksPath(const QString& user) const;
    UnlockMap& unlocksFor(const QString& user);
    void saveUnlocks(const QString& user);

    std::string patchSession(const QString& user, const std::string& body);
    void pruneUnlocks(const QString& user, const std::string& body);

    QString folder;
    std::mutex lock;
    std::map<QString, UnlockMap> unlocks;  // by lower-case user name
};

}

#endif // RA_OFFLINECACHE_H
