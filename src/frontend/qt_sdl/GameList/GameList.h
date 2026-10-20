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

#ifndef GAMELIST_H
#define GAMELIST_H

#include <atomic>
#include <memory>
#include <vector>

#include <QAbstractTableModel>
#include <QHash>
#include <QDialog>
#include <QIcon>
#include <QImage>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSortFilterProxyModel;
class QStackedWidget;
class QThread;
class QTreeView;

// The list of games shown in the main window while nothing is running, built
// from the folders chosen in Game List Settings.
namespace GameList
{

struct Game
{
    QString path;
    QString fileName;
    qint64 size = 0;

    QString title;      // from the ROM's banner, in the system language if it has one
    QString subtitle;   // middle line of a three-line banner title
    QString developer;  // last line of the banner title, or from the maker code
    QString gameCode;   // e.g. "ADAE"
    QString region;     // e.g. "USA", "Spain"
    QString regionFlag; // flag image(s), e.g. "us" or "us+eu"; empty if none
    QString platform;   // "DS", "DSi Enhanced", "DSi"
    QImage icon;        // the 32x32 icon from the banner
};

// Reads a DS ROM's header and banner. Returns false if it isn't one.
bool readGame(const QString& path, Game& game);

// Finds every DS ROM in the given folders.
std::vector<Game> scanFolders(const QStringList& folders, bool recursive, const std::atomic<bool>& cancel);

// settings, in the global config
QStringList folders();
void setFolders(const QStringList& folders);
bool searchSubfolders();
void setSearchSubfolders(bool recursive);
bool isEnabled();
void setEnabled(bool enabled);


class Model : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column
    {
        Col_Title,
        Col_Developer,
        Col_Region,
        Col_GameCode,
        Col_Platform,
        Col_Size,
        Col_FileName,
        NumColumns
    };
    // value used for sorting a column
    static constexpr int SortRole = Qt::UserRole;
    // full path of the row's file
    static constexpr int PathRole = Qt::UserRole + 1;

    explicit Model(QObject* parent = nullptr);

    void setGames(std::vector<Game> games);
    const std::vector<Game>& games() const { return list; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QIcon flagIcon(const QString& key) const;

    std::vector<Game> list;
    std::vector<QIcon> icons;
    mutable QHash<QString, QIcon> flags;
};


class Widget : public QWidget
{
    Q_OBJECT
public:
    explicit Widget(QWidget* parent = nullptr);
    ~Widget() override;

    // scans the folders again
    void refresh();
    int gameCount() const;

    QSize minimumSizeHint() const override { return QSize(200, 120); }

signals:
    void bootRequested(const QString& path);

public slots:
    void openSettings();

private:
    void scanFinished(int serial, std::vector<Game> games);
    void updatePage();
    void showContextMenu(const QPoint& pos);
    void showHeaderMenu(const QPoint& pos);
    void bootIndex(const QModelIndex& index);
    void saveHeader();
    void restoreHeader();

    QStackedWidget* pages;
    QWidget* listPage;
    QWidget* emptyPage;
    QLabel* emptyText;
    QLineEdit* search;
    QLabel* status;
    QTreeView* view;
    Model* model;
    QSortFilterProxyModel* proxy;

    QPointer<QThread> scanThread;
    std::shared_ptr<std::atomic<bool>> scanCancel;
    int scanSerial = 0;
    bool scanning = false;
};


// Config → Game List Settings
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent);

    // shows the dialog; returns true if the settings were changed
    static bool edit(QWidget* parent);

private:
    void addFolder();
    void removeFolder();
    void save();

    QListWidget* folderList;
    QCheckBox* cbRecursive;
    QCheckBox* cbEnabled;
};

}

#endif // GAMELIST_H
