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

#include "Covers.h"
#include "GameList.h"

#include <atomic>

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

#include "Config.h"
#include "Platform.h"
#include "version.h"
#include "RetroAchievements/RAHttp.h"

namespace GameList
{

namespace
{

constexpr int kParallelDownloads = 2;
constexpr qint64 kMissingRetryMs = 7LL * 24 * 60 * 60 * 1000;
constexpr qint64 kIndexMaxAgeMs = 30LL * 24 * 60 * 60 * 1000;
constexpr int kMaxCoverWidth = 512;     // covers are kept at most this wide
constexpr int kMemoryBudgetKB = 96 * 1024;

const char* const kGameTDB = "https://art.gametdb.com/ds/cover/";
const char* const kLibretro = "https://thumbnails.libretro.com/Nintendo%20-%20Nintendo%20DS/Named_Boxarts/";

// set while the covers object exists; download callbacks check it
std::atomic<bool> alive{false};

Config::Table globalConfig()
{
    return Config::GetGlobalTable();
}

// GameTDB's region folder for the last letter of a game code
QString gameTDBRegion(QChar r)
{
    switch (r.toLatin1())
    {
    case 'E': case 'L': case 'T': case 'O': return "US";
    case 'J': return "JA";
    case 'K': return "KO";
    case 'C': return "ZHCN";
    case 'D': return "DE";
    case 'F': return "FR";
    case 'I': return "IT";
    case 'S': return "ES";
    case 'H': return "NL";
    case 'U': return "AU";
    case 'R': return "RU";
    case 'Q': return "DK";
    case 'M': return "SE";
    case 'N': return "NO";
    default:  return "EN"; // P, V, W, X, Y, Z: Europe
    }
}

bool validCode(const QString& code)
{
    if (code.size() != 4) return false;
    for (QChar c : code)
        if (!c.isLetterOrNumber() || c.unicode() > 0x7F) return false;
    return true;
}

// lower case letters and digits only, without "(...)" and "[...]" tags
QString normalizeTitle(QString s)
{
    s.remove(QRegularExpression("\\([^)]*\\)|\\[[^\\]]*\\]"));
    s.replace('&', " and ");
    s = s.toLower();
    QString out;
    bool space = false;
    for (QChar c : s)
    {
        if (c.isLetterOrNumber()) { out += c; space = false; }
        else if (!space && !out.isEmpty()) { out += ' '; space = true; }
    }
    return out.trimmed();
}

}

Covers* Covers::get()
{
    static Covers* instance = nullptr;
    if (!instance)
    {
        instance = new Covers();
        instance->setParent(qApp);
    }
    return instance;
}

Covers::Covers()
{
    alive = true;
    http = std::make_unique<RA::Http>(kParallelDownloads);
    http->setUserAgent("melonRA/" MELONRA_VERSION " (game list covers)");
    memory.setMaxCost(kMemoryBudgetKB);
    QDir().mkpath(cacheFolder());
}

Covers::~Covers()
{
    alive = false;
    http->shutdown();
}

QString Covers::cacheFolder() const
{
    return QString::fromStdString(melonDS::Platform::GetLocalFilePath("covers"));
}

bool Covers::useGameTDB() { return globalConfig().GetBool("GameList.CoversGameTDB"); }
void Covers::setUseGameTDB(bool on) { globalConfig().SetBool("GameList.CoversGameTDB", on); }
bool Covers::useLibretro() { return globalConfig().GetBool("GameList.CoversLibretro"); }
void Covers::setUseLibretro(bool on) { globalConfig().SetBool("GameList.CoversLibretro", on); }
QString Covers::customFolder() { return globalConfig().GetQString("GameList.CoversFolder"); }
void Covers::setCustomFolder(const QString& folder) { globalConfig().SetQString("GameList.CoversFolder", folder); }

QString Covers::keyFor(const Game& game)
{
    if (validCode(game.gameCode))
        return game.gameCode.toUpper();
    // homebrew: by title
    QByteArray h = QCryptographicHash::hash(normalizeTitle(game.title).toUtf8(), QCryptographicHash::Md5);
    return "title-" + QString::fromLatin1(h.toHex().left(16));
}

QImage Covers::loadFile(const QString& path) const
{
    QImage img(path);
    if (img.isNull()) return img;
    if (img.width() > kMaxCoverWidth)
        img = img.scaledToWidth(kMaxCoverWidth, Qt::SmoothTransformation);
    return img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QImage Covers::cover(const Game& game)
{
    const QString key = keyFor(game);
    if (QImage* img = memory.object(key))
        return *img;

    auto remember = [this, &key](const QImage& img)
    {
        memory.insert(key, new QImage(img), std::max<qsizetype>(1, img.sizeInBytes() / 1024));
        return img;
    };

    // the player's own covers
    QString custom = customFolder();
    if (!custom.isEmpty())
    {
        QFileInfo rom(game.path);
        const QStringList names = {
            game.gameCode + ".png", game.gameCode + ".jpg", game.gameCode + ".jpeg",
            rom.fileName() + ".png", rom.completeBaseName() + ".png", rom.completeBaseName() + ".jpg",
        };
        for (const QString& n : names)
        {
            QString p = QDir(custom).filePath(n);
            if (QFileInfo::exists(p))
            {
                QImage img = loadFile(p);
                if (!img.isNull()) return remember(img);
            }
        }
    }

    // downloaded before
    QString cached = QDir(cacheFolder()).filePath(key + ".png");
    if (QFileInfo::exists(cached))
    {
        QImage img = loadFile(cached);
        if (!img.isNull()) return remember(img);
    }

    if (missing.contains(key) || queued.contains(key))
        return {};

    // looked for recently and not found
    QFileInfo marker(QDir(cacheFolder()).filePath(key + ".none"));
    if (marker.exists() &&
        marker.lastModified().msecsTo(QDateTime::currentDateTime()) < kMissingRetryMs)
    {
        missing.insert(key);
        return {};
    }

    enqueue(game, key);
    return {};
}

void Covers::enqueue(const Game& game, const QString& key)
{
    auto job = std::make_shared<Job>();
    job->key = key;

    if (useGameTDB() && validCode(game.gameCode))
    {
        // the ROM's own region first, then the common ones
        QStringList regions = {gameTDBRegion(game.gameCode[3])};
        for (const char* r : {"EN", "US", "JA"})
            if (!regions.contains(r)) regions << r;
        for (const QString& r : regions)
            job->urls << QString(kGameTDB) + r + "/" + game.gameCode.toUpper() + ".png";
    }

    if (useLibretro())
    {
        QString full = game.subtitle.isEmpty() ? game.title : game.title + " " + game.subtitle;
        job->names << full << game.title << QFileInfo(game.path).completeBaseName();
        job->names.removeDuplicates();
        job->region = game.region;
    }
    else
        job->libretroDone = true;

    if (job->urls.isEmpty() && job->libretroDone)
        return; // nowhere to look

    queued.insert(key);
    queue.push_back(job);
    pump();
}

void Covers::pump()
{
    while (running < kParallelDownloads && !queue.empty())
    {
        auto job = queue.front();
        queue.pop_front();
        running++;
        tryNext(job);
    }
}

void Covers::tryNext(std::shared_ptr<Job> job)
{
    if (job->urls.isEmpty() && !job->libretroDone)
    {
        // the title-based lookup needs the libretro index first
        if (!libretroIndexLoaded)
        {
            waitingForIndex.push_back(job);
            loadLibretroIndex();
            return;
        }
        job->libretroDone = true;
        resolveLibretro(*job);
    }

    if (job->urls.isEmpty())
    {
        finish(job, {});
        return;
    }

    const QString url = job->urls.takeFirst();
    http->request(url.toStdString(), "", "", [this, job](const RA::HttpResponse& r)
    {
        if (!alive) return;
        QByteArray data;
        if (r.status == 200)
            data = QByteArray(r.body.data(), (qsizetype)r.body.size());
        const bool netError = r.status == 0 || r.status == 429 || r.status >= 500;
        QMetaObject::invokeMethod(this, [this, job, data, netError]()
        {
            if (netError) job->networkError = true;
            QImage img;
            if (!data.isEmpty() && img.loadFromData(data))
                finish(job, data);
            else
                tryNext(job);
        }, Qt::QueuedConnection);
    });
}

bool Covers::resolveLibretro(Job& job)
{
    // titles in the index look like "Mario Kart DS (USA, Australia)"; match
    // on the part before the tags, preferring the ROM's region
    for (const QString& name : job.names)
    {
        QString want = normalizeTitle(name);
        if (want.isEmpty()) continue;

        QString best;
        for (const QString& entry : libretroIndex)
        {
            QString base = entry;
            base.chop(4); // ".png"
            if (normalizeTitle(base) != want) continue;
            if (best.isEmpty()) best = entry;
            if (!job.region.isEmpty() && base.contains(job.region, Qt::CaseInsensitive))
            {
                best = entry;
                break;
            }
        }
        if (!best.isEmpty())
        {
            job.urls << QString(kLibretro) + QString::fromLatin1(QUrl::toPercentEncoding(best));
            return true;
        }
    }
    return false;
}

void Covers::loadLibretroIndex()
{
    if (libretroIndexLoading) return;

    // a copy from the last month
    QString path = QDir(cacheFolder()).filePath("libretro_index.txt");
    QFileInfo info(path);
    if (info.exists() && info.lastModified().msecsTo(QDateTime::currentDateTime()) < kIndexMaxAgeMs)
    {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly))
        {
            libretroIndex = QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
            libretroIndexLoaded = true;
            auto jobs = std::move(waitingForIndex);
            waitingForIndex.clear();
            for (auto& j : jobs) tryNext(j);
            return;
        }
    }

    libretroIndexLoading = true;
    http->request(kLibretro, "", "", [this, path](const RA::HttpResponse& r)
    {
        if (!alive) return;
        QStringList names;
        if (r.status == 200)
        {
            // the folder listing: href="Some%20Game%20(USA).png"
            QString html = QString::fromUtf8(r.body.data(), (qsizetype)r.body.size());
            QRegularExpression re("href=\"([^\"/?]+\\.png)\"");
            for (auto it = re.globalMatch(html); it.hasNext();)
                names << QUrl::fromPercentEncoding(it.next().captured(1).toUtf8());
        }
        QMetaObject::invokeMethod(this, [this, names, path]()
        {
            libretroIndexLoading = false;
            libretroIndexLoaded = true; // even if empty: don't ask again this session
            libretroIndex = names;
            if (!names.isEmpty())
            {
                QFile f(path);
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                    f.write(names.join('\n').toUtf8());
            }
            auto jobs = std::move(waitingForIndex);
            waitingForIndex.clear();
            for (auto& j : jobs) tryNext(j);
        }, Qt::QueuedConnection);
    });
}

void Covers::finish(const std::shared_ptr<Job>& job, const QByteArray& data)
{
    running--;
    queued.remove(job->key);

    const QDir dir(cacheFolder());
    QImage img;
    if (!data.isEmpty() && img.loadFromData(data))
    {
        if (img.width() > kMaxCoverWidth)
            img = img.scaledToWidth(kMaxCoverWidth, Qt::SmoothTransformation);
        img.save(dir.filePath(job->key + ".png"), "PNG");
        QFile::remove(dir.filePath(job->key + ".none"));
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        memory.insert(job->key, new QImage(img), std::max<qsizetype>(1, img.sizeInBytes() / 1024));
        emit coverReady(job->key);
    }
    else
    {
        missing.insert(job->key);
        // not found anywhere: don't look again for a while. (If the network
        // was the problem, look again next time melonRA starts.)
        if (!job->networkError)
        {
            QFile marker(dir.filePath(job->key + ".none"));
            if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate))
                marker.close();
        }
    }

    pump();
}

void Covers::retryMissing()
{
    missing.clear();
    QDir dir(cacheFolder());
    for (const QString& f : dir.entryList({"*.none"}, QDir::Files))
        dir.remove(f);
    emit coversReset();
}

void Covers::reset()
{
    memory.clear();
    missing.clear();
    emit coversReset();
}

}
