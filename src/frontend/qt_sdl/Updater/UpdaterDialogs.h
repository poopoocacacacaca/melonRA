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

#ifndef UPDATERDIALOGS_H
#define UPDATERDIALOGS_H

#include <QDialog>

#include "Updater.h"

class QCheckBox;
class QLabel;
class QRadioButton;

namespace Updater
{

// Runs at startup, before any window opens. With "Always update to the latest
// version" on, downloads and installs a newer build if there is one, and
// starts it. Returns true if it did, in which case this copy should exit.
bool runAutoUpdate();

// Runs once the main window is open: checks in the background and asks the
// user if there's an update, unless they turned that off.
void startupCheck(QWidget* window);

// Stops a background check that is still running. Call before exiting.
void shutdown();

// "An update is available" prompt
class UpdatePromptDialog : public QDialog
{
    Q_OBJECT

public:
    enum Choice { UpdateNow, Skip, NotNow };

    // `showNeverAsk`: offer the "Never ask again" checkbox (for the automatic check)
    UpdatePromptDialog(const Release& release, bool showNeverAsk, QWidget* parent);

    Choice choice() const { return userChoice; }
    bool neverAskAgain() const;

private:
    Choice userChoice = NotNow;
    QCheckBox* cbNeverAsk = nullptr;
};

// Config → Update Settings
class UpdateSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UpdateSettingsDialog(QWidget* parent);

private:
    void updateEnabledStates();
    void save();
    void checkNow();

    QRadioButton* rbStable;
    QRadioButton* rbNightly;
    QCheckBox* cbAutoUpdate;
    QCheckBox* cbNeverAsk;
    QLabel* neverAskNote;
};

}

#endif // UPDATERDIALOGS_H
