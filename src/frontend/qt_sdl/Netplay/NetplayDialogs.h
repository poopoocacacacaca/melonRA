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

#ifndef NETPLAYDIALOGS_H
#define NETPLAYDIALOGS_H

#include <QDialog>
#include <QPointer>
#include <QStringList>

#include "NetplaySession.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class MainWindow;

namespace Netplay
{
class Runner;

// Experimental → Host Netplay Game...
class HostDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HostDialog(MainWindow* parent);

    // picks this game (from the game list, or adds it)
    void selectGame(const QString& path);

    QStringList romPath() const { return ROMPath; }
    QString name() const;
    u16 port() const;
    int inputDelay() const;

private slots:
    void browse();
    void accept() override;

private:
    MainWindow* Win;
    QLineEdit* Name;
    QComboBox* Game;
    QSpinBox* Port;
    QSpinBox* Delay;
    QStringList ROMPath;
    QStringList Paths;  // the game list's paths, by combo index
};

// Experimental → Join Netplay Game...
class JoinDialog : public QDialog
{
    Q_OBJECT

public:
    explicit JoinDialog(QWidget* parent);

    QString name() const;
    QString address() const;
    u16 port() const;

private slots:
    void accept() override;

private:
    QLineEdit* Name;
    QLineEdit* Address;
    QSpinBox* Port;
};

// The room: who's in it, and starting or leaving. Stays open (hidden while
// the game runs) for as long as the session does.
class RoomDialog : public QDialog
{
    Q_OBJECT

public:
    static RoomDialog* open(MainWindow* win, Runner* runner);
    static RoomDialog* current() { return Current; }

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void refresh();
    void onNotice(const QString& text);
    void onNeedROM(const Netplay::RoomInfo& room);
    void onGo();
    void onDesync(int frame, int console);
    void onFinished(const QString& reason);

private:
    RoomDialog(MainWindow* win, Runner* runner);
    ~RoomDialog() override;

    static RoomDialog* Current;

    MainWindow* Win;
    QPointer<Runner> Run;
    QLabel* Game;
    QLabel* Status;
    QLabel* Note;
    QListWidget* Players;
    QPushButton* StartBtn;
    QPushButton* LeaveBtn;
    bool Closing = false;
};

}

#endif // NETPLAYDIALOGS_H
