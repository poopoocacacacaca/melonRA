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

#ifndef SETTINGSWINDOW_H
#define SETTINGSWINDOW_H

#include <QDialog>
#include <QPointer>

class QListWidget;
class QScrollArea;
class QStackedWidget;
class MainWindow;

// All the settings in one window: a list of pages on the left, the page on
// the right. Each page is one of the settings dialogs, shown inside the
// window; it is created the first time it is opened. OK saves every page
// that was opened, Cancel (or closing the window) reverts them.
class SettingsWindow : public QDialog
{
    Q_OBJECT

public:
    enum Page
    {
        Page_Emulation,
        Page_Input,
        Page_Video,
        Page_Camera,
        Page_Audio,
        Page_Multiplayer,
        Page_WiFi,
        Page_Firmware,
        Page_Interface,
        Page_Paths,
        Page_Updates,
        Page_Achievements,
        Page_Count
    };

    // opens the window on a page (or switches to it if it's already open)
    static void open(MainWindow* win, Page page);
    static SettingsWindow* current() { return Current; }

    void showPage(Page page);

    void accept() override;
    void reject() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    SettingsWindow(MainWindow* win);
    ~SettingsWindow() override;

    void updateIcons();

    struct PageSlot
    {
        QScrollArea* Holder = nullptr;
        QPointer<QDialog> Dialog;
        bool Finished = false;
        bool Available = true;
    };

    static SettingsWindow* Current;

    MainWindow* Win;
    QListWidget* List;
    QStackedWidget* Stack;
    PageSlot Pages[Page_Count];
    bool Closing = false;
};

#endif // SETTINGSWINDOW_H
