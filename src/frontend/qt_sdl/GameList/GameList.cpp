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

#include "GameList.h"

#include <algorithm>
#include <set>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QShowEvent>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QThread>
#include <QTimer>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

#include "Config.h"

namespace GameList
{

// ---------------------------------------------------------------------------
// Reading ROMs

namespace
{

// offsets in the cartridge header
constexpr int kHeaderSize = 0x200;
constexpr int kHdrTitle = 0x000;       // 12 chars
constexpr int kHdrGameCode = 0x00C;    // 4 chars
constexpr int kHdrMakerCode = 0x010;   // 2 chars
constexpr int kHdrUnitCode = 0x012;    // 0 = DS, 2 = DSi enhanced, 3 = DSi only
constexpr int kHdrRegion = 0x01D;      // 0x40 = Korea, 0x80 = China
constexpr int kHdrBannerOffset = 0x068;

// offsets in the banner
constexpr int kBnrIcon = 0x020;        // 32x32, 4bpp, 8x8 tiles
constexpr int kBnrPalette = 0x220;     // 16 BGR555 colors, 0 is transparent
constexpr int kBnrTitles = 0x240;      // 128 UTF-16 chars per language
constexpr int kBnrTitleSize = 0x100;
constexpr int kBnrReadSize = kBnrTitles + 8 * kBnrTitleSize;

// languages in the banner, in order
enum BannerLanguage { Lang_Japanese, Lang_English, Lang_French, Lang_German,
                      Lang_Italian, Lang_Spanish, Lang_Chinese, Lang_Korean };

struct Region
{
    char code;
    const char* name;
    const char* flag;
};

// last letter of the game code
const Region kRegions[] = {
    {'J', "Japan", "jp"},
    {'E', "USA", "us"},
    {'L', "USA", "us"},
    {'P', "Europe", "eu"},
    {'W', "Europe", "eu"},
    {'X', "Europe", "eu"},
    {'Y', "Europe", "eu"},
    {'Z', "Europe", "eu"},
    {'O', "USA/Europe", "us+eu"},
    {'T', "USA/Australia", "us+au"},
    {'V', "Europe/Australia", "eu+au"},
    {'U', "Australia", "au"},
    {'K', "Korea", "kr"},
    {'C', "China", "cn"},
    {'D', "Germany", "de"},
    {'F', "France", "fr"},
    {'I', "Italy", "it"},
    {'S', "Spain", "es"},
    {'H', "Netherlands", "nl"},
    {'R', "Russia", "ru"},
    {'Q', "Denmark", "dk"},
    {'M', "Sweden", "se"},
    {'N', "Norway", "no"},
    {'A', "Region Free", ""},
};

// publishers by maker code, for ROMs whose banner doesn't name one
struct Maker
{
    const char* code;
    const char* name;
};
const Maker kMakers[] = {
    {"01", "Nintendo"}, {"08", "Capcom"}, {"18", "Hudson Soft"}, {"41", "Ubisoft"},
    {"4F", "Eidos"}, {"4Q", "Disney Interactive"}, {"52", "Activision"}, {"5D", "Midway"},
    {"5G", "Majesco"}, {"69", "Electronic Arts"}, {"70", "Atari"}, {"78", "THQ"},
    {"7D", "Vivendi"}, {"8P", "Sega"}, {"A4", "Konami"}, {"AF", "Namco Bandai"},
    {"B2", "Bandai"}, {"C8", "Koei"}, {"E9", "Natsume"}, {"EB", "Atlus"},
    {"GD", "Square Enix"},
};

uint16_t read16(const QByteArray& d, int off)
{
    return (uint8_t)d[off] | ((uint8_t)d[off + 1] << 8);
}

uint32_t read32(const QByteArray& d, int off)
{
    return read16(d, off) | ((uint32_t)read16(d, off + 2) << 16);
}

QString latin(const QByteArray& d, int off, int len)
{
    QString s;
    for (int i = 0; i < len; i++)
    {
        char c = d[off + i];
        if (c == 0) break;
        s += QChar((uchar)c);
    }
    return s.trimmed();
}

QImage decodeIcon(const QByteArray& banner)
{
    QRgb palette[16];
    for (int i = 0; i < 16; i++)
    {
        uint16_t c = read16(banner, kBnrPalette + i * 2);
        int r = c & 0x1F, g = (c >> 5) & 0x1F, b = (c >> 10) & 0x1F;
        palette[i] = qRgba((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2), 255);
    }
    palette[0] = qRgba(0, 0, 0, 0);

    QImage img(32, 32, QImage::Format_ARGB32);
    for (int tile = 0; tile < 16; tile++)
    {
        int tx = (tile % 4) * 8, ty = (tile / 4) * 8;
        for (int y = 0; y < 8; y++)
        {
            for (int x = 0; x < 8; x++)
            {
                uint8_t byte = banner[kBnrIcon + tile * 32 + y * 4 + x / 2];
                int index = (x & 1) ? (byte >> 4) : (byte & 0xF);
                img.setPixel(tx + x, ty + y, palette[index]);
            }
        }
    }
    return img;
}

QString bannerTitle(const QByteArray& banner, int lang)
{
    int off = kBnrTitles + lang * kBnrTitleSize;
    if (off + kBnrTitleSize > banner.size()) return {};
    QString s;
    for (int i = 0; i < 128; i++)
    {
        char16_t c = read16(banner, off + i * 2);
        if (c == 0) break;
        s += QChar(c);
    }
    return s.trimmed();
}

int preferredLanguage(int bannerVersion)
{
    switch (QLocale::system().language())
    {
    case QLocale::Japanese: return Lang_Japanese;
    case QLocale::French:   return Lang_French;
    case QLocale::German:   return Lang_German;
    case QLocale::Italian:  return Lang_Italian;
    case QLocale::Spanish:  return Lang_Spanish;
    case QLocale::Chinese:  return bannerVersion >= 2 ? Lang_Chinese : Lang_English;
    case QLocale::Korean:   return bannerVersion >= 3 ? Lang_Korean : Lang_English;
    default:                return Lang_English;
    }
}

Config::Table globalConfig()
{
    return Config::GetGlobalTable();
}

// column order, widths, visibility and sorting. (Renamed when the title
// column stopped stretching, so layouts from before start over.)
const char* const kColumnsKey = "GameList.ColumnLayout";

}

bool readGame(const QString& path, Game& game)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    const qint64 size = f.size();
    QByteArray header = f.read(kHeaderSize);
    if (header.size() < kHeaderSize)
        return false;

    QFileInfo info(path);
    game.path = info.absoluteFilePath();
    game.fileName = info.fileName();
    game.size = size;

    const QString code = latin(header, kHdrGameCode, 4);
    game.gameCode = code;

    switch ((uint8_t)header[kHdrUnitCode])
    {
    case 0x02: game.platform = "DSi Enhanced"; break;
    case 0x03: game.platform = "DSi"; break;
    default:   game.platform = "DS"; break;
    }

    // region, from the last letter of the game code
    bool validCode = code.size() == 4;
    for (QChar c : code)
        if (!c.isLetterOrNumber()) validCode = false;
    if (validCode)
    {
        char r = code[3].toLatin1();
        for (const Region& reg : kRegions)
        {
            if (reg.code == r)
            {
                game.region = reg.name;
                game.regionFlag = reg.flag;
                break;
            }
        }
        if (game.region.isEmpty())
            game.region = "Unknown";

        // Korean and Chinese carts are also marked in the header
        uint8_t hdrRegion = header[kHdrRegion];
        if (hdrRegion & 0x80) { game.region = "China"; game.regionFlag = "cn"; }
        else if (hdrRegion & 0x40) { game.region = "Korea"; game.regionFlag = "kr"; }
    }
    else
    {
        game.region = "Homebrew";
    }

    // title, publisher and icon from the banner
    QString title;
    uint32_t bannerOffset = read32(header, kHdrBannerOffset);
    if (bannerOffset >= (uint32_t)kHeaderSize && bannerOffset + kBnrTitles + kBnrTitleSize <= (quint64)size &&
        f.seek(bannerOffset))
    {
        QByteArray banner = f.read(kBnrReadSize);
        uint16_t version = read16(banner, 0);
        if (banner.size() >= kBnrTitles + 6 * kBnrTitleSize && (version & 0xFF) >= 1)
        {
            game.icon = decodeIcon(banner);

            title = bannerTitle(banner, preferredLanguage(version & 0xFF));
            if (title.isEmpty()) title = bannerTitle(banner, Lang_English);
            if (title.isEmpty()) title = bannerTitle(banner, Lang_Japanese);
        }
    }

    // banner titles are "Title\nPublisher" or "Title\nSubtitle\nPublisher"
    QStringList lines = title.split('\n', Qt::SkipEmptyParts);
    for (QString& l : lines) l = l.trimmed();
    if (lines.size() >= 2)
    {
        game.developer = lines.takeLast();
        game.title = lines.takeFirst();
        game.subtitle = lines.join(' ');
    }
    else if (lines.size() == 1)
    {
        game.title = lines.first();
    }

    if (game.title.isEmpty())
        game.title = latin(header, kHdrTitle, 12);
    if (game.title.isEmpty())
        game.title = info.completeBaseName();

    if (game.developer.isEmpty())
    {
        QString maker = latin(header, kHdrMakerCode, 2);
        for (const Maker& m : kMakers)
            if (maker == m.code) game.developer = m.name;
    }

    return true;
}

std::vector<Game> scanFolders(const QStringList& dirs, bool recursive, const std::atomic<bool>& cancel)
{
    std::vector<Game> games;
    std::set<QString> seen;

    const QStringList filters = {"*.nds", "*.dsi", "*.srl", "*.ids"};
    for (const QString& dir : dirs)
    {
        QDirIterator it(dir, filters, QDir::Files | QDir::Readable,
                        recursive ? QDirIterator::Subdirectories | QDirIterator::FollowSymlinks
                                  : QDirIterator::NoIteratorFlags);
        while (it.hasNext())
        {
            if (cancel) return {};

            QString path = QFileInfo(it.next()).canonicalFilePath();
            if (path.isEmpty() || !seen.insert(path).second)
                continue;

            Game game;
            if (readGame(path, game))
                games.push_back(std::move(game));
        }
    }
    return games;
}

QStringList folders()
{
    return globalConfig().GetQString("GameList.Folders").split('\n', Qt::SkipEmptyParts);
}

void setFolders(const QStringList& list)
{
    globalConfig().SetQString("GameList.Folders", list.join('\n'));
}

bool searchSubfolders()
{
    return globalConfig().GetBool("GameList.SearchSubfolders");
}

void setSearchSubfolders(bool recursive)
{
    globalConfig().SetBool("GameList.SearchSubfolders", recursive);
}

bool isEnabled()
{
    return globalConfig().GetBool("GameList.Enabled");
}

void setEnabled(bool enabled)
{
    globalConfig().SetBool("GameList.Enabled", enabled);
}

bool showSearchBar()
{
    return globalConfig().GetBool("GameList.SearchBar");
}

void setShowSearchBar(bool show)
{
    globalConfig().SetBool("GameList.SearchBar", show);
}

bool showRegionNames()
{
    return globalConfig().GetBool("GameList.RegionNames");
}

void setShowRegionNames(bool show)
{
    globalConfig().SetBool("GameList.RegionNames", show);
}


// ---------------------------------------------------------------------------
// Model

Model::Model(QObject* parent) : QAbstractTableModel(parent)
{
}

void Model::setGames(std::vector<Game> games)
{
    beginResetModel();
    list = std::move(games);
    icons.clear();
    icons.reserve(list.size());
    for (const Game& g : list)
        icons.push_back(g.icon.isNull() ? QIcon() : QIcon(QPixmap::fromImage(g.icon)));
    endResetModel();
}

void Model::setShowRegionNames(bool show)
{
    if (show == regionNames) return;
    regionNames = show;
    if (!list.empty())
        emit dataChanged(index(0, Col_Region), index((int)list.size() - 1, Col_Region));
}

int Model::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : (int)list.size();
}

int Model::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : NumColumns;
}

void Model::setFlagStyle(bool highRes, qreal scale)
{
    if (highRes == highResFlags && scale == flagScale) return;
    highResFlags = highRes;
    flagScale = scale;
    flags.clear();
    if (!list.empty())
        emit dataChanged(index(0, Col_Region), index((int)list.size() - 1, Col_Region));
}

QPixmap Model::flagPixmap(const QString& code) const
{
    QPixmap pm;
    if (highResFlags)
    {
        // drawn from the vector flag at the screen's real resolution
        QImageReader reader(":/flags-hd/" + code + ".svg");
        const QSize logical(32, 24);
        reader.setScaledSize(logical * flagScale);
        QImage img = reader.read();
        if (!img.isNull())
        {
            pm = QPixmap::fromImage(img);
            pm.setDevicePixelRatio(flagScale);
        }
    }
    if (pm.isNull())
    {
        // the 16x11 pixel-art flags, doubled and kept sharp; also the
        // fallback if the vector ones can't be drawn
        QPixmap small(":/flags/" + code + ".png");
        if (small.isNull()) return {};
        pm = small.scaled(small.size() * 2, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        return pm;
    }

    // a hairline edge, so white flags (Japan, Korea) don't melt into the list
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(QColor(0, 0, 0, 70), 0));
    p.setBrush(Qt::NoBrush);
    QSizeF size = (QSizeF(pm.size()) / pm.devicePixelRatio());
    p.drawRect(QRectF(0, 0, size.width() - 1.0 / flagScale, size.height() - 1.0 / flagScale));
    p.end();
    return pm;
}

QPixmap Model::flagIcon(const QString& key) const
{
    if (key.isEmpty()) return {};
    auto it = flags.find(key);
    if (it != flags.end()) return *it;

    QList<QPixmap> parts;
    for (const QString& code : key.split('+'))
    {
        QPixmap pm = flagPixmap(code);
        if (!pm.isNull()) parts << pm;
    }

    QPixmap icon;
    if (parts.size() == 1)
    {
        icon = parts.first();
    }
    else if (!parts.isEmpty())
    {
        // side by side, e.g. USA/Europe
        const qreal dpr = parts.first().devicePixelRatio();
        const qreal gap = 4;
        qreal w = -gap, h = 0;
        for (const QPixmap& pm : parts)
        {
            QSizeF s = (QSizeF(pm.size()) / pm.devicePixelRatio());
            w += s.width() + gap;
            h = std::max(h, s.height());
        }
        QPixmap combined((QSizeF(w, h) * dpr).toSize());
        combined.setDevicePixelRatio(dpr);
        combined.fill(Qt::transparent);
        QPainter p(&combined);
        qreal x = 0;
        for (const QPixmap& pm : parts)
        {
            QSizeF s = (QSizeF(pm.size()) / pm.devicePixelRatio());
            p.drawPixmap(QPointF(x, (h - s.height()) / 2), pm);
            x += s.width() + gap;
        }
        p.end();
        icon = combined;
    }
    flags.insert(key, icon);
    return icon;
}

QVariant Model::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= (int)list.size())
        return {};
    const Game& g = list[index.row()];

    if (role == PathRole)
        return g.path;

    if (role == Qt::ToolTipRole)
    {
        QString tip = g.title;
        if (!g.subtitle.isEmpty()) tip += "\n" + g.subtitle;
        if (!g.developer.isEmpty()) tip += "\n" + g.developer;
        if (!g.region.isEmpty()) tip += "\n" + g.region;
        return tip + "\n\n" + QDir::toNativeSeparators(g.path);
    }

    switch (index.column())
    {
    case Col_Title:
        if (role == Qt::DisplayRole)
            return g.subtitle.isEmpty() ? g.title : g.title + " - " + g.subtitle;
        if (role == Qt::DecorationRole) return icons[index.row()];
        if (role == SortRole) return QString(g.title + " " + g.subtitle).toCaseFolded();
        break;
    case Col_Developer:
        if (role == Qt::DisplayRole) return g.developer;
        if (role == SortRole) return g.developer.toCaseFolded();
        break;
    case Col_Region:
        // with names hidden, games without a flag (homebrew, region free) still say what they are
        if (role == Qt::DisplayRole)
            return (regionNames || g.regionFlag.isEmpty()) ? g.region : QString();
        if (role == SortRole) return g.region;
        if (role == Qt::DecorationRole) return flagIcon(g.regionFlag);
        if (role == Qt::TextAlignmentRole) return int(Qt::AlignCenter);
        break;
    case Col_GameCode:
        if (role == Qt::DisplayRole || role == SortRole) return g.gameCode;
        if (role == Qt::TextAlignmentRole) return int(Qt::AlignCenter);
        break;
    case Col_Platform:
        if (role == Qt::DisplayRole || role == SortRole) return g.platform;
        if (role == Qt::TextAlignmentRole) return int(Qt::AlignCenter);
        break;
    case Col_Size:
        if (role == Qt::DisplayRole) return QLocale().formattedDataSize(g.size);
        if (role == SortRole) return g.size;
        if (role == Qt::TextAlignmentRole) return int(Qt::AlignCenter);
        break;
    case Col_FileName:
        if (role == Qt::DisplayRole) return g.fileName;
        if (role == SortRole) return g.fileName.toCaseFolded();
        break;
    }
    return {};
}

QVariant Model::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section)
    {
    case Col_Title:     return "Title";
    case Col_Developer: return "Developer";
    case Col_Region:    return "Region";
    case Col_GameCode:  return "Game Code";
    case Col_Platform:  return "Platform";
    case Col_Size:      return "Size";
    case Col_FileName:  return "File Name";
    }
    return {};
}


// ---------------------------------------------------------------------------
// Widget

namespace
{

// sorts by the raw values, and searches title, developer, game code and file name
class ProxyModel : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setSearch(const QString& text)
    {
        terms = text.split(' ', Qt::SkipEmptyParts);
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override
    {
        if (terms.isEmpty()) return true;
        auto* m = static_cast<Model*>(sourceModel());
        const Game& g = m->games()[row];
        QString hay = g.title + " " + g.subtitle + " " + g.developer + " " + g.gameCode + " " +
                      g.fileName + " " + g.region;
        for (const QString& t : terms)
            if (!hay.contains(t, Qt::CaseInsensitive)) return false;
        (void)parent;
        return true;
    }

private:
    QStringList terms;
};

}

// Qt keeps an item's icon at the left even when its text is centered; this
// centers the flag and region name together
class CenteredDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QPixmap flag = index.data(Qt::DecorationRole).value<QPixmap>();
        const QString text = opt.text;

        // background, selection and focus as usual, without the content
        opt.icon = QIcon();
        opt.text.clear();
        opt.features &= ~QStyleOptionViewItem::HasDecoration;
        const QWidget* w = option.widget;
        QStyle* style = w ? w->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, p, w);

        // measured in logical pixels, so high-DPI flags aren't drawn doubled
        const QSize iconSize = flag.isNull() ? QSize()
                             : (QSizeF(flag.size()) / flag.devicePixelRatio()).toSize();
        const int gap = (!flag.isNull() && !text.isEmpty()) ? 6 : 0;
        const int textW = text.isEmpty() ? 0 : option.fontMetrics.horizontalAdvance(text);
        const QRect r = option.rect;
        int x = r.left() + std::max(4, (r.width() - iconSize.width() - gap - textW) / 2);

        if (!flag.isNull())
        {
            p->drawPixmap(QRect(QPoint(x, r.top() + (r.height() - iconSize.height()) / 2), iconSize), flag);
            x += iconSize.width() + gap;
        }
        if (!text.isEmpty())
        {
            const bool selected = option.state & QStyle::State_Selected;
            p->save();
            p->setPen(option.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));
            p->setFont(option.font);
            QRect tr(x, r.top(), r.right() - x - 2, r.height());
            p->drawText(tr, Qt::AlignLeft | Qt::AlignVCenter,
                        option.fontMetrics.elidedText(text, Qt::ElideRight, tr.width()));
            p->restore();
        }
    }
};

Widget::Widget(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    pages = new QStackedWidget();
    layout->addWidget(pages);

    // the list
    listPage = new QWidget();
    auto* listLayout = new QVBoxLayout(listPage);
    listLayout->setContentsMargins(6, 6, 6, 6);
    searchRow = new QWidget();
    auto* top = new QHBoxLayout(searchRow);
    top->setContentsMargins(0, 0, 0, 0);
    search = new QLineEdit();
    search->setPlaceholderText("Search");
    search->setClearButtonEnabled(true);
    top->addWidget(search, 1);
    status = new QLabel();
    top->addWidget(status);
    listLayout->addWidget(searchRow);
    searchRow->setVisible(showSearchBar());

    model = new Model(this);
    proxy = new ProxyModel(this);
    proxy->setSourceModel(model);
    proxy->setSortRole(Model::SortRole);
    proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    proxy->setSortLocaleAware(true);

    view = new QTreeView();
    view->setModel(proxy);
    view->setRootIsDecorated(false);
    view->setUniformRowHeights(true);
    view->setAlternatingRowColors(true);
    view->setSortingEnabled(true);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setIconSize(QSize(32, 32));
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    view->header()->setContextMenuPolicy(Qt::CustomContextMenu);
    view->header()->setSectionsMovable(true);
    view->header()->setFirstSectionMovable(true);
    // centered column titles; the cells keep their own alignment
    view->header()->setDefaultAlignment(Qt::AlignCenter);
    view->setItemDelegateForColumn(Model::Col_Region, new CenteredDelegate(view));
    model->setShowRegionNames(showRegionNames());
    // every column keeps the width it's dragged to; the list scrolls sideways
    // if they add up to more than the window
    view->header()->setStretchLastSection(false);
    view->header()->setSectionResizeMode(QHeaderView::Interactive);
    view->header()->setMinimumSectionSize(40);
    listLayout->addWidget(view, 1);
    pages->addWidget(listPage);

    // shown when there's nothing to list
    emptyPage = new QWidget();
    auto* emptyLayout = new QVBoxLayout(emptyPage);
    emptyLayout->addStretch(1);
    emptyText = new QLabel();
    emptyText->setAlignment(Qt::AlignCenter);
    emptyText->setWordWrap(true);
    emptyLayout->addWidget(emptyText);
    auto* emptyButtons = new QHBoxLayout();
    emptyButtons->addStretch(1);
    auto* addButton = new QPushButton("Add Folder...");
    auto* settingsButton = new QPushButton("Path Settings...");
    emptyButtons->addWidget(addButton);
    emptyButtons->addWidget(settingsButton);
    emptyButtons->addStretch(1);
    emptyLayout->addLayout(emptyButtons);
    emptyLayout->addStretch(1);
    pages->addWidget(emptyPage);

    connect(search, &QLineEdit::textChanged, this, [this](const QString& text)
    {
        static_cast<ProxyModel*>(proxy)->setSearch(text);
        updatePage();
    });
    connect(view, &QTreeView::activated, this, &Widget::bootIndex);
    connect(view, &QWidget::customContextMenuRequested, this, &Widget::showContextMenu);
    connect(view->header(), &QWidget::customContextMenuRequested, this, &Widget::showHeaderMenu);
    connect(addButton, &QPushButton::clicked, this, [this]()
    {
        QString dir = QFileDialog::getExistingDirectory(this, "Add Game Folder");
        if (dir.isEmpty()) return;
        QStringList list = folders();
        if (!list.contains(dir)) list << dir;
        setFolders(list);
        Config::Save();
        refresh();
    });
    connect(settingsButton, &QPushButton::clicked, this, &Widget::openSettings);

    auto* refreshKey = new QShortcut(QKeySequence::Refresh, this);
    refreshKey->setContext(Qt::WidgetWithChildrenShortcut);
    connect(refreshKey, &QShortcut::activated, this, &Widget::refresh);

    auto* findKey = new QShortcut(QKeySequence::Find, this);
    findKey->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findKey, &QShortcut::activated, this, [this]()
    {
        setSearchBarVisible(true);
        search->setFocus();
        search->selectAll();
    });

    restoreHeader();
    refresh();
}

Widget::~Widget()
{
    saveHeader();
    if (scanThread)
    {
        *scanCancel = true;
        scanThread->wait(2000);
    }
}

void Widget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateFlagStyle();
}

void Widget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    updateFlagStyle();
}

void Widget::updateFlagStyle()
{
    // pixel-art flags suit a small window on a normal screen; once the window
    // is large or the screen is high-DPI they'd look blocky, so use vector ones
    const qreal dpr = devicePixelRatioF();
    const bool highRes = dpr >= 1.5 || width() >= 1100;
    model->setFlagStyle(highRes, dpr);
}

int Widget::gameCount() const
{
    return model->rowCount();
}

void Widget::refresh()
{
    if (scanCancel) *scanCancel = true;

    const QStringList dirs = folders();
    const bool recursive = searchSubfolders();
    const int serial = ++scanSerial;
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    scanCancel = cancel;
    scanning = !dirs.isEmpty();
    updatePage();
    if (dirs.isEmpty())
    {
        model->setGames({});
        updatePage();
        return;
    }

    QPointer<Widget> self(this);
    QThread* thread = QThread::create([self, dirs, recursive, serial, cancel]()
    {
        std::vector<Game> games = scanFolders(dirs, recursive, *cancel);
        if (*cancel) return;
        QMetaObject::invokeMethod(qApp, [self, serial, games = std::move(games)]() mutable
        {
            if (self) self->scanFinished(serial, std::move(games));
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    scanThread = thread;
    thread->start(QThread::LowPriority);
}

void Widget::scanFinished(int serial, std::vector<Game> games)
{
    if (serial != scanSerial) return; // a newer scan is running

    scanning = false;
    model->setGames(std::move(games));
    view->sortByColumn(view->header()->sortIndicatorSection(), view->header()->sortIndicatorOrder());
    updatePage();
}

void Widget::updatePage()
{
    const int total = model->rowCount();
    const int shown = proxy->rowCount();

    if (scanning && total == 0)
        status->setText("Scanning...");
    else if (shown != total)
        status->setText(QString("%1 of %2 games").arg(shown).arg(total));
    else
        status->setText(total == 1 ? QString("1 game") : QString("%1 games").arg(total));

    if (total > 0 || scanning)
    {
        pages->setCurrentWidget(listPage);
        return;
    }

    emptyText->setText(folders().isEmpty()
        ? "Add a folder with your DS games, and they'll be listed here."
        : "No DS games were found in your game folders.");
    pages->setCurrentWidget(emptyPage);
}

void Widget::bootIndex(const QModelIndex& index)
{
    if (!index.isValid()) return;
    QString path = proxy->data(index, Model::PathRole).toString();
    if (!path.isEmpty())
        emit bootRequested(path);
}

void Widget::showContextMenu(const QPoint& pos)
{
    QModelIndex index = view->indexAt(pos);
    QString path = index.isValid() ? proxy->data(index, Model::PathRole).toString() : QString();

    QMenu menu(this);
    if (!path.isEmpty())
    {
        QAction* boot = menu.addAction("Boot");
        QFont f = boot->font();
        f.setBold(true);
        boot->setFont(f);
        connect(boot, &QAction::triggered, this, [this, index]() { bootIndex(index); });

        connect(menu.addAction("Open Containing Folder"), &QAction::triggered, this, [path]()
        {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
        });
        connect(menu.addAction("Copy File Path"), &QAction::triggered, this, [path]()
        {
            QApplication::clipboard()->setText(QDir::toNativeSeparators(path));
        });
        menu.addSeparator();
    }
    addViewOptions(menu);
    menu.addSeparator();
    connect(menu.addAction("Refresh"), &QAction::triggered, this, &Widget::refresh);
    connect(menu.addAction("Game Folders..."), &QAction::triggered, this, &Widget::openSettings);
    menu.exec(view->viewport()->mapToGlobal(pos));
}

void Widget::addViewOptions(QMenu& menu)
{
    QAction* act = menu.addAction("Show Search Bar");
    act->setCheckable(true);
    act->setChecked(searchRow->isVisible());
    act->setShortcut(QKeySequence::Find);
    connect(act, &QAction::toggled, this, [this](bool on)
    {
        setSearchBarVisible(on);
        if (on) search->setFocus();
    });
}

void Widget::setSearchBarVisible(bool visible)
{
    if (!visible)
        search->clear(); // a hidden filter would only be confusing
    searchRow->setVisible(visible);
    setShowSearchBar(visible);
}

void Widget::showHeaderMenu(const QPoint& pos)
{
    QMenu menu(this);
    QHeaderView* header = view->header();

    // in the order they're shown, so the menu matches the header
    for (int v = 0; v < Model::NumColumns; v++)
    {
        int c = header->logicalIndex(v);
        QAction* act = menu.addAction(model->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString());
        act->setCheckable(true);
        act->setChecked(!header->isSectionHidden(c));
        if (c == Model::Col_Title)
        {
            act->setEnabled(false); // always shown
            continue;
        }
        connect(act, &QAction::toggled, this, [this, header, c](bool on)
        {
            header->setSectionHidden(c, !on);
            saveHeader();
        });
    }

    menu.addSeparator();
    QAction* names = menu.addAction("Show Region Names");
    names->setCheckable(true);
    names->setChecked(model->showsRegionNames());
    names->setEnabled(!header->isSectionHidden(Model::Col_Region));
    connect(names, &QAction::toggled, this, [this, header](bool on)
    {
        model->setShowRegionNames(on);
        setShowRegionNames(on);
        // fit the column to just the flags, or to the names again
        header->resizeSection(Model::Col_Region, on ? 170 : 80);
        saveHeader();
    });

    menu.addSeparator();
    addViewOptions(menu);

    menu.addSeparator();
    QAction* reset = menu.addAction("Reset Columns");
    connect(reset, &QAction::triggered, this, [this]()
    {
        model->setShowRegionNames(true);
        setShowRegionNames(true);
        defaultColumns();
        saveHeader();
    });

    auto* hint = menu.addAction("Drag the column headers to reorder them");
    hint->setEnabled(false);

    menu.exec(header->mapToGlobal(pos));
}

void Widget::openSettings()
{
    emit settingsRequested();
}

void Widget::saveHeader()
{
    QByteArray state = view->header()->saveState();
    globalConfig().SetString(kColumnsKey, state.toBase64().toStdString());
}

void Widget::restoreHeader()
{
    QHeaderView* header = view->header();
    QByteArray state = QByteArray::fromBase64(QByteArray::fromStdString(globalConfig().GetString(kColumnsKey)));
    if (!state.isEmpty() && header->restoreState(state))
    {
        header->setSectionResizeMode(QHeaderView::Interactive);
        return;
    }

    defaultColumns();
}

void Widget::defaultColumns()
{
    QHeaderView* header = view->header();

    // original order, everything but platform and file name shown
    for (int c = 0; c < Model::NumColumns; c++)
    {
        header->moveSection(header->visualIndex(c), c);
        header->setSectionHidden(c, false);
    }
    header->setSectionHidden(Model::Col_Platform, true);
    header->setSectionHidden(Model::Col_FileName, true);
    header->resizeSection(Model::Col_Developer, 160);
    header->resizeSection(Model::Col_Region, 170);
    header->resizeSection(Model::Col_GameCode, 90);
    header->resizeSection(Model::Col_Platform, 110);
    header->resizeSection(Model::Col_Size, 90);
    header->resizeSection(Model::Col_FileName, 200);
    header->resizeSection(Model::Col_Title, 360);
    header->setSortIndicator(Model::Col_Title, Qt::AscendingOrder);

    // give the title whatever room is left once the list has its real size
    QTimer::singleShot(0, this, [this]()
    {
        QHeaderView* h = view->header();
        int others = 0;
        for (int c = 0; c < Model::NumColumns; c++)
            if (c != Model::Col_Title && !h->isSectionHidden(c)) others += h->sectionSize(c);
        int room = view->viewport()->width() - others;
        if (room > h->sectionSize(Model::Col_Title))
            h->resizeSection(Model::Col_Title, room);
        saveHeader();
    });
}


// ---------------------------------------------------------------------------
// FoldersEditor

FoldersEditor::FoldersEditor(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QGridLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    folderList = new QListWidget();
    folderList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    folderList->setMinimumHeight(90);
    for (const QString& dir : folders())
        folderList->addItem(QDir::toNativeSeparators(dir));
    layout->addWidget(folderList, 0, 0, 3, 1);

    auto* addButton = new QPushButton("Add...");
    auto* removeButton = new QPushButton("Remove");
    layout->addWidget(addButton, 0, 1);
    layout->addWidget(removeButton, 1, 1);
    layout->setRowStretch(2, 1);

    cbRecursive = new QCheckBox("Search subfolders");
    cbRecursive->setChecked(searchSubfolders());
    layout->addWidget(cbRecursive, 3, 0, 1, 2);

    connect(addButton, &QPushButton::clicked, this, &FoldersEditor::addFolder);
    connect(removeButton, &QPushButton::clicked, this, [this]() { qDeleteAll(folderList->selectedItems()); });
    connect(folderList, &QListWidget::itemSelectionChanged, this, [this, removeButton]()
    {
        removeButton->setEnabled(!folderList->selectedItems().isEmpty());
    });
    removeButton->setEnabled(false);
}

void FoldersEditor::addFolder()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Add Game Folder");
    if (dir.isEmpty()) return;
    QString shown = QDir::toNativeSeparators(dir);
    if (folderList->findItems(shown, Qt::MatchFixedString).isEmpty())
        folderList->addItem(shown);
}

bool FoldersEditor::save()
{
    QStringList list;
    for (int i = 0; i < folderList->count(); i++)
        list << QDir::fromNativeSeparators(folderList->item(i)->text());

    bool changed = list != folders() || cbRecursive->isChecked() != searchSubfolders();
    setFolders(list);
    setSearchSubfolders(cbRecursive->isChecked());
    return changed;
}

}
