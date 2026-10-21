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

#ifndef RA_DIALOGS_H
#define RA_DIALOGS_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include <QDialog>
#include <QFrame>
#include <QIcon>
#include <QObject>
#include <QTimer>

#include "RAOverlay.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSoundEffect;
class QTabWidget;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace RA
{

// Plays the unlock/mastery/leaderboard sounds requested by the Manager.
class SoundPlayer : public QObject
{
    Q_OBJECT
public:
    explicit SoundPlayer(QObject* parent = nullptr);

public slots:
    void play(int sound);

private:
    QSoundEffect* effects[3] = {};
};


class LoginDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LoginDialog(QWidget* parent);

    // opens the dialog (or focuses the existing one)
    static void open(QWidget* parent);

private slots:
    void onLogin();
    void onLoginFinished(bool success, QString message);

private:
    QLineEdit* username;
    QLineEdit* password;
    QPushButton* loginButton;
    QLabel* status;

    static LoginDialog* current;
};


// A stand-in game screen showing the overlay with sample content, for the
// Appearance tab of the settings
class OverlayPreview : public QFrame
{
    Q_OBJECT
public:
    explicit OverlayPreview(QWidget* parent = nullptr);

    void setStyle(const Overlay::Style& style);
    void setOptions(bool popups, bool challenges, bool progress, bool trackers);
    Overlay& overlay() { return *ovl; }

    QSize sizeHint() const override { return QSize(520, 300); }
    QSize minimumSizeHint() const override { return QSize(320, 200); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::unique_ptr<Overlay> ovl;
    QTimer timer;
    int tick = 0;
};


class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent);
    static void open(QWidget* parent);

private slots:
    void refresh();

private:
    QWidget* createGeneralTab();
    QWidget* createAppearanceTab();
    // reads the Appearance tab's controls
    Overlay::Style styleFromControls() const;
    void showStyle(const Overlay::Style& style);
    void applyStyle();
    void showTestNotifications();

    QLabel* avatar;
    QLabel* account;
    QPushButton* loginButton;
    QPushButton* logoutButton;

    QCheckBox* cbEnabled;
    QCheckBox* cbHardcore;
    QCheckBox* cbEncore;
    QCheckBox* cbUnpromoted;
    QCheckBox* cbSpectator;
    QCheckBox* cbOffline;
    QCheckBox* cbToolkit;
    QPushButton* toolkitDownload;
    QLabel* toolkitStatus;

    QCheckBox* cbNotifications;
    QCheckBox* cbAccountOnStart;
    QCheckBox* cbLeaderboardNotifications;
    QCheckBox* cbChallenges;
    QCheckBox* cbProgress;
    QCheckBox* cbTrackers;
    QCheckBox* cbSounds;

    // Appearance tab, one row per Overlay::Element
    OverlayPreview* preview;
    QLabel* styleLabel[Overlay::kNumElements];
    QComboBox* stylePosition[Overlay::kNumElements];
    QComboBox* styleCorners[Overlay::kNumElements];
    QCheckBox* styleBold[Overlay::kNumElements];
    QCheckBox* styleBorder[Overlay::kNumElements];
    QComboBox* styleSize;
    QComboBox* styleColors;

    bool updating = false;

    static SettingsDialog* current;
};


class AchievementsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AchievementsDialog(QWidget* parent);
    static void open(QWidget* parent, bool leaderboards = false);

private slots:
    void refresh();
    void refreshIcons();
    void onLeaderboardSelected();

private:
    void fillAchievements();
    void fillLeaderboards();
    QIcon badgeIcon(const std::string& url, bool locked);

    // the sidebar: the game being played, then the game list's games
    void onStateChanged();
    void rebuildSidebar();
    void onSidebarSelection();
    void showLibraryGame(uint32_t gameId);
    void fillLibraryDetails(uint32_t gameId);
    void clearGameView(const QString& title, const QString& info);

    QListWidget* sidebar;
    QLabel* sidebarStatus;
    // true while the right side shows a game from the list rather than the one being played
    bool showingLibrary = false;
    uint32_t shownLibraryGame = 0;
    bool rebuilding = false;

    QLabel* gameIcon;
    QLabel* gameTitle;
    QLabel* gameInfo;
    QLabel* presence;
    QProgressBar* progress;

    QTabWidget* tabs;
    QTreeWidget* achievementTree;
    QTreeWidget* leaderboardTree;
    QTableWidget* entries;
    QLabel* entriesStatus;

    std::string gameIconUrl;
    // items waiting for their badge to download: item -> (url, locked)
    std::multimap<std::string, QTreeWidgetItem*> pendingIcons;
    uint32_t selectedLeaderboard = 0;
    unsigned requestSerial = 0;

    static AchievementsDialog* current;
};

}

#endif // RA_DIALOGS_H
