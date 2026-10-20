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

#include "Updater.h"

#include <cstdio>
#include <vector>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QVersionNumber>

#include <archive.h>
#include <archive_entry.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#else
#include <cstdio>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#endif

#include "Config.h"
#include "version.h"

namespace Updater
{

namespace
{

constexpr const char* kDefaultRepo = "poopoocacacacaca/melonRA";
constexpr int kApiTimeoutMs = 10000;
constexpr int kDownloadTimeoutMs = 30000;  // with no data arriving
constexpr int kMaxListedCommits = 30;

// suffixes for files written next to the program while replacing it
const QString kNewSuffix = ".melonRA-new";
const QString kOldSuffix = ".melonRA-old";

bool relaunchFlag = false;

QString repo()
{
#ifdef MELONDS_EMBED_BUILD_INFO
    // CI builds record the repository they were built from, so forks of
    // melonRA update from their own releases
    QString r = QStringLiteral(MELONDS_GIT_REPO);
    if (r.count('/') == 1)
        return r;
#endif
    return kDefaultRepo;
}

QString buildHash()
{
#ifdef MELONDS_EMBED_BUILD_INFO
    return QStringLiteral(MELONDS_GIT_HASH).trimmed().toLower();
#else
    return {};
#endif
}

QString buildBranch()
{
#ifdef MELONDS_EMBED_BUILD_INFO
    return QStringLiteral(MELONDS_GIT_BRANCH);
#else
    return {};
#endif
}

QString userAgent()
{
    return QStringLiteral("melonRA/" MELONRA_VERSION " (updater)");
}

QString apiUrl(const QString& path)
{
    return "https://api.github.com/repos/" + repo() + path;
}

// end of the package file name for this platform, e.g. "-windows-x86_64.zip"
QString assetSuffix()
{
#if defined(_WIN32)
#if defined(_M_ARM64) || defined(__aarch64__)
    return "-windows-arm64.zip";
#elif defined(_M_X64) || defined(__x86_64__)
    return "-windows-x86_64.zip";
#else
    return {};
#endif
#elif defined(__linux__)
    // only the AppImage is a single file we can swap out
    if (qEnvironmentVariableIsEmpty("APPIMAGE"))
        return {};
#if defined(__aarch64__)
    return "-linux-aarch64.AppImage";
#elif defined(__x86_64__)
    return "-linux-x86_64.AppImage";
#else
    return {};
#endif
#else
    return {};
#endif
}


// ---- HTTP ----------------------------------------------------------------

struct Response
{
    int status = 0;     // 0 if the server was never reached
    QByteArray body;
    QString error;
};

bool isApi(const QString& url)
{
    return url.startsWith("https://api.github.com/");
}

#ifdef _WIN32

std::wstring toWide(const QString& s)
{
    return s.toStdWString();
}

QString winError(const char* what)
{
    return QString("%1 failed (error %2)").arg(what).arg(GetLastError());
}

Response httpGet(const QString& url, const ProgressFn& progress,
                 const std::atomic<bool>& cancel, int timeoutMs)
{
    Response resp;
    std::wstring wurl = toWide(url);

    URL_COMPONENTS comps = {};
    comps.dwStructSize = sizeof(comps);
    wchar_t host[256] = {};
    comps.lpszHostName = host;
    comps.dwHostNameLength = 256;
    comps.dwUrlPathLength = (DWORD)-1;
    comps.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &comps))
    {
        resp.error = winError("WinHttpCrackUrl");
        return resp;
    }

    std::wstring path;
    if (comps.lpszUrlPath) path.assign(comps.lpszUrlPath, comps.dwUrlPathLength);
    if (comps.lpszExtraInfo) path.append(comps.lpszExtraInfo, comps.dwExtraInfoLength);
    if (path.empty()) path = L"/";

    std::wstring ua = toWide(userAgent());
    HINTERNET session = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        session = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
    {
        resp.error = winError("WinHttpOpen");
        return resp;
    }
    WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    HINTERNET connect = WinHttpConnect(session, host, comps.nPort, 0);
    HINTERNET req = nullptr;
    if (connect)
    {
        DWORD flags = (comps.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        req = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    }
    if (!req)
    {
        resp.error = winError("WinHttpOpenRequest");
        if (connect) WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return resp;
    }

    std::wstring headers;
    if (isApi(url))
        headers = L"Accept: application/vnd.github+json\r\n";

    BOOL ok = WinHttpSendRequest(req,
                                 headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                                 headers.empty() ? 0 : (DWORD)-1L,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = WinHttpReceiveResponse(req, nullptr);

    if (!ok)
    {
        resp.error = winError("HTTP request");
    }
    else
    {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        resp.status = (int)status;

        DWORD length = 0;
        size = sizeof(length);
        qint64 total = -1;
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &length, &size, WINHTTP_NO_HEADER_INDEX))
            total = length;
        if (total > 0) resp.body.reserve((qsizetype)total);

        for (;;)
        {
            if (cancel)
            {
                resp.status = 0;
                resp.error = "Cancelled";
                break;
            }

            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail))
            {
                resp.status = 0;
                resp.error = winError("WinHttpQueryDataAvailable");
                break;
            }
            if (avail == 0) break;

            qsizetype offset = resp.body.size();
            resp.body.resize(offset + avail);
            DWORD read = 0;
            if (!WinHttpReadData(req, resp.body.data() + offset, avail, &read))
            {
                resp.status = 0;
                resp.error = winError("WinHttpReadData");
                break;
            }
            resp.body.resize(offset + read);
            if (progress) progress(resp.body.size(), total);
        }
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return resp;
}

#else // !_WIN32

Response httpGet(const QString& url, const ProgressFn& progress,
                 const std::atomic<bool>& cancel, int timeoutMs)
{
    Response resp;

    // created on the calling (worker) thread, which runs its own event loop below
    QNetworkAccessManager nam;
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    if (isApi(url))
        req.setRawHeader("Accept", "application/vnd.github+json");

    QNetworkReply* reply = nam.get(req);

    QEventLoop loop;
    QTimer idle;  // restarted whenever data arrives
    idle.setSingleShot(true);
    QTimer poll;  // watches the cancel flag
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&idle, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&]
    {
        if (cancel) loop.quit();
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, &loop, [&](qint64 done, qint64 total)
    {
        idle.start(timeoutMs);
        if (progress) progress(done, total);
    });
    idle.start(timeoutMs);
    poll.start(100);
    loop.exec();

    if (!reply->isFinished())
    {
        reply->abort();
        resp.error = cancel ? "Cancelled" : "The connection timed out";
    }
    else
    {
        QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        resp.status = status.isValid() ? status.toInt() : 0;
        resp.body = reply->readAll();
        if (resp.status == 0)
            resp.error = reply->errorString();
    }

    delete reply;
    return resp;
}

#endif

QString describeFailure(const Response& resp)
{
    if (resp.status == 0)
        return "Couldn't reach GitHub: " + resp.error;
    if (resp.status == 403 || resp.status == 429)
        return "GitHub is limiting requests right now. Try again later.";
    return QString("GitHub returned HTTP %1.").arg(resp.status);
}

QJsonObject parseObject(const QByteArray& body)
{
    return QJsonDocument::fromJson(body).object();
}

void fillAsset(Release& rel, const QJsonObject& json)
{
    QString suffix = assetSuffix();
    if (suffix.isEmpty()) return;

    for (const QJsonValue& v : json["assets"].toArray())
    {
        QJsonObject asset = v.toObject();
        if (asset["name"].toString().endsWith(suffix, Qt::CaseInsensitive))
        {
            rel.assetUrl = asset["browser_download_url"].toString();
            rel.assetSize = (qint64)asset["size"].toDouble();
            return;
        }
    }
}

QString firstLine(const QString& s)
{
    int nl = s.indexOf('\n');
    return (nl < 0 ? s : s.left(nl)).trimmed();
}

CheckResult checkStable(const std::atomic<bool>& cancel)
{
    CheckResult res;

    Response resp = httpGet(apiUrl("/releases/latest"), {}, cancel, kApiTimeoutMs);
    if (resp.status == 404)
    {
        // no release published yet
        res.status = CheckResult::UpToDate;
        return res;
    }
    if (resp.status != 200)
    {
        res.error = describeFailure(resp);
        return res;
    }

    QJsonObject json = parseObject(resp.body);
    QString tag = json["tag_name"].toString();
    QString version = tag.startsWith('v') ? tag.mid(1) : tag;
    QVersionNumber latest = QVersionNumber::fromString(version);
    if (tag.isEmpty() || latest.isNull())
    {
        res.error = "Couldn't read the latest release's version.";
        return res;
    }

    if (latest <= QVersionNumber::fromString(MELONRA_VERSION))
    {
        res.status = CheckResult::UpToDate;
        return res;
    }

    Release& rel = res.release;
    rel.channel = Channel::Stable;
    rel.id = tag;
    rel.version = version;
    rel.notes = json["body"].toString();
    rel.pageUrl = json["html_url"].toString();
    fillAsset(rel, json);
    res.status = CheckResult::Available;
    return res;
}

CheckResult checkNightly(const std::atomic<bool>& cancel)
{
    CheckResult res;

    Response resp = httpGet(apiUrl("/releases/tags/nightly"), {}, cancel, kApiTimeoutMs);
    if (resp.status == 404)
    {
        res.error = "There is no nightly build yet.";
        return res;
    }
    if (resp.status != 200)
    {
        res.error = describeFailure(resp);
        return res;
    }

    QJsonObject json = parseObject(resp.body);
    QString body = json["body"].toString();

    // the nightly's notes end with "Built from <full hash>"; the title has the short hash
    QString sha;
    QRegularExpressionMatch m = QRegularExpression("Built from ([0-9a-f]{40})").match(body);
    if (m.hasMatch())
        sha = m.captured(1);
    else
    {
        m = QRegularExpression("\\(([0-9a-f]{7,40})\\)").match(json["name"].toString());
        if (m.hasMatch()) sha = m.captured(1);
    }
    if (sha.isEmpty())
    {
        res.error = "Couldn't read the nightly build's version.";
        return res;
    }

    Release& rel = res.release;
    rel.channel = Channel::Nightly;
    rel.id = sha;
    rel.version = "nightly " + sha.left(7);
    rel.pageUrl = json["html_url"].toString();
    fillAsset(rel, json);

    QString current = buildHash();
    if (!current.isEmpty() && (sha.startsWith(current) || current.startsWith(sha)))
    {
        res.status = CheckResult::UpToDate;
        return res;
    }

    if (!current.isEmpty())
    {
        // list what changed, and make sure the nightly is actually newer than this build
        Response cmp = httpGet(apiUrl("/compare/" + current + "..." + sha), {}, cancel, kApiTimeoutMs);
        if (cmp.status == 200)
        {
            QJsonObject diff = parseObject(cmp.body);
            QString status = diff["status"].toString();
            if (status == "behind" || status == "identical")
            {
                // this build is newer than (or the same as) the nightly
                res.status = CheckResult::UpToDate;
                return res;
            }

            QJsonArray commits = diff["commits"].toArray();
            int total = diff["total_commits"].toInt(commits.size());
            QStringList lines;
            // newest first
            for (int i = commits.size() - 1; i >= 0 && lines.size() < kMaxListedCommits; i--)
            {
                QJsonObject c = commits[i].toObject();
                QString msg = firstLine(c["commit"].toObject()["message"].toString());
                if (msg.startsWith("Merge ")) continue;
                lines << QString("- %1 (`%2`)").arg(msg, c["sha"].toString().left(7));
            }
            if (!lines.isEmpty())
            {
                rel.notes = QString("**Changes since your version (%1):**\n\n").arg(current.left(7))
                          + lines.join('\n');
                if (total > lines.size())
                    rel.notes += QString("\n\n…and %1 more.").arg(total - lines.size());
            }
        }
        // if the comparison fails (a local build GitHub doesn't know about),
        // still offer the nightly
    }

    if (rel.notes.isEmpty())
        rel.notes = body;

    res.status = CheckResult::Available;
    return res;
}


// ---- installing ----------------------------------------------------------

struct PackageFile
{
    QString path;  // relative to the program's folder
    QByteArray data;
};

// rejects absolute paths and anything climbing out of the program's folder
QString safeRelativePath(QString path)
{
    path.replace('\\', '/');
    while (path.startsWith("./")) path = path.mid(2);
    if (path.isEmpty() || path.startsWith('/') || path.contains(':'))
        return {};
    for (const QString& part : path.split('/'))
        if (part == "..") return {};
    return path;
}

bool readZip(const QByteArray& package, std::vector<PackageFile>& files, QString& error)
{
    struct archive* a = archive_read_new();
    archive_read_support_format_zip(a);
    archive_read_support_filter_all(a);

    if (archive_read_open_memory(a, package.constData(), (size_t)package.size()) != ARCHIVE_OK)
    {
        error = QString("The download isn't a valid zip file (%1).").arg(archive_error_string(a));
        archive_read_free(a);
        return false;
    }

    bool ok = true;
    struct archive_entry* entry;
    int r;
    while ((r = archive_read_next_header(a, &entry)) == ARCHIVE_OK)
    {
        if (archive_entry_filetype(entry) != AE_IFREG)
            continue;

        const char* name = archive_entry_pathname_utf8(entry);
        if (!name) name = archive_entry_pathname(entry);
        QString path = safeRelativePath(QString::fromUtf8(name ? name : ""));
        if (path.isEmpty())
        {
            error = "The download contains an invalid file name.";
            ok = false;
            break;
        }

        QByteArray data;
        la_int64_t size = archive_entry_size(entry);
        if (size > 0) data.reserve((qsizetype)size);
        char buf[65536];
        la_ssize_t n;
        while ((n = archive_read_data(a, buf, sizeof(buf))) > 0)
            data.append(buf, (qsizetype)n);
        if (n < 0)
        {
            error = QString("Couldn't unpack the download (%1).").arg(archive_error_string(a));
            ok = false;
            break;
        }

        files.push_back({path, std::move(data)});
    }

    if (ok && r != ARCHIVE_EOF)
    {
        error = QString("Couldn't unpack the download (%1).").arg(archive_error_string(a));
        ok = false;
    }

    archive_read_free(a);
    return ok;
}

bool writeFile(const QString& path, const QByteArray& data, QFileDevice::Permissions perms)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    bool ok = f.write(data) == data.size();
    f.close();
    if (ok && perms != QFileDevice::Permissions()) f.setPermissions(perms);
    return ok && f.error() == QFileDevice::NoError;
}

QString unwritableFolderMessage(const QString& dir)
{
    return QString("melonRA can't write to its folder (%1).\n\n"
                   "If it's in a protected folder such as Program Files, move it to a folder you "
                   "own, or download the update from the release page.")
        .arg(QDir::toNativeSeparators(dir));
}

#ifdef _WIN32

bool installWindows(const QByteArray& package, QString& error)
{
    std::vector<PackageFile> files;
    if (!readZip(package, files, error))
        return false;

    const QFileInfo self(QCoreApplication::applicationFilePath());
    const QString dir = self.absolutePath();

    // the package's exe replaces the running one, even if it has been renamed
    int exes = 0;
    for (const PackageFile& f : files)
        if (f.path.endsWith(".exe", Qt::CaseInsensitive) && !f.path.contains('/')) exes++;
    if (exes != 1)
    {
        error = "The download doesn't contain melonRA.exe.";
        return false;
    }
    for (PackageFile& f : files)
        if (f.path.endsWith(".exe", Qt::CaseInsensitive) && !f.path.contains('/'))
            f.path = self.fileName();

    // write everything next to the current files first, so a failure leaves
    // the current version untouched
    std::vector<QString> staged;
    for (const PackageFile& f : files)
    {
        QString path = dir + '/' + f.path + kNewSuffix;
        if (!writeFile(path, f.data, {}))
        {
            for (const QString& s : staged) QFile::remove(s);
            QFile::remove(path);
            error = unwritableFolderMessage(dir);
            return false;
        }
        staged.push_back(path);
    }

    // then swap them in. Windows lets a running exe be renamed, but not
    // overwritten or deleted; the old copies are removed on the next start.
    struct Swapped { QString target, old; };
    std::vector<Swapped> done;
    auto rollBack = [&]
    {
        for (auto it = done.rbegin(); it != done.rend(); ++it)
        {
            QFile::remove(it->target);
            if (!it->old.isEmpty()) QFile::rename(it->old, it->target);
        }
        for (const QString& s : staged) QFile::remove(s);
    };

    for (const PackageFile& f : files)
    {
        QString target = dir + '/' + f.path;
        QString old;
        if (QFile::exists(target))
        {
            old = target + kOldSuffix;
            // a leftover from an update whose program is still running can't be removed
            for (int i = 2; QFile::exists(old) && !QFile::remove(old); i++)
                old = target + kOldSuffix + QString::number(i);

            if (!QFile::rename(target, old))
            {
                rollBack();
                error = unwritableFolderMessage(dir);
                return false;
            }
        }
        if (!QFile::rename(target + kNewSuffix, target))
        {
            if (!old.isEmpty()) QFile::rename(old, target);
            rollBack();
            error = unwritableFolderMessage(dir);
            return false;
        }
        done.push_back({target, old});
    }

    return true;
}

#endif

#ifdef __linux__

bool installAppImage(const QByteArray& package, QString& error)
{
    const QString target = qEnvironmentVariable("APPIMAGE");
    if (target.isEmpty())
    {
        error = "melonRA isn't running from an AppImage.";
        return false;
    }
    if (!package.startsWith("\x7f" "ELF"))
    {
        error = "The download isn't an AppImage.";
        return false;
    }

    QFileDevice::Permissions perms = QFile::permissions(target)
        | QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
        | QFileDevice::ExeUser | QFileDevice::ExeGroup | QFileDevice::ExeOther;

    // write the new AppImage beside the old one and rename it over it; the
    // running program keeps using the old file until it exits
    const QString tmp = target + kNewSuffix;
    if (!writeFile(tmp, package, perms))
    {
        QFile::remove(tmp);
        error = unwritableFolderMessage(QFileInfo(target).absolutePath());
        return false;
    }
    if (std::rename(QFile::encodeName(tmp).constData(), QFile::encodeName(target).constData()) != 0)
    {
        QFile::remove(tmp);
        error = unwritableFolderMessage(QFileInfo(target).absolutePath());
        return false;
    }
    return true;
}

#endif

Config::Table globalConfig()
{
    return Config::GetGlobalTable();
}

}


// ---- public API ------------------------------------------------------------

Channel channel()
{
    return globalConfig().GetString("Updater.Channel") == "nightly" ? Channel::Nightly : Channel::Stable;
}

void setChannel(Channel ch)
{
    globalConfig().SetString("Updater.Channel", ch == Channel::Nightly ? "nightly" : "stable");
}

bool neverAsk()
{
    return globalConfig().GetBool("Updater.NeverAsk");
}

void setNeverAsk(bool never)
{
    globalConfig().SetBool("Updater.NeverAsk", never);
}

bool autoUpdate()
{
    return canInstall() && globalConfig().GetBool("Updater.AutoUpdate");
}

void setAutoUpdate(bool enabled)
{
    globalConfig().SetBool("Updater.AutoUpdate", enabled);
}

QString skippedRelease()
{
    return globalConfig().GetQString("Updater.SkippedRelease");
}

void setSkippedRelease(const QString& id)
{
    globalConfig().SetQString("Updater.SkippedRelease", id);
}

bool canInstall()
{
    return !assetSuffix().isEmpty();
}

QString cannotInstallReason()
{
#if defined(__APPLE__)
    return "On macOS, updates are installed by downloading them from the release page.";
#elif defined(__linux__)
    return "This copy of melonRA isn't the AppImage, so it can't update itself. "
           "Update it the same way you installed it.";
#else
    return "This build can't update itself. Download updates from the release page.";
#endif
}

QString currentVersionLabel()
{
    QString label = MELONRA_VERSION;
    QString hash = buildHash();
    if (hash.isEmpty())
        return label;

    QString branch = buildBranch();
    if (branch == "v" MELONRA_VERSION)
        return label;
    if (branch == "master")
        return label + " (nightly " + hash.left(7) + ")";
    return label + " (" + hash.left(7) + ")";
}

CheckResult check(Channel ch, const std::atomic<bool>& cancel)
{
    return ch == Channel::Nightly ? checkNightly(cancel) : checkStable(cancel);
}

bool download(const QString& url, QByteArray& out, const ProgressFn& progress,
              const std::atomic<bool>& cancel, QString& error)
{
    Response resp = httpGet(url, progress, cancel, kDownloadTimeoutMs);
    if (resp.status != 200)
    {
        error = (resp.status == 0) ? "The download failed: " + resp.error
                                   : QString("The download failed (HTTP %1).").arg(resp.status);
        return false;
    }
    if (resp.body.isEmpty())
    {
        error = "The download was empty.";
        return false;
    }
    out = std::move(resp.body);
    return true;
}

bool install(const QByteArray& package, QString& error)
{
#if defined(_WIN32)
    return installWindows(package, error);
#elif defined(__linux__)
    return installAppImage(package, error);
#else
    (void)package;
    error = cannotInstallReason();
    return false;
#endif
}

void cleanUpAfterUpdate()
{
#if defined(_WIN32)
    QDir dir(QFileInfo(QCoreApplication::applicationFilePath()).absolutePath());
    for (const QString& name : dir.entryList(QDir::Files | QDir::Hidden))
    {
        if (name.contains(kOldSuffix) || name.endsWith(kNewSuffix))
            QFile::remove(dir.filePath(name));
    }
#elif defined(__linux__)
    QString appImage = qEnvironmentVariable("APPIMAGE");
    if (!appImage.isEmpty())
        QFile::remove(appImage + kNewSuffix);
#endif
}

void requestRelaunch(bool relaunch)
{
    relaunchFlag = relaunch;
}

bool relaunchRequested()
{
    return relaunchFlag;
}

bool relaunch()
{
    QString program = QCoreApplication::applicationFilePath();
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

#ifdef __linux__
    QString appImage = qEnvironmentVariable("APPIMAGE");
    if (!appImage.isEmpty())
    {
        program = appImage;
        // set by the AppImage runtime for the old copy; the new one sets its own
        for (const char* var : {"APPIMAGE", "APPDIR", "ARGV0", "OWD"})
            env.remove(var);
    }
#endif

    QProcess proc;
    proc.setProgram(program);
    proc.setArguments(QCoreApplication::arguments().mid(1));
    proc.setWorkingDirectory(QDir::currentPath());
    proc.setProcessEnvironment(env);
    bool ok = proc.startDetached();
    if (!ok)
        printf("Updater: couldn't restart %s\n", qPrintable(program));
    return ok;
}

}
