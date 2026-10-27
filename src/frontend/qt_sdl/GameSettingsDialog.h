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

#ifndef GAMESETTINGSDIALOG_H
#define GAMESETTINGSDIALOG_H

#include <memory>
#include <string>
#include <vector>

#include <QDialog>
#include <QIcon>

namespace melonDS { class ARCodeFile; }
class QComboBox;
class QLineEdit;

// Game Settings, from the game list: settings that replace the usual ones
// while one game runs. Every setting starts at "Global" (follow the
// emulator); it can instead have its own value, and on/off settings can be
// turned on or off for this game alone.
class GameSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    // `romPath` locates the game's cheat file; `running` if it's the game
    // being played (its cheat list is then edited in Set Up Cheat Codes instead)
    GameSettingsDialog(QWidget* parent, const std::string& gameId, const QString& title, const QIcon& icon,
                       const QString& romPath, bool running);
    ~GameSettingsDialog() override;

signals:
    // the game's settings were saved; `id` is the game they belong to
    void settingsChanged(const QString& id);

private:
    struct Row
    {
        std::string key;  // full config path
        QComboBox* combo;
    };

    void addGroup(class QTabWidget* tabs, const QString& name, const std::vector<int>& settings);
    void save();
    void resetAll();
    void updateWifiControls();
    void addCheatsTab(class QTabWidget* tabs);
    void saveCheats();

    std::string gameId;
    std::vector<Row> rows;
    QComboBox* wifiServer = nullptr;
    QLineEdit* wifiCustom = nullptr;

    QString romPath;
    bool running = false;
    std::unique_ptr<melonDS::ARCodeFile> cheats;
    class QTreeWidget* cheatTree = nullptr;
    bool cheatsEdited = false;
};

#endif // GAMESETTINGSDIALOG_H
