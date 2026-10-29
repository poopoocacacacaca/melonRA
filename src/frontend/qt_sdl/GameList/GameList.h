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
#include <map>
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
class QProgressBar;
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
    qint64 modified = 0; // last modification, ms since the epoch
    bool notRom = false; // a file that looked like a ROM but isn't one (only kept to skip it next time)

    QString title;      // from the ROM's banner, in the system language if it has one
    QString subtitle;   // middle line of a three-line banner title
    QString developer;  // last line of the banner title, or from the maker code
    QString gameCode;   // e.g. "ADAE"
    QString region;     // e.g. "USA", "Spain"
    QString regionFlag; // flag image(s), e.g. "us" or "us+eu"; empty if none
    QString platform;   // "DS", "DSi Enhanced", "DSi"
    QImage icon;        // the 32x32 icon from the banner
};

// The id its per-game settings are kept under.
QString settingsId(const Game& game);

// Reads a DS ROM's header and banner. Returns false if it isn't one.
bool readGame(const QString& path, Game& game);
// Same, for a ROM already in memory (`fileName` is only used as a last-resort title).
bool readGame(const QByteArray& rom, const QString& fileName, Game& game);

// Finds every DS ROM in the given folders. Entries in `known` whose file
// hasn't changed (same size and modification time) are reused instead of
// being read again; `read` receives how many files had to be read. Files
// that turned out not to be ROMs go to `rejected` (with notRom set), so they
// can be passed back in `known` and skipped next time.
std::vector<Game> scanFolders(const QStringList& folders, bool recursive, const std::atomic<bool>& cancel,
                              const std::map<QString, Game>* known = nullptr, int* read = nullptr,
                              std::vector<Game>* rejected = nullptr);

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
// a star in front of games mastered on RetroAchievements
bool showMasteryStars();
void setShowMasteryStars(bool show);
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
        Col_Achievements,
        NumColumns
    };
    // value used for sorting a column
    static constexpr int SortRole = Qt::UserRole;
    // full path of the row's file
    static constexpr int PathRole = Qt::UserRole + 1;
    // the box art (QImage; null until there is one), for the grid
    static constexpr int CoverRole = Qt::UserRole + 2;
    // identifies the cover (QString), so drawn covers can be cached
    static constexpr int CoverKeyRole = Qt::UserRole + 3;
    // whether the game has its own settings (bool), for the gear marker
    static constexpr int HasSettingsRole = Qt::UserRole + 4;
    // RetroAchievements progress (Progress, see below)
    static constexpr int ProgressRole = Qt::UserRole + 5;
    // mastered on RetroAchievements and stars are shown (bool)
    static constexpr int StarRole = Qt::UserRole + 6;

    // the player's RetroAchievements progress for a game
    struct Progress
    {
        enum State { Unknown, NotOnRA, Known } state = Unknown;
        uint32_t total = 0;      // 0: on RetroAchievements but no achievements yet
        uint32_t unlocked = 0;   // in either mode
        uint32_t hardcore = 0;   // in Hardcore mode
        bool mastered() const { return state == Known && total && hardcore >= total; }
        bool completed() const { return state == Known && total && unlocked >= total; }
    };

    explicit Model(QObject* parent = nullptr);

    void setGames(std::vector<Game> games);
    // region column: flag and name, or just the flag
    void setShowRegionNames(bool show);
    bool showsRegionNames() const { return regionNames; }
    // vector flags (large windows, high-DPI screens) or the pixel-art ones;
    // `scale` is the screen's device pixel ratio
    void setFlagStyle(bool highRes, qreal scale);
    const std::vector<Game>& games() const { return list; }
    // looks again at which games have their own settings
    void refreshSettingsMarks();
    // RetroAchievements progress, by ROM path (empty: none known)
    void setProgress(const QHash<QString, Progress>& byPath);
    void setShowStars(bool show);

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
    std::vector<QString> sizeTexts;
    std::vector<bool> hasSettings;
    QHash<QString, Progress> progress;
    bool stars = true;
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

    QSize minimumSizeHint() const override { return QSize(200, 120); }

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void bootRequested(const QString& path);
    // a DSiWare title, which can't be started like a cartridge
    void dsiWareRequested(const QString& path, const QString& title);
    // Game Settings... for a game; `id` is its Config::GameSettingsId
    void gameSettingsRequested(const QString& id, const QString& title, const QIcon& icon, const QString& path);
    // the game folders are set in Path Settings, which the window opens
    void settingsRequested();
    // Achievements... for a game: the achievements window, on that game
    void achievementsRequested(const QString& path);
    // Start Netplay... for a game: host a netplay room for it
    void netplayRequested(const QString& path);

public:
    // after a game's settings changed
    void refreshSettingsMarks();

public slots:
    void openSettings();

private:
    void scanFinished(int serial, std::vector<Game> games, bool changed);
    void updatePage();
    void showContextMenu(QAbstractItemView* from, const QPoint& pos);
    void setGridMode(bool grid);
    void setTileSize(int size);
    // in grid view: download missing covers, asking the first time
    void maybeDownloadCovers();
    void showHeaderMenu(const QPoint& pos);
    void bootIndex(const QModelIndex& index);
    void saveHeader();
    void restoreHeader();
    void defaultColumns();
    void updateFlagStyle();

    void setSearchBarVisible(bool visible);
    void addViewOptions(QMenu& menu);

    // RetroAchievements progress, for the Achievements column and the stars
    bool wantsAchievements() const;
    void updateAchievements();
    void pushProgress();
    bool raGameWasLoaded = false;

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
    QProgressBar* coverProgress;
    QToolButton* coverStop;
    bool askedAboutCovers = false;
    Model* model;
    QSortFilterProxyModel* proxy;

    // shows the list saved by the last scan, while the folders are checked
    bool showSavedList();
    bool savedListShown = false;
    // files in the game folders that aren't ROMs, from the last scan
    std::vector<Game> rejectedFiles;

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

Q_DECLARE_METATYPE(GameList::Model::Progress)

#endif // GAMELIST_H
