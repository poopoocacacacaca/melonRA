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

#include "RADialogs.h"
#include "RAClient.h"
#include "RAImageCache.h"

#include <vector>

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSoundEffect>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "rc_client.h"

#include "Config.h"

namespace RA
{

static QPixmap roundedPixmap(const QImage& img, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, size, size), size * 0.12, size * 0.12);
    p.setClipPath(path);
    if (img.isNull())
        p.fillRect(pm.rect(), QColor(0x3A, 0x3D, 0x46));
    else
        p.drawImage(QRectF(0, 0, size, size), img);
    return pm;
}


// ---------------------------------------------------------------------------
// SoundPlayer

SoundPlayer::SoundPlayer(QObject* parent) : QObject(parent)
{
    const char* files[3] = {
        "qrc:/retroachievements/unlock.wav",
        "qrc:/retroachievements/mastery.wav",
        "qrc:/retroachievements/leaderboard.wav",
    };
    for (int i = 0; i < 3; i++)
    {
        effects[i] = new QSoundEffect(this);
        effects[i]->setSource(QUrl(files[i]));
        effects[i]->setVolume(0.6f);
    }
}

void SoundPlayer::play(int sound)
{
    if (sound < 0 || sound >= 3) return;
    Manager* m = Manager::get();
    if (m && !m->soundsEnabled()) return;
    effects[sound]->play();
}


// ---------------------------------------------------------------------------
// LoginDialog

LoginDialog* LoginDialog::current = nullptr;

void LoginDialog::open(QWidget* parent)
{
    if (current)
    {
        current->raise();
        current->activateWindow();
        return;
    }
    current = new LoginDialog(parent);
    current->show();
}

LoginDialog::LoginDialog(QWidget* parent) : QDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("RetroAchievements Login");
    connect(this, &QObject::destroyed, []() { current = nullptr; });

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        "Log in with your <a href=\"https://retroachievements.org\">RetroAchievements.org</a> account.<br>"
        "Your password is only sent to RetroAchievements; melonDS stores a login token instead.");
    intro->setOpenExternalLinks(true);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* form = new QFormLayout();
    username = new QLineEdit();
    username->setText(Manager::get()->userName());
    password = new QLineEdit();
    password->setEchoMode(QLineEdit::Password);
    form->addRow("Username:", username);
    form->addRow("Password:", password);
    layout->addLayout(form);

    status = new QLabel();
    status->setWordWrap(true);
    layout->addWidget(status);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    loginButton = buttons->addButton("Log in", QDialogButtonBox::AcceptRole);
    loginButton->setDefault(true);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(loginButton, &QPushButton::clicked, this, &LoginDialog::onLogin);
    connect(Manager::get(), &Manager::loginFinished, this, &LoginDialog::onLoginFinished);

    if (username->text().isEmpty()) username->setFocus();
    else password->setFocus();
}

void LoginDialog::onLogin()
{
    if (username->text().trimmed().isEmpty() || password->text().isEmpty())
    {
        status->setText("Enter your username and password.");
        return;
    }

    loginButton->setEnabled(false);
    username->setEnabled(false);
    password->setEnabled(false);
    status->setText("Logging in...");

    Manager::get()->loginWithPassword(username->text(), password->text());
}

void LoginDialog::onLoginFinished(bool success, QString message)
{
    if (success)
    {
        accept();
        return;
    }

    status->setText(QString("<span style=\"color:#d05050\">Login failed: %1</span>").arg(message.toHtmlEscaped()));
    loginButton->setEnabled(true);
    username->setEnabled(true);
    password->setEnabled(true);
    password->selectAll();
    password->setFocus();
}


// ---------------------------------------------------------------------------
// SettingsDialog

SettingsDialog* SettingsDialog::current = nullptr;

void SettingsDialog::open(QWidget* parent)
{
    if (current)
    {
        current->raise();
        current->activateWindow();
        return;
    }
    current = new SettingsDialog(parent);
    current->show();
}

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("RetroAchievements Settings");
    connect(this, &QObject::destroyed, []() { current = nullptr; });

    Manager* m = Manager::get();
    auto* layout = new QVBoxLayout(this);

    // account
    auto* accountBox = new QGroupBox("Account");
    auto* accountLayout = new QHBoxLayout(accountBox);
    avatar = new QLabel();
    avatar->setFixedSize(56, 56);
    accountLayout->addWidget(avatar);
    account = new QLabel();
    account->setWordWrap(true);
    accountLayout->addWidget(account, 1);
    auto* accountButtons = new QVBoxLayout();
    loginButton = new QPushButton("Log in...");
    logoutButton = new QPushButton("Log out");
    accountButtons->addWidget(loginButton);
    accountButtons->addWidget(logoutButton);
    accountLayout->addLayout(accountButtons);
    layout->addWidget(accountBox);

    // general
    auto* generalBox = new QGroupBox("General");
    auto* general = new QVBoxLayout(generalBox);
    cbEnabled = new QCheckBox("Enable RetroAchievements");
    cbHardcore = new QCheckBox("Hardcore mode (no savestate loading, cheats, slow motion or frame advance)");
    cbEncore = new QCheckBox("Encore mode (re-trigger achievements you already unlocked)");
    cbUnpromoted = new QCheckBox("Show unofficial/unpromoted achievements");
    cbSpectator = new QCheckBox("Spectator mode (don't submit unlocks or leaderboard entries)");
    general->addWidget(cbEnabled);
    general->addWidget(cbHardcore);
    general->addWidget(cbEncore);
    general->addWidget(cbUnpromoted);
    general->addWidget(cbSpectator);
    cbToolkit = new QCheckBox("Load the RAIntegration developer toolkit at startup (restart required)");
    general->addWidget(cbToolkit);
    auto* toolkitRow = new QHBoxLayout();
    toolkitDownload = new QPushButton("Download / update RAIntegration");
    toolkitStatus = new QLabel();
    toolkitStatus->setWordWrap(true);
    toolkitRow->addWidget(toolkitDownload);
    toolkitRow->addWidget(toolkitStatus, 1);
    general->addLayout(toolkitRow);
    if (!Manager::raIntegrationSupported())
    {
        cbToolkit->hide();
        toolkitDownload->hide();
        toolkitStatus->hide();
    }
    layout->addWidget(generalBox);

    // notifications
    auto* notifBox = new QGroupBox("On-screen notifications");
    auto* notif = new QVBoxLayout(notifBox);
    cbNotifications = new QCheckBox("Achievement and game notifications");
    cbLeaderboardNotifications = new QCheckBox("Leaderboard notifications");
    cbChallenges = new QCheckBox("Challenge indicators");
    cbProgress = new QCheckBox("Progress indicators");
    cbTrackers = new QCheckBox("Leaderboard trackers (always shown in hardcore)");
    cbSounds = new QCheckBox("Sound effects");
    for (QCheckBox* cb : {cbNotifications, cbLeaderboardNotifications, cbChallenges, cbProgress, cbTrackers, cbSounds})
        notif->addWidget(cb);
    layout->addWidget(notifBox);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);

    connect(loginButton, &QPushButton::clicked, this, [this]() { LoginDialog::open(this); });
    connect(logoutButton, &QPushButton::clicked, this, []() { Manager::get()->logout(); });

    connect(cbEnabled, &QCheckBox::toggled, this, [this, m](bool on) { if (!updating) m->setEnabled(on); });
    connect(cbHardcore, &QCheckBox::toggled, this, [this, m](bool on)
    {
        if (updating) return;
        if (on && m->isGameLoaded())
        {
            auto res = QMessageBox::question(this, "Enable Hardcore mode",
                "Enabling Hardcore mode resets the running game. Continue?",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (res != QMessageBox::Yes)
            {
                refresh();
                return;
            }
        }
        m->setHardcorePreferred(on);
    });
    connect(cbEncore, &QCheckBox::toggled, this, [this, m](bool on) { if (!updating) m->setEncoreEnabled(on); });
    connect(cbUnpromoted, &QCheckBox::toggled, this, [this, m](bool on) { if (!updating) m->setUnpromotedEnabled(on); });
    connect(cbSpectator, &QCheckBox::toggled, this, [this, m](bool on) { if (!updating) m->setSpectatorEnabled(on); });
    connect(cbToolkit, &QCheckBox::toggled, this, [this, m](bool on) { if (!updating) m->setRAIntegrationEnabled(on); });
    connect(toolkitDownload, &QPushButton::clicked, this, [this, m]()
    {
        toolkitDownload->setEnabled(false);
        toolkitStatus->setText("Downloading...");
        QPointer<SettingsDialog> self(this);
        m->downloadRAIntegration([self](bool ok, QString msg)
        {
            if (!self) return;
            self->toolkitDownload->setEnabled(true);
            self->toolkitStatus->setText(ok ? msg + ". Restart melonDS to load it." : msg);
        });
    });

    auto bindDisplay = [this, m](QCheckBox* cb, const char* key)
    {
        connect(cb, &QCheckBox::toggled, this, [this, m, key](bool on)
        {
            if (updating) return;
            Config::GetGlobalTable().GetTable("RetroAchievements").SetBool(key, on);
            Config::Save();
            m->reloadDisplayOptions();
        });
    };
    bindDisplay(cbNotifications, "Notifications");
    bindDisplay(cbLeaderboardNotifications, "LeaderboardNotifications");
    bindDisplay(cbChallenges, "ChallengeIndicators");
    bindDisplay(cbProgress, "ProgressIndicators");
    bindDisplay(cbTrackers, "LeaderboardTrackers");
    bindDisplay(cbSounds, "Sounds");

    connect(m, &Manager::stateChanged, this, &SettingsDialog::refresh);
    connect(m, &Manager::imagesUpdated, this, &SettingsDialog::refresh);

    refresh();
}

void SettingsDialog::refresh()
{
    Manager* m = Manager::get();
    updating = true;

    if (m->isLoggedIn())
    {
        account->setText(QString("Logged in as <b>%1</b><br>%2 hardcore points, %3 casual points")
                             .arg(m->userDisplayName().toHtmlEscaped())
                             .arg(m->userScore()).arg(m->userCasualScore()));
        avatar->setPixmap(roundedPixmap(m->images().get(m->userAvatarUrl()), 56));
        loginButton->setText("Switch account...");
        logoutButton->setEnabled(true);
    }
    else if (m->isLoginPending())
    {
        account->setText("Logging in...");
        avatar->setPixmap(roundedPixmap(QImage(), 56));
        loginButton->setText("Log in...");
        logoutButton->setEnabled(false);
    }
    else
    {
        account->setText("Not logged in.");
        avatar->setPixmap(roundedPixmap(QImage(), 56));
        loginButton->setText("Log in...");
        logoutButton->setEnabled(false);
    }

    Config::Table cfg = Config::GetGlobalTable().GetTable("RetroAchievements");
    cbEnabled->setChecked(m->isEnabled());
    cbHardcore->setChecked(m->isHardcorePreferred());
    cbEncore->setChecked(m->isEncoreEnabled());
    cbUnpromoted->setChecked(m->isUnpromotedEnabled());
    cbSpectator->setChecked(m->isSpectatorEnabled());
    cbToolkit->setChecked(cfg.GetBool("UseRAIntegration"));
    if (toolkitStatus->text().isEmpty() || toolkitStatus->text().startsWith("Installed") ||
        toolkitStatus->text().startsWith("Not installed"))
    {
        if (m->isRAIntegrationLoaded())
            toolkitStatus->setText("Installed and loaded.");
        else
            toolkitStatus->setText(QFileInfo::exists(Manager::raIntegrationDllPath()) ? "Installed." : "Not installed.");
    }
    cbNotifications->setChecked(cfg.GetBool("Notifications"));
    cbLeaderboardNotifications->setChecked(cfg.GetBool("LeaderboardNotifications"));
    cbChallenges->setChecked(cfg.GetBool("ChallengeIndicators"));
    cbProgress->setChecked(cfg.GetBool("ProgressIndicators"));
    cbTrackers->setChecked(cfg.GetBool("LeaderboardTrackers"));
    cbSounds->setChecked(cfg.GetBool("Sounds"));

    for (QCheckBox* cb : {cbHardcore, cbEncore, cbUnpromoted, cbSpectator, cbNotifications,
                          cbLeaderboardNotifications, cbChallenges, cbProgress, cbTrackers, cbSounds})
        cb->setEnabled(m->isEnabled());
    loginButton->setEnabled(m->isEnabled());

    updating = false;
}


// ---------------------------------------------------------------------------
// AchievementsDialog

AchievementsDialog* AchievementsDialog::current = nullptr;

void AchievementsDialog::open(QWidget* parent, bool leaderboards)
{
    if (!current)
    {
        current = new AchievementsDialog(parent);
        current->show();
    }
    else
    {
        current->raise();
        current->activateWindow();
    }
    current->tabs->setCurrentIndex(leaderboards ? 1 : 0);
}

AchievementsDialog::AchievementsDialog(QWidget* parent) : QDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("RetroAchievements");
    resize(720, 600);
    connect(this, &QObject::destroyed, []() { current = nullptr; });

    auto* layout = new QVBoxLayout(this);

    // header: game badge, title, summary
    auto* header = new QHBoxLayout();
    gameIcon = new QLabel();
    gameIcon->setFixedSize(64, 64);
    header->addWidget(gameIcon);
    auto* headerText = new QVBoxLayout();
    gameTitle = new QLabel();
    QFont tf = gameTitle->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.35);
    tf.setBold(true);
    gameTitle->setFont(tf);
    gameInfo = new QLabel();
    presence = new QLabel();
    presence->setWordWrap(true);
    progress = new QProgressBar();
    progress->setTextVisible(true);
    headerText->addWidget(gameTitle);
    headerText->addWidget(gameInfo);
    headerText->addWidget(presence);
    headerText->addWidget(progress);
    header->addLayout(headerText, 1);
    layout->addLayout(header);

    tabs = new QTabWidget();

    achievementTree = new QTreeWidget();
    achievementTree->setColumnCount(3);
    achievementTree->setHeaderLabels({"Achievement", "Points", "Status"});
    achievementTree->setIconSize(QSize(40, 40));
    achievementTree->setRootIsDecorated(true);
    achievementTree->setUniformRowHeights(false);
    achievementTree->setWordWrap(true);
    achievementTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    achievementTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    achievementTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    achievementTree->header()->setStretchLastSection(false);
    tabs->addTab(achievementTree, "Achievements");

    auto* lbPage = new QSplitter(Qt::Vertical);
    leaderboardTree = new QTreeWidget();
    leaderboardTree->setColumnCount(2);
    leaderboardTree->setHeaderLabels({"Leaderboard", "Status"});
    leaderboardTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    leaderboardTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    leaderboardTree->header()->setStretchLastSection(false);
    lbPage->addWidget(leaderboardTree);

    auto* entriesPane = new QWidget();
    auto* entriesLayout = new QVBoxLayout(entriesPane);
    entriesLayout->setContentsMargins(0, 0, 0, 0);
    entriesStatus = new QLabel("Select a leaderboard to see its rankings.");
    entriesLayout->addWidget(entriesStatus);
    entries = new QTableWidget(0, 4);
    entries->setHorizontalHeaderLabels({"Rank", "User", "Result", "Submitted"});
    entries->verticalHeader()->setVisible(false);
    entries->setEditTriggers(QAbstractItemView::NoEditTriggers);
    entries->setSelectionMode(QAbstractItemView::NoSelection);
    entries->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    entriesLayout->addWidget(entries);
    lbPage->addWidget(entriesPane);
    tabs->addTab(lbPage, "Leaderboards");

    layout->addWidget(tabs, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);

    Manager* m = Manager::get();
    connect(m, &Manager::stateChanged, this, &AchievementsDialog::refresh);
    connect(m, &Manager::imagesUpdated, this, &AchievementsDialog::refreshIcons);
    connect(leaderboardTree, &QTreeWidget::itemSelectionChanged, this, &AchievementsDialog::onLeaderboardSelected);

    refresh();
}

QIcon AchievementsDialog::badgeIcon(const std::string& url, bool)
{
    QImage img = Manager::get()->images().get(url);
    return QIcon(roundedPixmap(img, 40));
}

void AchievementsDialog::refresh()
{
    Manager* m = Manager::get();
    rc_client_t* client = m->client();

    pendingIcons.clear();

    if (!m->isGameLoaded())
    {
        gameTitle->setText("No game loaded");
        if (!m->isEnabled())
            gameInfo->setText("RetroAchievements is disabled.");
        else if (!m->isLoggedIn())
            gameInfo->setText("Log in to RetroAchievements to earn achievements.");
        else
            gameInfo->setText("Start a game to see its achievements.");
        presence->clear();
        progress->hide();
        gameIcon->setPixmap(roundedPixmap(QImage(), 64));
        achievementTree->clear();
        leaderboardTree->clear();
        entries->setRowCount(0);
        return;
    }

    gameTitle->setText(m->gameTitle());
    gameIconUrl = m->gameBadgeUrl();
    gameIcon->setPixmap(roundedPixmap(m->images().get(gameIconUrl), 64));

    rc_client_user_game_summary_t summary;
    rc_client_get_user_game_summary(client, &summary);
    gameInfo->setText(QString("%1 - %2 of %3 points")
                          .arg(m->hardcoreStatusText())
                          .arg(summary.points_unlocked).arg(summary.points_available));
    QString rp = m->richPresence();
    presence->setText(rp.isEmpty() ? QString() : QString("<i>%1</i>").arg(rp.toHtmlEscaped()));

    if (summary.num_promoted_achievements)
    {
        progress->show();
        progress->setRange(0, (int)summary.num_promoted_achievements);
        progress->setValue((int)summary.num_unlocked_achievements);
        progress->setFormat(QString("%1 / %2 achievements").arg(summary.num_unlocked_achievements)
                                                           .arg(summary.num_promoted_achievements));
    }
    else
    {
        progress->hide();
    }

    fillAchievements();
    fillLeaderboards();
}

void AchievementsDialog::fillAchievements()
{
    Manager* m = Manager::get();
    rc_client_t* client = m->client();

    achievementTree->clear();

    int category = RC_CLIENT_ACHIEVEMENT_CATEGORY_PROMOTED;
    if (m->isUnpromotedEnabled()) category |= RC_CLIENT_ACHIEVEMENT_CATEGORY_UNPROMOTED;

    rc_client_subset_list_t* subsets = rc_client_create_subset_list(client);
    uint32_t numSubsets = subsets ? subsets->num_subsets : 0;

    auto addBuckets = [&](QTreeWidgetItem* parentItem, rc_client_achievement_list_t* list)
    {
        if (!list) return;
        for (uint32_t b = 0; b < list->num_buckets; b++)
        {
            const rc_client_achievement_bucket_t& bucket = list->buckets[b];
            auto* bucketItem = new QTreeWidgetItem();
            bucketItem->setText(0, QString("%1 (%2)").arg(QString::fromUtf8(bucket.label)).arg(bucket.num_achievements));
            QFont f = bucketItem->font(0);
            f.setBold(true);
            bucketItem->setFont(0, f);
            bucketItem->setFirstColumnSpanned(true);
            if (parentItem) parentItem->addChild(bucketItem);
            else achievementTree->addTopLevelItem(bucketItem);

            for (uint32_t i = 0; i < bucket.num_achievements; i++)
            {
                const rc_client_achievement_t* ach = bucket.achievements[i];
                bool unlocked = ach->unlocked != RC_CLIENT_ACHIEVEMENT_UNLOCKED_NONE;
                bool hardcore = m->isHardcorePreferred();
                bool unlockedInMode = hardcore ? (ach->unlocked & RC_CLIENT_ACHIEVEMENT_UNLOCKED_HARDCORE)
                                               : unlocked;

                std::string url;
                if (unlockedInMode) url = ach->badge_url ? ach->badge_url : "";
                else url = ach->badge_locked_url ? ach->badge_locked_url : "";

                auto* item = new QTreeWidgetItem();
                QString text = QString("%1\n%2").arg(QString::fromUtf8(ach->title))
                                                .arg(QString::fromUtf8(ach->description));
                item->setText(0, text);
                item->setToolTip(0, QString::fromUtf8(ach->description));
                item->setIcon(0, badgeIcon(url, !unlockedInMode));
                item->setText(1, QString::number(ach->points));
                item->setTextAlignment(1, Qt::AlignCenter);

                QString status;
                if (bucket.bucket_type == RC_CLIENT_ACHIEVEMENT_BUCKET_UNSUPPORTED)
                    status = "Unsupported";
                else if (unlocked)
                {
                    status = (ach->unlocked & RC_CLIENT_ACHIEVEMENT_UNLOCKED_HARDCORE) ? "Unlocked (hardcore)"
                                                                                       : "Unlocked (casual)";
                    if (ach->unlock_time)
                        status += "\n" + QDateTime::fromSecsSinceEpoch(ach->unlock_time)
                                             .toString("yyyy-MM-dd hh:mm");
                }
                else if (ach->measured_progress[0])
                    status = QString("%1 (%2%)").arg(QString::fromUtf8(ach->measured_progress))
                                                .arg((int)ach->measured_percent);
                else
                    status = "Locked";

                if (ach->rarity > 0.f)
                    status += QString("\n%1% of players").arg(hardcore ? ach->rarity_hardcore : ach->rarity, 0, 'f', 1);

                item->setText(2, status);
                if (unlockedInMode)
                    for (int c = 0; c < 3; c++) item->setForeground(c, QColor(0xD4, 0xA7, 0x2C));

                bucketItem->addChild(item);
                pendingIcons.emplace(url, item);
            }
            bucketItem->setExpanded(bucket.bucket_type != RC_CLIENT_ACHIEVEMENT_BUCKET_UNLOCKED ||
                                    bucket.num_achievements < 30);
        }
    };

    if (numSubsets <= 1)
    {
        rc_client_achievement_list_t* list = rc_client_create_achievement_list(
            client, category, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);
        addBuckets(nullptr, list);
        if (list) rc_client_destroy_achievement_list(list);
    }
    else
    {
        // multi-set: one top-level entry per achievement set (core + subsets)
        for (uint32_t s = 0; s < numSubsets; s++)
        {
            const rc_client_subset_t* subset = subsets->subsets[s];

            rc_client_user_game_summary_t ssum;
            rc_client_get_user_subset_summary(client, subset->id, &ssum);

            auto* setItem = new QTreeWidgetItem();
            setItem->setText(0, QString("%1 - %2/%3 unlocked")
                                    .arg(QString::fromUtf8(subset->title))
                                    .arg(ssum.num_unlocked_achievements)
                                    .arg(ssum.num_promoted_achievements));
            QFont f = setItem->font(0);
            f.setBold(true);
            f.setPointSizeF(f.pointSizeF() * 1.1);
            setItem->setFont(0, f);
            setItem->setFirstColumnSpanned(true);
            std::string badge = subset->badge_url ? subset->badge_url : "";
            setItem->setIcon(0, badgeIcon(badge, false));
            pendingIcons.emplace(badge, setItem);
            achievementTree->addTopLevelItem(setItem);

            rc_client_achievement_list_params_t params;
            params.subset_id = subset->id;
            params.grouping = RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS;
            params.category = (uint32_t)category;
            rc_client_achievement_list_t* list = rc_client_create_subset_achievement_list(client, &params);
            addBuckets(setItem, list);
            if (list) rc_client_destroy_achievement_list(list);
            setItem->setExpanded(true);
        }
    }

    if (subsets) rc_client_destroy_subset_list(subsets);

    if (achievementTree->topLevelItemCount() == 0)
    {
        auto* item = new QTreeWidgetItem();
        item->setText(0, "This game has no achievements.");
        achievementTree->addTopLevelItem(item);
    }
}

void AchievementsDialog::fillLeaderboards()
{
    rc_client_t* client = Manager::get()->client();

    uint32_t keep = selectedLeaderboard;
    leaderboardTree->blockSignals(true);
    leaderboardTree->clear();

    rc_client_leaderboard_list_t* list = rc_client_create_leaderboard_list(client, RC_CLIENT_LEADERBOARD_LIST_GROUPING_TRACKING);
    QTreeWidgetItem* reselect = nullptr;
    if (list)
    {
        for (uint32_t b = 0; b < list->num_buckets; b++)
        {
            const rc_client_leaderboard_bucket_t& bucket = list->buckets[b];
            auto* bucketItem = new QTreeWidgetItem();
            bucketItem->setText(0, QString("%1 (%2)").arg(QString::fromUtf8(bucket.label)).arg(bucket.num_leaderboards));
            QFont f = bucketItem->font(0);
            f.setBold(true);
            bucketItem->setFont(0, f);
            bucketItem->setFirstColumnSpanned(true);
            bucketItem->setFlags(Qt::ItemIsEnabled);
            leaderboardTree->addTopLevelItem(bucketItem);

            for (uint32_t i = 0; i < bucket.num_leaderboards; i++)
            {
                const rc_client_leaderboard_t* lb = bucket.leaderboards[i];
                auto* item = new QTreeWidgetItem();
                item->setText(0, QString("%1\n%2").arg(QString::fromUtf8(lb->title))
                                                  .arg(QString::fromUtf8(lb->description)));
                QString status;
                if (lb->state == RC_CLIENT_LEADERBOARD_STATE_TRACKING && lb->tracker_value)
                    status = QString("In progress: %1").arg(QString::fromUtf8(lb->tracker_value));
                else if (lb->state == RC_CLIENT_LEADERBOARD_STATE_DISABLED)
                    status = "Unsupported";
                item->setText(1, status);
                item->setData(0, Qt::UserRole, (uint)lb->id);
                bucketItem->addChild(item);
                if (lb->id == keep) reselect = item;
            }
            bucketItem->setExpanded(true);
        }
        rc_client_destroy_leaderboard_list(list);
    }

    if (leaderboardTree->topLevelItemCount() == 0)
    {
        auto* item = new QTreeWidgetItem();
        item->setText(0, "This game has no leaderboards.");
        item->setFlags(Qt::ItemIsEnabled);
        leaderboardTree->addTopLevelItem(item);
    }

    leaderboardTree->blockSignals(false);
    if (reselect) leaderboardTree->setCurrentItem(reselect);
}

void AchievementsDialog::refreshIcons()
{
    Manager* m = Manager::get();

    if (!gameIconUrl.empty())
        gameIcon->setPixmap(roundedPixmap(m->images().get(gameIconUrl), 64));

    for (auto it = pendingIcons.begin(); it != pendingIcons.end();)
    {
        QImage img = m->images().get(it->first);
        if (!img.isNull())
        {
            it->second->setIcon(0, QIcon(roundedPixmap(img, 40)));
            it = pendingIcons.erase(it);
        }
        else
            ++it;
    }
}

namespace
{
struct EntryRow
{
    uint32_t rank;
    QString user;
    QString display;
    qint64 submitted;
    bool isUser;
};

struct EntriesRequest
{
    QPointer<AchievementsDialog> dialog;
    unsigned serial;
};
}

void AchievementsDialog::onLeaderboardSelected()
{
    QTreeWidgetItem* item = leaderboardTree->currentItem();
    if (!item) return;
    QVariant id = item->data(0, Qt::UserRole);
    if (!id.isValid()) return;

    Manager* m = Manager::get();
    selectedLeaderboard = id.toUInt();
    unsigned serial = ++requestSerial;

    entries->setRowCount(0);
    entriesStatus->setText("Loading rankings...");

    auto* req = new EntriesRequest{this, serial};

    auto callback = [](int result, const char* error, rc_client_leaderboard_entry_list_t* list,
                       rc_client_t* client, void* userdata)
    {
        // HTTP worker thread: copy the data, then hand it to the GUI thread
        auto* req = static_cast<EntriesRequest*>(userdata);
        std::vector<EntryRow> rows;
        QString err;
        int total = 0;

        if (result == RC_OK && list)
        {
            total = (int)list->total_entries;
            for (uint32_t i = 0; i < list->num_entries; i++)
            {
                const rc_client_leaderboard_entry_t& e = list->entries[i];
                rows.push_back({e.rank, QString::fromUtf8(e.user), QString::fromUtf8(e.display),
                                (qint64)e.submitted, list->user_index == (int32_t)i});
            }
        }
        else
        {
            err = QString::fromUtf8(error ? error : rc_error_str(result));
        }
        if (list) rc_client_destroy_leaderboard_entry_list(list);

        QPointer<AchievementsDialog> dlg = req->dialog;
        unsigned serial = req->serial;
        delete req;

        QMetaObject::invokeMethod(qApp, [dlg, serial, rows, err, total]()
        {
            if (!dlg || dlg->requestSerial != serial) return;

            if (!err.isEmpty())
            {
                dlg->entriesStatus->setText(QString("Failed to load rankings: %1").arg(err));
                return;
            }

            dlg->entriesStatus->setText(rows.empty() ? QString("No entries yet.")
                                                     : QString("%1 entries").arg(total));
            dlg->entries->setRowCount((int)rows.size());
            for (int r = 0; r < (int)rows.size(); r++)
            {
                const EntryRow& row = rows[r];
                QTableWidgetItem* cells[4] = {
                    new QTableWidgetItem(QString::number(row.rank)),
                    new QTableWidgetItem(row.user),
                    new QTableWidgetItem(row.display),
                    new QTableWidgetItem(row.submitted ? QDateTime::fromSecsSinceEpoch(row.submitted)
                                                             .toString("yyyy-MM-dd") : QString()),
                };
                for (int c = 0; c < 4; c++)
                {
                    if (row.isUser)
                    {
                        QFont f = cells[c]->font();
                        f.setBold(true);
                        cells[c]->setFont(f);
                    }
                    dlg->entries->setItem(r, c, cells[c]);
                }
            }
            dlg->entries->resizeColumnToContents(0);
            dlg->entries->resizeColumnToContents(2);
            dlg->entries->resizeColumnToContents(3);
        }, Qt::QueuedConnection);
    };

    // show the area around the player if they have an entry, else the top
    if (m->isLoggedIn())
        rc_client_begin_fetch_leaderboard_entries_around_user(m->client(), selectedLeaderboard, 25, callback, req);
    else
        rc_client_begin_fetch_leaderboard_entries(m->client(), selectedLeaderboard, 1, 25, callback, req);
}

}
