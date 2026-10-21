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
class QListView;
class QListWidget;
class QSlider;
class QToolButton;
class QAbstractItemView;
class QMenu;
class QSortFilterProxyModel;
class QStackedWidget;
class QThread;
class QTreeView;

// The list of games shown in the main window while nothing is running, built
// from the game folders chosen in Path Settings.
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

// The games found by the main window's last scan, for other windows (the
// RetroAchievements list uses them). notifier() signals when they change.
std::vector<Game> scannedGames();
class Notifier : public QObject
{
    Q_OBJECT
signals:
    void gamesChanged();
};
Notifier* notifier();

// settings, in the global config
QStringList folders();
void setFolders(const QStringList& folders);
bool searchSubfolders();
void setSearchSubfolders(bool recursive);
bool isEnabled();
void setEnabled(bool enabled);
bool showRegionNames();
void setShowRegionNames(bool show);
bool showSearchBar();
void setShowSearchBar(bool show);
bool gridMode();
void setGridMode(bool grid);
int tileSize();
void setTileSize(int size);


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
    // the box art (QImage; null until there is one), for the grid
    static constexpr int CoverRole = Qt::UserRole + 2;

    explicit Model(QObject* parent = nullptr);

    void setGames(std::vector<Game> games);
    // region column: flag and name, or just the flag
    void setShowRegionNames(bool show);
    bool showsRegionNames() const { return regionNames; }
    // vector flags (large windows, high-DPI screens) or the pixel-art ones;
    // `scale` is the screen's device pixel ratio
    void setFlagStyle(bool highRes, qreal scale);
    const std::vector<Game>& games() const { return list; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    // a pixmap rather than a QIcon, which would lose its device pixel ratio
    QPixmap flagIcon(const QString& key) const;
    QPixmap flagPixmap(const QString& code) const;

    std::vector<Game> list;
    std::vector<QIcon> icons;
    std::vector<QString> coverKeys;
    mutable QHash<QString, QPixmap> flags;
    bool regionNames = true;
    bool highResFlags = false;
    qreal flagScale = 1.0;
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

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void bootRequested(const QString& path);
    // the game folders are set in Path Settings, which the window opens
    void settingsRequested();

public slots:
    void openSettings();

private:
    void scanFinished(int serial, std::vector<Game> games);
    void updatePage();
    void showContextMenu(QAbstractItemView* from, const QPoint& pos);
    void setGridMode(bool grid);
    void setTileSize(int size);
    void showHeaderMenu(const QPoint& pos);
    void bootIndex(const QModelIndex& index);
    void saveHeader();
    void restoreHeader();
    void defaultColumns();
    void updateFlagStyle();

    void setSearchBarVisible(bool visible);
    void addViewOptions(QMenu& menu);

    QStackedWidget* pages;
    QWidget* searchRow;
    QWidget* listPage;
    QWidget* emptyPage;
    QLabel* emptyText;
    QLineEdit* search;
    QLabel* status;
    QTreeView* view;
    QListView* grid;
    QStackedWidget* views;
    QToolButton* listButton;
    QToolButton* gridButton;
    QSlider* sizeSlider;
    QLabel* sizeLabel;
    Model* model;
    QSortFilterProxyModel* proxy;

    QPointer<QThread> scanThread;
    std::shared_ptr<std::atomic<bool>> scanCancel;
    int scanSerial = 0;
    bool scanning = false;
};


// The game folders list with Add/Remove and "Search subfolders", shown in
// Path Settings
class FoldersEditor : public QWidget
{
    Q_OBJECT
public:
    explicit FoldersEditor(QWidget* parent = nullptr);

    // stores the folders; returns true if anything changed
    bool save();

private:
    void addFolder();

    QListWidget* folderList;
    QCheckBox* cbRecursive;
};

}

#endif // GAMELIST_H
