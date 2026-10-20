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
#include <string>

#include <QDialog>
#include <QIcon>
#include <QObject>

class QCheckBox;
class QLabel;
class QLineEdit;
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


class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent);
    static void open(QWidget* parent);

private slots:
    void refresh();

private:
    QLabel* avatar;
    QLabel* account;
    QPushButton* loginButton;
    QPushButton* logoutButton;

    QCheckBox* cbEnabled;
    QCheckBox* cbHardcore;
    QCheckBox* cbEncore;
    QCheckBox* cbUnpromoted;
    QCheckBox* cbSpectator;
    QCheckBox* cbToolkit;
    QPushButton* toolkitDownload;
    QLabel* toolkitStatus;

    QCheckBox* cbNotifications;
    QCheckBox* cbLeaderboardNotifications;
    QCheckBox* cbChallenges;
    QCheckBox* cbProgress;
    QCheckBox* cbTrackers;
    QCheckBox* cbSounds;

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
