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

#include "RAOfflineCache.h"

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrlQuery>

namespace RA
{

namespace
{

// requests whose answers only depend on who is asking and what about
const std::set<QString> kCacheable = {
    "login2",          // the account (token logins only, see lookup)
    "gameid",          // ROM hash -> game
    "achievementsets", // the game's achievements and leaderboards
    "patch",           // same, older API
    "startsession",    // your unlocks for the game
    "hashlibrary",     // achievements window: every known DS ROM
    "allprogress",     // achievements window: your progress per game
    "gameinfolist",    // achievements window: game titles
    "lbinfo",          // leaderboard rankings
};

QUrlQuery parse(const std::string& postData)
{
    return QUrlQuery(QString::fromStdString(postData));
}

QJsonObject parseObject(const std::string& body)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(body), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return {};
    return doc.object();
}

QString userOf(const QUrlQuery& q)
{
    return q.queryItemValue("u", QUrl::FullyDecoded).toLower();
}

}

OfflineCache::OfflineCache(const std::string& folderPath)
    : folder(QString::fromStdString(folderPath))
{
    QDir().mkpath(folder);
}

std::string OfflineCache::keyFor(const std::string& postData)
{
    if (postData.empty()) return {};
    QUrlQuery q = parse(postData);
    const QString r = q.queryItemValue("r");
    if (!kCacheable.count(r)) return {};

    // everything that identifies the answer: not the token or password, and
    // not the client version
    std::vector<std::pair<QString, QString>> items;
    for (const auto& [name, value] : q.queryItems(QUrl::FullyDecoded))
    {
        if (name == "r" || name == "t" || name == "p" || name == "l") continue;
        // the session start is the same in hardcore and casual, for any ROM
        // of the game
        if (r == "startsession" && (name == "h" || name == "m")) continue;
        items.emplace_back(name, name == "u" ? value.toLower() : value);
    }
    std::sort(items.begin(), items.end());

    QString key = r;
    for (const auto& [name, value] : items)
        key += "&" + name + "=" + value;
    return key.toStdString();
}

QString OfflineCache::pathFor(const std::string& key) const
{
    QByteArray hash = QCryptographicHash::hash(QByteArray::fromStdString(key), QCryptographicHash::Sha1);
    return folder + "/" + QString::fromLatin1(hash.toHex()) + ".json";
}

QString OfflineCache::unlocksPath(const QString& user) const
{
    QByteArray hash = QCryptographicHash::hash(user.toUtf8(), QCryptographicHash::Sha1);
    return folder + "/unlocks-" + QString::fromLatin1(hash.toHex().left(16)) + ".json";
}

void OfflineCache::store(const std::string& key, const std::string& postData, const std::string& body)
{
    if (key.empty()) return;

    // only answers the server marked as successful
    QJsonObject obj = parseObject(body);
    if (!obj.value("Success").toBool()) return;

    QUrlQuery q = parse(postData);
    const QString r = q.queryItemValue("r");
    if (r == "login2" && obj.value("Token").toString().isEmpty()) return;

    std::lock_guard<std::mutex> guard(lock);

    QSaveFile f(pathFor(key));
    if (f.open(QIODevice::WriteOnly))
    {
        f.write(QByteArray::fromStdString(body));
        f.commit();
    }

    // the server now knows about these unlocks; no need to keep them here
    if (r == "startsession")
        pruneUnlocks(userOf(q), body);
}

bool OfflineCache::lookup(const std::string& key, const std::string& postData, std::string& body)
{
    if (key.empty()) return false;

    std::lock_guard<std::mutex> guard(lock);

    QFile f(pathFor(key));
    if (!f.open(QIODevice::ReadOnly)) return false;
    body = f.readAll().toStdString();
    f.close();

    QUrlQuery q = parse(postData);
    const QString r = q.queryItemValue("r");

    if (r == "login2")
    {
        // offline, only the saved login of the same account, with the same
        // token, is accepted: never a password login
        const QString token = q.queryItemValue("t", QUrl::FullyDecoded);
        if (token.isEmpty() || parseObject(body).value("Token").toString() != token)
            return false;
    }
    else if (r == "startsession")
    {
        body = patchSession(userOf(q), body);
    }
    return !body.empty();
}

void OfflineCache::noteRequest(const std::string& postData)
{
    if (postData.empty()) return;
    QUrlQuery q = parse(postData);
    if (q.queryItemValue("r") != "awardachievement") return;

    const uint32_t id = q.queryItemValue("a").toUInt();
    const QString user = userOf(q);
    if (!id || user.isEmpty()) return;

    std::lock_guard<std::mutex> guard(lock);
    Unlock& u = unlocksFor(user)[id];
    if (!u.when) u.when = (int64_t)time(nullptr);
    u.hardcore = u.hardcore || q.queryItemValue("h").toInt() != 0;
    saveUnlocks(user);
}

OfflineCache::UnlockMap& OfflineCache::unlocksFor(const QString& user)
{
    auto it = unlocks.find(user);
    if (it != unlocks.end()) return it->second;

    UnlockMap& map = unlocks[user];
    QFile f(unlocksPath(user));
    if (f.open(QIODevice::ReadOnly))
    {
        QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        for (auto e = root.begin(); e != root.end(); ++e)
        {
            QJsonObject o = e.value().toObject();
            const uint32_t id = e.key().toUInt();
            if (!id) continue;
            map[id] = {o.value("hardcore").toBool(), (int64_t)o.value("when").toDouble()};
        }
    }
    return map;
}

void OfflineCache::saveUnlocks(const QString& user)
{
    const UnlockMap& map = unlocksFor(user);
    if (map.empty())
    {
        QFile::remove(unlocksPath(user));
        return;
    }

    QJsonObject root;
    for (const auto& [id, u] : map)
        root[QString::number(id)] = QJsonObject{{"hardcore", u.hardcore}, {"when", (double)u.when}};

    QSaveFile f(unlocksPath(user));
    if (f.open(QIODevice::WriteOnly))
    {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

std::string OfflineCache::patchSession(const QString& user, const std::string& body)
{
    QJsonObject obj = parseObject(body);
    if (obj.isEmpty()) return {};

    QJsonArray hardcore = obj.value("HardcoreUnlocks").toArray();
    QJsonArray casual = obj.value("Unlocks").toArray();
    std::set<uint32_t> haveHardcore, haveCasual;
    for (const QJsonValue& v : hardcore) haveHardcore.insert((uint32_t)v.toObject().value("ID").toDouble());
    for (const QJsonValue& v : casual) haveCasual.insert((uint32_t)v.toObject().value("ID").toDouble());

    // unlocks for other games are ignored by rcheevos, so all of them can go in
    for (const auto& [id, u] : unlocksFor(user))
    {
        QJsonObject entry{{"ID", (double)id}, {"When", (double)u.when}};
        if (u.hardcore && !haveHardcore.count(id))
            hardcore.append(entry);
        else if (!u.hardcore && !haveHardcore.count(id) && !haveCasual.count(id))
            casual.append(entry);
    }

    obj["HardcoreUnlocks"] = hardcore;
    obj["Unlocks"] = casual;
    obj["ServerNow"] = (double)time(nullptr);
    return QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString();
}

void OfflineCache::pruneUnlocks(const QString& user, const std::string& body)
{
    QJsonObject obj = parseObject(body);
    std::set<uint32_t> hardcore, casual;
    for (const QJsonValue& v : obj.value("HardcoreUnlocks").toArray())
        hardcore.insert((uint32_t)v.toObject().value("ID").toDouble());
    for (const QJsonValue& v : obj.value("Unlocks").toArray())
        casual.insert((uint32_t)v.toObject().value("ID").toDouble());

    UnlockMap& map = unlocksFor(user);
    bool changed = false;
    for (auto it = map.begin(); it != map.end();)
    {
        const bool known = hardcore.count(it->first) || (!it->second.hardcore && casual.count(it->first));
        if (known)
        {
            it = map.erase(it);
            changed = true;
        }
        else
            ++it;
    }
    if (changed) saveUnlocks(user);
}

}
