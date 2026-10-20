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

#ifndef UPDATER_H
#define UPDATER_H

#include <atomic>
#include <functional>

#include <QByteArray>
#include <QString>

// Checks GitHub for new melonRA builds and installs them.
//
// There are two channels: Stable follows the versioned releases, Nightly
// follows the "nightly" pre-release that CI rebuilds on every master commit.
// Installing in place is supported for the Windows build (a single exe) and
// the Linux AppImage. Other builds can only open the download page.
namespace Updater
{

enum class Channel { Stable, Nightly };

struct Release
{
    Channel channel = Channel::Stable;
    // identifies the release for "Skip This Version": the tag for stable
    // releases, the commit hash for nightlies
    QString id;
    // shown to the user, e.g. "0.2.0" or "nightly abc1234"
    QString version;
    // Markdown: the release notes, or for nightlies the new commits
    QString notes;
    QString pageUrl;
    // the package for this platform; empty if it can't be installed here
    QString assetUrl;
    qint64 assetSize = 0;
    // identifies the exact uploaded file (name and upload time), so the same
    // file is never installed twice in a row
    QString packageKey;
};

struct CheckResult
{
    enum Status { UpToDate, Available, Failed };
    Status status = Failed;
    Release release;
    QString error;
};

using ProgressFn = std::function<void(qint64 done, qint64 total)>;

// settings (stored in the global config)
Channel channel();
void setChannel(Channel ch);
bool neverAsk();
void setNeverAsk(bool never);
bool autoUpdate();
void setAutoUpdate(bool enabled);
QString skippedRelease();
void setSkippedRelease(const QString& id);
// the package installed last, see Release::packageKey
QString lastInstalledPackage();
void setLastInstalledPackage(const QString& key);

// whether this build can replace itself with a downloaded update
bool canInstall();
// why it can't, for the settings dialog
QString cannotInstallReason();

// "0.1.0" for release builds, "0.1.0 (nightly abc1234)" for others
QString currentVersionLabel();

// Blocking; call these off the GUI thread. `cancel` may be set from another
// thread to stop early.
CheckResult check(Channel ch, const std::atomic<bool>& cancel);
bool download(const QString& url, QByteArray& out, const ProgressFn& progress,
              const std::atomic<bool>& cancel, QString& error);

// Puts the downloaded package in place of the running build. The running
// process keeps working; the new version starts on the next launch.
bool install(const QByteArray& package, QString& error);

// Removes files left over from replacing the running build last time.
void cleanUpAfterUpdate();

// Starts the (updated) program again once this one has exited.
// `version` is what it was updated to, shown when the new one opens.
void requestRelaunch(bool relaunch = true, const QString& version = QString());
bool relaunchRequested();
// Starts the program again with the same arguments, letting its window come
// to the front. Returns false on failure.
bool relaunch();
// Lets the next program started take the foreground. Call while one of our
// windows is still in front (Windows only; elsewhere it does nothing).
void allowForegroundHandoff();
// In the restarted program: the version it was updated to, or empty if it
// wasn't just updated. Reading it clears it.
QString takeUpdatedVersion();

}

#endif // UPDATER_H
