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

#include "UpdaterDialogs.h"

#include <cstdio>
#include <memory>

#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QTextBrowser>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "Config.h"

namespace Updater
{

namespace
{

using Work = std::function<void(const ProgressFn&, const std::atomic<bool>&)>;

// Runs `work` on a background thread behind a progress dialog, which appears
// after `showAfterMs`. Returns false if the user cancelled.
bool runTask(QWidget* parent, const QString& label, int showAfterMs, const Work& work)
{
    QProgressDialog dlg(label, "Cancel", 0, 0, parent);
    dlg.setWindowTitle("melonRA Update");
    dlg.setWindowModality(Qt::ApplicationModal);
    dlg.setAutoClose(false);
    dlg.setAutoReset(false);
    dlg.setMinimumWidth(380);
    dlg.setMinimumDuration(showAfterMs);
    dlg.setValue(0);  // starts the timer that shows the dialog after showAfterMs

    std::atomic<bool> cancel{false};
    std::atomic<qint64> done{0}, total{-1};
    QObject::connect(&dlg, &QProgressDialog::canceled, [&] { cancel = true; });

    ProgressFn progress = [&](qint64 d, qint64 t)
    {
        done = d;
        total = t;
    };

    QThread* thread = QThread::create([&] { work(progress, cancel); });
    QEventLoop loop;
    QObject::connect(thread, &QThread::finished, &loop, &QEventLoop::quit);

    QTimer refresh;
    QObject::connect(&refresh, &QTimer::timeout, [&]
    {
        if (cancel) return;
        qint64 d = done, t = total;
        if (t > 0)
        {
            QLocale loc;
            dlg.setMaximum(1000);
            dlg.setValue((int)(d * 1000 / t));
            dlg.setLabelText(QString("%1\n%2 of %3").arg(label, loc.formattedDataSize(d),
                                                         loc.formattedDataSize(t)));
        }
    });

    thread->start();
    refresh.start(100);
    if (showAfterMs <= 0) dlg.show();
    loop.exec();
    thread->wait();
    delete thread;

    return !cancel;
}

void openPage(const Release& rel)
{
    QString url = rel.pageUrl;
    if (url.isEmpty()) url = "https://github.com/ppkkkkk/melonRA/releases";
    QDesktopServices::openUrl(QUrl(url));
}

void showFailure(QWidget* parent, const Release& rel, const QString& error)
{
    QMessageBox box(QMessageBox::Warning, "Update Failed - melonRA",
                    "melonRA couldn't be updated.\n\n" + error, QMessageBox::NoButton, parent);
    QPushButton* page = box.addButton("Open Release Page", QMessageBox::ActionRole);
    box.addButton(QMessageBox::Close);
    box.exec();
    if (box.clickedButton() == page)
        openPage(rel);
}

// returns true once the update is in place
bool downloadAndInstall(QWidget* parent, const Release& rel)
{
    QByteArray package;
    QString error;
    bool ok = false;

    bool finished = runTask(parent, "Downloading melonRA " + rel.version + "...", 0,
        [&](const ProgressFn& progress, const std::atomic<bool>& cancel)
        {
            ok = download(rel.assetUrl, package, progress, cancel, error);
        });
    if (!finished)
        return false;

    if (ok)
        ok = verify(rel, package, error);
    if (ok)
        ok = install(package, error);

    if (!ok)
    {
        showFailure(parent, rel, error);
        return false;
    }

    setLastInstalledPackage(rel.packageKey);
    Config::Save();
    return true;
}

// The file just installed is still what the server offers, but this build
// doesn't identify as it (an upload racing a newer commit, or a build without
// its commit recorded). Installing it again would only repeat the same thing,
// over and over with "Always update" on, so treat it as current.
void guardAgainstRepeats(CheckResult& res, const QString& lastInstalled)
{
    if (res.status == CheckResult::Available && !res.release.packageKey.isEmpty() &&
        res.release.packageKey == lastInstalled)
        res.status = CheckResult::UpToDate;
}

void askToRestart(QWidget* parent, const Release& rel)
{
    QMessageBox box(QMessageBox::Question, "Update Installed - melonRA",
                    QString("melonRA %1 is installed. Restart melonRA now to start using it?\n\n"
                            "If a game is running, anything you haven't saved will be lost.")
                        .arg(rel.version),
                    QMessageBox::NoButton, parent);
    QPushButton* restart = box.addButton("Restart Now", QMessageBox::AcceptRole);
    box.addButton("Later", QMessageBox::RejectRole);
    box.setDefaultButton(restart);
    box.exec();
    if (box.clickedButton() != restart)
        return;

    // main() starts the new version once everything has shut down normally
    requestRelaunch(true, rel.version);
    allowForegroundHandoff();
    QApplication::closeAllWindows();

    // a window refused to close, so we're not exiting after all
    for (QWidget* w : QApplication::topLevelWidgets())
    {
        if (w->isVisible() && w->isWindow() && !qobject_cast<QDialog*>(w))
        {
            requestRelaunch(false);
            break;
        }
    }
}

// `manual`: the user asked for this check, so skipped versions and "never ask" don't apply
void handleAvailable(QWidget* parent, const Release& rel, bool manual)
{
    UpdatePromptDialog dlg(rel, !manual, parent);
    dlg.exec();

    if (!manual && dlg.neverAskAgain())
        setNeverAsk(true);

    switch (dlg.choice())
    {
    case UpdatePromptDialog::Skip:
        setSkippedRelease(rel.id);
        break;

    case UpdatePromptDialog::UpdateNow:
        if (rel.assetUrl.isEmpty() || !canInstall())
        {
            openPage(rel);
            break;
        }
        Config::Save();
        if (downloadAndInstall(parent, rel))
            askToRestart(parent, rel);
        break;

    case UpdatePromptDialog::NotNow:
        break;
    }

    Config::Save();
}

QLabel* noteLabel(const QString& text)
{
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    QPalette pal = label->palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::PlaceholderText));
    label->setPalette(pal);
    return label;
}

// the background check started by startupCheck()
QPointer<QThread> backgroundCheck;
std::shared_ptr<std::atomic<bool>> backgroundCancel;

}


bool runAutoUpdate()
{
    cleanUpAfterUpdate();

    if (!autoUpdate())
        return false;

    const Channel ch = channel();
    CheckResult res;
    bool finished = runTask(nullptr, "Checking for updates...", 1500,
        [&](const ProgressFn&, const std::atomic<bool>& cancel)
        {
            res = check(ch, cancel);
        });

    if (!finished)
        return false;
    guardAgainstRepeats(res, lastInstalledPackage());
    if (res.status == CheckResult::Failed)
    {
        // probably offline; start normally
        printf("Updater: %s\n", qPrintable(res.error));
        return false;
    }
    if (res.status != CheckResult::Available || res.release.assetUrl.isEmpty())
        return false;

    if (!downloadAndInstall(nullptr, res.release))
        return false;

    // start the new version right away, while this one still has a window in
    // front, so the new window opens in front too
    QProgressDialog starting("Starting melonRA " + res.release.version + "...", QString(), 0, 0);
    starting.setWindowTitle("melonRA Update");
    starting.setMinimumWidth(380);
    starting.setMinimumDuration(0);
    starting.show();
    starting.raise();
    starting.activateWindow();
    QCoreApplication::processEvents();

    requestRelaunch(true, res.release.version);
    if (!relaunch())
    {
        QMessageBox::warning(&starting, "melonRA Update",
                             "melonRA " + res.release.version + " is installed, but couldn't be started "
                             "automatically. Open melonRA again to use it.");
    }
    requestRelaunch(false);
    return true;
}

void startupCheck(QWidget* window)
{
    // with "Always update" on, runAutoUpdate() already checked
    if (neverAsk() || autoUpdate() || backgroundCheck)
        return;

    const Channel ch = channel();
    const QString skipped = skippedRelease();
    const QString lastInstalled = lastInstalledPackage();
    QPointer<QWidget> win = window;
    auto cancel = std::make_shared<std::atomic<bool>>(false);

    QThread* thread = QThread::create([=]
    {
        CheckResult res = check(ch, *cancel);
        guardAgainstRepeats(res, lastInstalled);
        if (*cancel || res.status != CheckResult::Available || res.release.id == skipped)
        {
            if (res.status == CheckResult::Failed)
                printf("Updater: %s\n", qPrintable(res.error));
            return;
        }

        QMetaObject::invokeMethod(qApp, [win, res]
        {
            if (win && !neverAsk())
                handleAvailable(win, res.release, false);
        }, Qt::QueuedConnection);
    });

    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    backgroundCheck = thread;
    backgroundCancel = cancel;
    thread->start();
}

void shutdown()
{
    if (backgroundCheck)
    {
        *backgroundCancel = true;
        // a check that is stuck connecting is left to end with the process
        backgroundCheck->wait(2000);
    }
}


UpdatePromptDialog::UpdatePromptDialog(const Release& rel, bool showNeverAsk, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Update Available - melonRA");
    setMinimumWidth(480);

    const bool installable = canInstall() && !rel.assetUrl.isEmpty();
    const bool nightly = rel.channel == Channel::Nightly;

    auto* layout = new QVBoxLayout(this);

    auto* top = new QHBoxLayout();
    auto* icon = new QLabel();
    icon->setPixmap(QApplication::windowIcon().pixmap(64, 64));
    icon->setAlignment(Qt::AlignTop);
    top->addWidget(icon);

    auto* text = new QVBoxLayout();
    auto* heading = new QLabel(nightly ? "A new melonRA nightly build is available!"
                                       : "A new version of melonRA is available!");
    QFont hf = heading->font();
    hf.setPointSizeF(hf.pointSizeF() * 1.25);
    hf.setBold(true);
    heading->setFont(hf);
    text->addWidget(heading);

    QString what = nightly ? "Nightly build " + rel.id.left(7) : "melonRA " + rel.version;
    auto* info = new QLabel(QString("%1 is %2. You have %3.")
                                .arg(what, installable ? "ready to install" : "out",
                                     currentVersionLabel()));
    info->setWordWrap(true);
    text->addWidget(info);
    if (!installable)
        text->addWidget(noteLabel(cannotInstallReason()));
    top->addLayout(text, 1);
    layout->addLayout(top);

    if (!rel.notes.trimmed().isEmpty())
    {
        layout->addWidget(new QLabel(nightly ? "What's new:" : "Release notes:"));
        auto* notes = new QTextBrowser();
        notes->setOpenExternalLinks(true);
        notes->setMarkdown(rel.notes);
        notes->setMinimumHeight(180);
        layout->addWidget(notes, 1);
    }

    auto* bottom = new QHBoxLayout();
    if (showNeverAsk)
    {
        cbNeverAsk = new QCheckBox("Never ask again");
        cbNeverAsk->setToolTip("You can turn this back on in Config → Update Settings.");
        bottom->addWidget(cbNeverAsk);
    }
    bottom->addStretch(1);

    auto* skip = new QPushButton("Skip This Version");
    auto* later = new QPushButton("Not Now");
    auto* update = new QPushButton(installable ? "Update Now" : "Open Download Page");
    update->setDefault(true);
    bottom->addWidget(skip);
    bottom->addWidget(later);
    bottom->addWidget(update);
    layout->addLayout(bottom);

    connect(skip, &QPushButton::clicked, this, [this] { userChoice = Skip; accept(); });
    connect(later, &QPushButton::clicked, this, [this] { userChoice = NotNow; reject(); });
    connect(update, &QPushButton::clicked, this, [this] { userChoice = UpdateNow; accept(); });
}

bool UpdatePromptDialog::neverAskAgain() const
{
    return cbNeverAsk && cbNeverAsk->isChecked();
}


UpdateSettingsDialog::UpdateSettingsDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle("Update Settings - melonRA");
    setMinimumWidth(460);

    auto* layout = new QVBoxLayout(this);

    // channel
    auto* channelBox = new QGroupBox("Update Channel");
    auto* channelLayout = new QVBoxLayout(channelBox);
    rbStable = new QRadioButton("Stable");
    channelLayout->addWidget(rbStable);
    QLabel* stableNote = noteLabel("Official releases. Recommended for most people.");
    stableNote->setContentsMargins(22, 0, 0, 6);
    channelLayout->addWidget(stableNote);
    rbNightly = new QRadioButton("Nightly");
    channelLayout->addWidget(rbNightly);
    QLabel* nightlyNote = noteLabel("A new build for every change, as soon as it's made. "
                                    "You get new features and fixes first, but they're less tested.");
    nightlyNote->setContentsMargins(22, 0, 0, 0);
    channelLayout->addWidget(nightlyNote);
    layout->addWidget(channelBox);

    // behavior
    auto* behaviorBox = new QGroupBox("Update Behavior");
    auto* behaviorLayout = new QVBoxLayout(behaviorBox);
    cbAutoUpdate = new QCheckBox("Always update to the latest version");
    behaviorLayout->addWidget(cbAutoUpdate);
    QLabel* autoNote = noteLabel(canInstall()
        ? "New versions are downloaded and installed when melonRA starts, before it opens."
        : cannotInstallReason());
    autoNote->setContentsMargins(22, 0, 0, 6);
    behaviorLayout->addWidget(autoNote);
    cbNeverAsk = new QCheckBox("Never ask about updates");
    behaviorLayout->addWidget(cbNeverAsk);
    neverAskNote = noteLabel("melonRA won't check for updates when it starts. "
                             "You can still check from here.");
    neverAskNote->setContentsMargins(22, 0, 0, 0);
    behaviorLayout->addWidget(neverAskNote);
    layout->addWidget(behaviorBox);

    // current version and manual check
    auto* checkRow = new QHBoxLayout();
    checkRow->addWidget(new QLabel("Current version: " + currentVersionLabel()), 1);
    auto* checkButton = new QPushButton("Check for Updates Now");
    checkRow->addWidget(checkButton);
    layout->addLayout(checkRow);

    layout->addStretch(1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);

    // load
    (channel() == Channel::Nightly ? rbNightly : rbStable)->setChecked(true);
    cbAutoUpdate->setEnabled(canInstall());
    cbAutoUpdate->setChecked(autoUpdate());
    cbNeverAsk->setChecked(neverAsk());
    updateEnabledStates();

    connect(cbAutoUpdate, &QCheckBox::toggled, this, &UpdateSettingsDialog::updateEnabledStates);
    connect(checkButton, &QPushButton::clicked, this, &UpdateSettingsDialog::checkNow);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { save(); accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void UpdateSettingsDialog::updateEnabledStates()
{
    // updates are installed without asking, so "never ask" has nothing to do
    bool asks = !cbAutoUpdate->isChecked();
    cbNeverAsk->setEnabled(asks);
    neverAskNote->setEnabled(asks);
}

void UpdateSettingsDialog::save()
{
    Channel ch = rbNightly->isChecked() ? Channel::Nightly : Channel::Stable;
    if (ch != channel())
        setSkippedRelease({});
    setChannel(ch);
    if (canInstall())
        setAutoUpdate(cbAutoUpdate->isChecked());
    setNeverAsk(cbNeverAsk->isChecked());
    Config::Save();
}

void UpdateSettingsDialog::checkNow()
{
    // keep the choices made so far, in case the update restarts melonRA
    save();

    const Channel ch = channel();
    CheckResult res;
    bool finished = runTask(this, "Checking for updates...", 0,
        [&](const ProgressFn&, const std::atomic<bool>& cancel)
        {
            res = check(ch, cancel);
        });
    if (!finished)
        return;
    guardAgainstRepeats(res, lastInstalledPackage());

    switch (res.status)
    {
    case CheckResult::Failed:
        QMessageBox::warning(this, "Update Settings - melonRA",
                             "Couldn't check for updates.\n\n" + res.error);
        break;

    case CheckResult::UpToDate:
        QMessageBox::information(this, "Update Settings - melonRA",
                                 QString("You're up to date. melonRA %1 is the latest %2.")
                                     .arg(currentVersionLabel(),
                                          ch == Channel::Nightly ? "nightly build" : "release"));
        break;

    case CheckResult::Available:
        handleAvailable(this, res.release, true);
        break;
    }
}

}
