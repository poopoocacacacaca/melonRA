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

#include "SettingsBackup.h"

#include <string>

#include <QApplication>
#include <QCheckBox>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "Config.h"
#include "version.h"
#include "Updater/Updater.h"

namespace SettingsBackup
{

namespace
{
QString startFolder()
{
    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return docs.isEmpty() ? QDir::homePath() : docs;
}
}

void backUp(QWidget* parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle("Back Up Settings - melonRA");
    auto* layout = new QVBoxLayout(&dlg);

    auto* intro = new QLabel("Saves all of melonRA's settings to one file: emulation, video, audio, controls "
                             "and hotkeys, RetroAchievements, the overlay and the game list. Restore it later "
                             "from Config → Restore Settings, here or on another computer.");
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* paths = new QCheckBox("Include file and folder locations");
    paths->setChecked(true);
    paths->setToolTip("BIOS and firmware files, game folders, save and savestate folders and so on.\n"
                      "Leave this off when moving to a computer where they're somewhere else.");
    layout->addWidget(paths);

    auto* login = new QCheckBox("Include my RetroAchievements login");
    login->setToolTip("Your username and login token (never your password), so you stay logged in after "
                      "restoring.\nAnyone with the file could use your account, so keep it private.");
    layout->addWidget(login);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    buttons->addButton("Save Backup...", QDialogButtonBox::AcceptRole);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);
    dlg.setMinimumWidth(420);

    if (dlg.exec() != QDialog::Accepted) return;

    const QString suggested = QDir(startFolder()).filePath(
        QString("melonRA-settings-%1.toml").arg(QDate::currentDate().toString(Qt::ISODate)));
    QString file = QFileDialog::getSaveFileName(parent, "Save Settings Backup", suggested,
                                                "melonRA settings (*.toml)");
    if (file.isEmpty()) return;
    if (!file.endsWith(".toml", Qt::CaseInsensitive)) file += ".toml";

    const std::string header =
        "# melonRA settings backup, made with melonRA " MELONRA_VERSION " on " +
        QDate::currentDate().toString(Qt::ISODate).toStdString() +
        "\n# Restore it from Config -> Restore Settings...\n";

    std::string error;
    if (!Config::ExportBackup(QDir::toNativeSeparators(file).toStdString(), paths->isChecked(),
                              login->isChecked(), header, error))
    {
        QMessageBox::warning(parent, "Back Up Settings - melonRA",
                             QString("The backup couldn't be saved.\n\n%1").arg(QString::fromStdString(error)));
        return;
    }

    QMessageBox::information(parent, "Back Up Settings - melonRA",
                             QString("Settings saved to:\n%1").arg(QDir::toNativeSeparators(file)));
}

void restore(QWidget* parent)
{
    QString file = QFileDialog::getOpenFileName(parent, "Restore Settings", startFolder(),
                                                "melonRA settings (*.toml);;All files (*)");
    if (file.isEmpty()) return;

    QMessageBox confirm(QMessageBox::Question, "Restore Settings - melonRA",
                        "Replace your settings with the ones in this backup?", QMessageBox::NoButton, parent);
    confirm.setInformativeText("melonRA restarts to apply them. Anything the backup leaves out (file "
                               "locations or your RetroAchievements login) stays as it is now.\n\n"
                               "Your current settings are kept as melonRA.toml.bak in the melonRA folder.");
    QPushButton* ok = confirm.addButton("Restore and Restart", QMessageBox::AcceptRole);
    confirm.addButton(QMessageBox::Cancel);
    confirm.setDefaultButton(ok);
    confirm.exec();
    if (confirm.clickedButton() != ok) return;

    // what's in memory now goes to the .bak copy
    Config::Save();

    std::string error;
    if (!Config::RestoreBackup(QDir::toNativeSeparators(file).toStdString(), error))
    {
        QMessageBox::warning(parent, "Restore Settings - melonRA",
                             QString("The settings couldn't be restored.\n\n%1").arg(QString::fromStdString(error)));
        return;
    }

    // main() starts melonRA again once everything has shut down
    Updater::requestRelaunch(true);
    Updater::allowForegroundHandoff();
    QApplication::closeAllWindows();

    // a window refused to close: the settings are in place for the next start
    for (QWidget* w : QApplication::topLevelWidgets())
    {
        if (w->isVisible() && w->isWindow() && !qobject_cast<QDialog*>(w))
        {
            Updater::requestRelaunch(false);
            QMessageBox::information(parent, "Restore Settings - melonRA",
                                     "The settings were restored. Restart melonRA to use them.");
            break;
        }
    }
}

}
