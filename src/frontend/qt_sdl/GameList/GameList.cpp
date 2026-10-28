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
#include "Platform.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <set>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QDataStream>
#include <QSaveFile>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QBuffer>
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
#include <QListView>
#include <QListWidget>
#include <QSlider>
#include <QToolTip>
#include <QHelpEvent>
#include <QPainterPath>
#include <QToolButton>
#include <QWheelEvent>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
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
#include "Covers.h"
#include "RetroAchievements/RAClient.h"
#include "RetroAchievements/RALibrary.h"

namespace GameList
{

// ---------------------------------------------------------------------------
// Reading ROMs

namespace
{

// offsets in the cartridge header
constexpr int kHeaderSize = 0x240;
constexpr int kHdrDSiTitleIdHigh = 0x234; // 0x00030004 for DSiWare
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

// column order, widths, visibility and sorting
const char* const kColumnsKey = "GameList.ColumnLayout";

}

namespace
{

// the header and banner of a ROM in `f`; the file details are filled in by the caller
bool readGameFrom(QIODevice& f, const QString& fileName, Game& game)
{
    const qint64 size = f.size();
    QByteArray header = f.read(kHeaderSize);
    if (header.size() < kHeaderSize)
        return false;

    const QString code = latin(header, kHdrGameCode, 4);
    game.gameCode = code;

    switch ((uint8_t)header[kHdrUnitCode])
    {
    case 0x02: game.platform = "DSi Enhanced"; break;
    case 0x03: game.platform = "DSi"; break;
    default:   game.platform = "DS"; break;
    }
    // sold on the DSi Shop rather than on a cartridge
    if ((uint8_t)header[kHdrUnitCode] & 0x02)
    {
        const auto* p = reinterpret_cast<const uint8_t*>(header.constData()) + kHdrDSiTitleIdHigh;
        const uint32_t high = p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
        if (high == 0x00030004) game.platform = "DSiWare";
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
    if (bannerOffset >= 0x200u && bannerOffset + kBnrTitles + kBnrTitleSize <= (quint64)size &&
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
        game.title = QFileInfo(fileName).completeBaseName();

    if (game.developer.isEmpty())
    {
        QString maker = latin(header, kHdrMakerCode, 2);
        for (const Maker& m : kMakers)
            if (maker == m.code) game.developer = m.name;
    }

    return true;
}

}

bool readGame(const QString& path, Game& game)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    QFileInfo info(path);
    game.path = info.absoluteFilePath();
    game.fileName = info.fileName();
    game.size = f.size();
    game.modified = info.lastModified().toMSecsSinceEpoch();
    return readGameFrom(f, game.fileName, game);
}

bool readGame(const QByteArray& rom, const QString& fileName, Game& game)
{
    QBuffer buf;
    buf.setData(rom);
    if (!buf.open(QIODevice::ReadOnly))
        return false;

    game.fileName = fileName;
    game.size = rom.size();
    return readGameFrom(buf, fileName, game);
}

std::vector<Game> scanFolders(const QStringList& dirs, bool recursive, const std::atomic<bool>& cancel,
                              const std::map<QString, Game>* known, int* read, std::vector<Game>* rejected)
{
    std::vector<Game> games;
    std::set<QString> seen;
    if (read) *read = 0;
    if (rejected) rejected->clear();

    const QStringList filters = {"*.nds", "*.dsi", "*.srl", "*.ids"};
    for (const QString& dir : dirs)
    {
        QDirIterator it(dir, filters, QDir::Files | QDir::Readable,
                        recursive ? QDirIterator::Subdirectories | QDirIterator::FollowSymlinks
                                  : QDirIterator::NoIteratorFlags);
        while (it.hasNext())
        {
            if (cancel) return {};

            it.next();
            const QFileInfo info = it.fileInfo();
            QString path = info.canonicalFilePath();
            if (path.isEmpty() || !seen.insert(path).second)
                continue;

            // unchanged since the last scan: nothing to read
            if (known)
            {
                auto k = known->find(path);
                if (k != known->end() && k->second.size == info.size() &&
                    k->second.modified == info.lastModified().toMSecsSinceEpoch())
                {
                    if (k->second.notRom) { if (rejected) rejected->push_back(k->second); }
                    else games.push_back(k->second);
                    continue;
                }
            }

            Game game;
            if (read) ++*read;
            if (readGame(path, game))
                games.push_back(std::move(game));
            else if (rejected)
            {
                Game skip;
                skip.path = path;
                skip.size = info.size();
                skip.modified = info.lastModified().toMSecsSinceEpoch();
                skip.notRom = true;
                rejected->push_back(std::move(skip));
            }
        }
    }
    return games;
}

namespace
{
std::mutex scannedLock;
std::vector<Game> scanned;
}

// The last scan's games, so the list shows at once on the next start and
// only new or changed ROMs have to be read.
namespace
{
constexpr quint32 kSavedMagic = 0x4D52474C; // "MRGL"
constexpr quint32 kSavedVersion = 2; // 2: DSiWare recognized

QString savedListPath()
{
    return QString::fromStdString(melonDS::Platform::GetLocalFilePath("gamelist.cache"));
}

// what the saved list was made for; titles depend on the system language
struct SavedHeader
{
    QStringList folders;
    bool recursive = false;
    int language = 0;
};

void saveList(const SavedHeader& h, const std::vector<Game>& games)
{
    QSaveFile f(savedListPath());
    if (!f.open(QIODevice::WriteOnly)) return;
    QDataStream out(&f);
    out.setVersion(QDataStream::Qt_6_0);
    out << kSavedMagic << kSavedVersion << h.folders << h.recursive << (qint32)h.language << (quint32)games.size();
    for (const Game& g : games)
        out << g.path << g.fileName << g.size << g.modified << g.title << g.subtitle << g.developer
            << g.gameCode << g.region << g.regionFlag << g.platform << g.icon << g.notRom;
    if (out.status() == QDataStream::Ok) f.commit();
}

bool loadList(const SavedHeader& want, std::vector<Game>& games, std::vector<Game>& rejected)
{
    QFile f(savedListPath());
    if (!f.open(QIODevice::ReadOnly)) return false;
    QDataStream in(&f);
    in.setVersion(QDataStream::Qt_6_0);

    quint32 magic = 0, version = 0, count = 0;
    SavedHeader h;
    qint32 language = 0;
    in >> magic >> version;
    if (magic != kSavedMagic || version != kSavedVersion) return false;
    in >> h.folders >> h.recursive >> language >> count;
    if (in.status() != QDataStream::Ok || h.folders != want.folders || h.recursive != want.recursive ||
        language != want.language || count > 1000000)
        return false;

    games.clear();
    rejected.clear();
    games.reserve(count);
    for (quint32 i = 0; i < count && in.status() == QDataStream::Ok; i++)
    {
        Game g;
        in >> g.path >> g.fileName >> g.size >> g.modified >> g.title >> g.subtitle >> g.developer
           >> g.gameCode >> g.region >> g.regionFlag >> g.platform >> g.icon >> g.notRom;
        (g.notRom ? rejected : games).push_back(std::move(g));
    }
    return in.status() == QDataStream::Ok;
}

SavedHeader currentHeader()
{
    return {folders(), searchSubfolders(), (int)QLocale::system().language()};
}

// same files, same sizes and dates, in any order
bool sameFiles(const std::vector<Game>& a, const std::map<QString, Game>& b)
{
    if (a.size() != b.size()) return false;
    for (const Game& g : a)
    {
        auto it = b.find(g.path);
        if (it == b.end() || it->second.size != g.size || it->second.modified != g.modified) return false;
    }
    return true;
}
}

std::vector<Game> scannedGames()
{
    std::lock_guard<std::mutex> guard(scannedLock);
    return scanned;
}

Notifier* notifier()
{
    static Notifier* n = new Notifier();
    return n;
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

bool gridMode()
{
    return globalConfig().GetString("GameList.View") == "grid";
}

void setGridMode(bool grid)
{
    globalConfig().SetString("GameList.View", grid ? "grid" : "list");
}

constexpr int kMinTile = 96, kMaxTile = 256, kDefaultTile = 144;

int tileSize()
{
    int s = globalConfig().GetInt("GameList.TileSize");
    return (s >= kMinTile && s <= kMaxTile) ? s : kDefaultTile;
}

void setTileSize(int size)
{
    globalConfig().SetInt("GameList.TileSize", size);
}

bool showSearchBar()
{
    return globalConfig().GetBool("GameList.SearchBar");
}

void setShowSearchBar(bool show)
{
    globalConfig().SetBool("GameList.SearchBar", show);
}

bool showMasteryStars()
{
    return globalConfig().GetBool("GameList.MasteryStars");
}

void setShowMasteryStars(bool show)
{
    globalConfig().SetBool("GameList.MasteryStars", show);
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
    // a cover arrived: redraw the games that use it
    connect(Covers::get(), &Covers::coverReady, this, [this](const QString& key)
    {
        for (size_t i = 0; i < coverKeys.size(); i++)
            if (coverKeys[i] == key)
                emit dataChanged(index((int)i, Col_Title), index((int)i, Col_Title), {CoverRole});
    });
    connect(Covers::get(), &Covers::coversReset, this, [this]()
    {
        if (!list.empty())
            emit dataChanged(index(0, Col_Title), index((int)list.size() - 1, Col_Title), {CoverRole});
    });
}

void Model::setGames(std::vector<Game> games)
{
    beginResetModel();
    list = std::move(games);
    icons.clear();
    icons.reserve(list.size());
    coverKeys.clear();
    sizeTexts.clear();
    QLocale loc;
    for (const Game& g : list)
    {
        icons.push_back(g.icon.isNull() ? QIcon() : QIcon(QPixmap::fromImage(g.icon)));
        coverKeys.push_back(Covers::keyFor(g));
        sizeTexts.push_back(loc.formattedDataSize(g.size));
    }
    hasSettings.assign(list.size(), false);
    for (size_t i = 0; i < list.size(); i++)
        hasSettings[i] = Config::HasGameSettings(settingsId(list[i]).toStdString());
    endResetModel();
}

void Model::refreshSettingsMarks()
{
    for (size_t i = 0; i < list.size(); i++)
    {
        const bool has = Config::HasGameSettings(settingsId(list[i]).toStdString());
        if (i < hasSettings.size() && hasSettings[i] != has)
        {
            hasSettings[i] = has;
            emit dataChanged(index((int)i, Col_Title), index((int)i, Col_Title), {HasSettingsRole});
        }
    }
}

void Model::setProgress(const QHash<QString, Progress>& byPath)
{
    progress = byPath;
    if (!list.empty())
    {
        emit dataChanged(index(0, Col_Title), index((int)list.size() - 1, Col_Title), {StarRole});
        emit dataChanged(index(0, Col_Achievements), index((int)list.size() - 1, Col_Achievements));
    }
}

void Model::setShowStars(bool show)
{
    if (show == stars) return;
    stars = show;
    if (!list.empty())
        emit dataChanged(index(0, Col_Title), index((int)list.size() - 1, Col_Title), {StarRole});
}

QString settingsId(const Game& game)
{
    return QString::fromStdString(Config::GameSettingsId(game.gameCode.toStdString(), game.fileName.toStdString()));
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
    if (role == CoverRole)
        return Covers::get()->cover(g);
    if (role == CoverKeyRole)
        return coverKeys[index.row()];
    if (role == HasSettingsRole)
        return (size_t)index.row() < hasSettings.size() && hasSettings[index.row()];
    if (role == StarRole)
        return stars && progress.value(g.path).mastered();
    if (role == ProgressRole)
        return QVariant::fromValue(progress.value(g.path));

    if (role == Qt::ToolTipRole && index.column() == Col_Achievements)
    {
        const Progress pr = progress.value(g.path);
        if (pr.state == Progress::NotOnRA) return "Not on RetroAchievements";
        if (pr.state != Progress::Known) return {};
        if (!pr.total) return "No achievements yet";
        QString tip = QString("%1 of %2 achievements unlocked").arg(pr.unlocked).arg(pr.total);
        if (pr.mastered()) tip += "\nMastered";
        else
        {
            if (pr.hardcore) tip += QString("\n%1 in Hardcore mode").arg(pr.hardcore);
            if (pr.completed()) tip += "\nCompleted (unlock them all in Hardcore mode to master it)";
        }
        return tip;
    }

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
        if (role == Qt::DisplayRole) return sizeTexts[index.row()];
        if (role == SortRole) return g.size;
        if (role == Qt::TextAlignmentRole) return int(Qt::AlignCenter);
        break;
    case Col_FileName:
        if (role == Qt::DisplayRole) return g.fileName;
        if (role == SortRole) return g.fileName.toCaseFolded();
        break;
    case Col_Achievements:
        if (role == SortRole)
        {
            // by how far along, then by how many; games without any last
            const Progress pr = progress.value(g.path);
            if (pr.state != Progress::Known || !pr.total) return pr.state == Progress::NotOnRA ? -2.0 : -1.0;
            return (double)pr.unlocked / pr.total + (pr.mastered() ? 0.5 : 0.0) + pr.unlocked * 1e-6;
        }
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
    case Col_Achievements: return "Achievements";
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

// a small gear, marking games with their own settings
static void drawGear(QPainter* p, const QRectF& r, const QColor& color)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->translate(r.center());
    const qreal s = std::min(r.width(), r.height()) / 2;
    QPainterPath gear;
    gear.addEllipse(QPointF(0, 0), s * 0.72, s * 0.72);
    for (int i = 0; i < 8; i++)
    {
        QPainterPath tooth;
        tooth.addRect(QRectF(-s * 0.17, -s, s * 0.34, s * 0.5));
        gear = gear.united(QTransform().rotate(i * 45).map(tooth));
    }
    QPainterPath hole;
    hole.addEllipse(QPointF(0, 0), s * 0.3, s * 0.3);
    p->fillPath(gear.subtracted(hole), color);
    p->restore();
}

// gold, for mastered games
static const QColor kGold(245, 197, 24);

// a five-pointed star, marking mastered games
static void drawStar(QPainter* p, const QRectF& r, const QColor& fill)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QPointF c = r.center() + QPointF(0, r.height() * 0.04);
    const qreal outer = std::min(r.width(), r.height()) / 2;
    const qreal inner = outer * 0.45;
    QPolygonF star;
    for (int i = 0; i < 10; i++)
    {
        const qreal a = (i * 36 - 90) * 3.14159265358979 / 180.0;
        const qreal rad = (i & 1) ? inner : outer;
        star << c + QPointF(std::cos(a) * rad, std::sin(a) * rad);
    }
    p->setPen(QPen(fill.darker(150), std::max<qreal>(1.0, outer / 8), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->setBrush(fill);
    p->drawPolygon(star);
    p->restore();
}

// The list's cells: no hover effect, and the selected row in the theme's
// selection color with its selected-text color, so it stands out clearly.
class RowDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    // paints the selection (if any) and returns the option to draw the
    // content with: no hover, no focus frame, selected text colors
    static QStyleOptionViewItem prepare(QPainter* p, const QStyleOptionViewItem& in)
    {
        QStyleOptionViewItem opt = in;
        const bool selected = opt.state & QStyle::State_Selected;
        opt.state &= ~(QStyle::State_MouseOver | QStyle::State_HasFocus | QStyle::State_Selected);
        if (selected)
        {
            p->fillRect(opt.rect, opt.palette.brush(QPalette::Active, QPalette::Highlight));
            const QColor text = opt.palette.color(QPalette::Active, QPalette::HighlightedText);
            opt.palette.setColor(QPalette::Text, text);
            opt.palette.setColor(QPalette::WindowText, text);
            opt.features &= ~QStyleOptionViewItem::Alternate;
        }
        return opt;
    }

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = prepare(p, option);
        initStyleOption(&opt, index);
        const QWidget* w = option.widget;
        QStyle* style = w ? w->style() : QApplication::style();

        const bool isTitle = index.column() == Model::Col_Title;
        const bool star = isTitle && index.data(Model::StarRole).toBool();
        const bool gear = isTitle && index.data(Model::HasSettingsRole).toBool();
        if (!star && !gear)
        {
            style->drawControl(QStyle::CE_ItemViewItem, &opt, p, w);
            return;
        }

        // the marks go in front of the title: draw the icon without the
        // text, then the star and the gear, then the title after them
        const QRect text = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, w);
        const QString title = opt.text;
        opt.text.clear();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, p, w);

        const int g = std::max(10, opt.fontMetrics.height() - 4);
        const qreal top = opt.rect.center().y() - g / 2.0;
        int x = text.left() + 1;
        if (star)
        {
            drawStar(p, QRectF(x - 1, top - 1, g + 2, g + 2), kGold);
            x += g + 5;
        }
        if (gear)
        {
            QColor c = opt.palette.color(QPalette::Text);
            c.setAlphaF(0.75);
            drawGear(p, QRectF(x, top, g, g), c);
            x += g + 5;
        }

        QRect rest = text.adjusted(x - text.left() + 1, 0, 0, 0);
        p->save();
        p->setFont(opt.font);
        p->setPen(opt.palette.color(QPalette::Text));
        p->drawText(rest, Qt::AlignLeft | Qt::AlignVCenter,
                    opt.fontMetrics.elidedText(title, opt.textElideMode, rest.width()));
        p->restore();
    }

    bool helpEvent(QHelpEvent* event, QAbstractItemView* view, const QStyleOptionViewItem& option,
                   const QModelIndex& index) override
    {
        if (event->type() == QEvent::ToolTip && index.column() == Model::Col_Title)
        {
            QStringList lines;
            if (index.data(Model::StarRole).toBool())
                lines << "Mastered on RetroAchievements";
            if (index.data(Model::HasSettingsRole).toBool())
                lines << "This game has its own settings (right-click > Game Settings)";
            if (!lines.isEmpty())
            {
                QToolTip::showText(event->globalPos(), lines.join('\n'), view);
                return true;
            }
        }
        return QStyledItemDelegate::helpEvent(event, view, option, index);
    }
};

// the Achievements column: "30/90" and a percentage over a bar that fills up,
// gold once every achievement is unlocked
class AchievementsDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = RowDelegate::prepare(p, option);
        initStyleOption(&opt, index);
        opt.text.clear();
        const QWidget* w = option.widget;
        QStyle* style = w ? w->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, p, w);

        const auto pr = index.data(Model::ProgressRole).value<Model::Progress>();
        if (pr.state == Model::Progress::Unknown) return;

        QColor text = opt.palette.color(QPalette::Text);
        const QRect r = opt.rect.adjusted(8, 0, -8, 0);
        p->save();
        p->setFont(opt.font);

        if (pr.state == Model::Progress::NotOnRA || !pr.total)
        {
            QColor dim = text;
            dim.setAlphaF(0.45);
            p->setPen(dim);
            p->drawText(r, Qt::AlignCenter, QString::fromUtf8("\xE2\x80\x94")); // em dash
            p->restore();
            return;
        }

        const QFontMetrics& fm = opt.fontMetrics;
        const int barH = std::max(4, fm.height() / 3);
        const int gap = 3;
        const int blockH = fm.height() + gap + barH;
        const int y = opt.rect.center().y() - blockH / 2;
        const QRect line(r.left(), y, r.width(), fm.height());

        const bool done = pr.completed();
        const int percent = done ? 100 : (int)((uint64_t)pr.unlocked * 100 / pr.total);
        p->setPen(text);
        p->drawText(line, Qt::AlignLeft | Qt::AlignVCenter, QString("%1/%2").arg(pr.unlocked).arg(pr.total));
        p->drawText(line, Qt::AlignRight | Qt::AlignVCenter, QString("%1%").arg(percent));

        p->setRenderHint(QPainter::Antialiasing);
        const QRectF bar(r.left(), y + fm.height() + gap, r.width(), barH);
        QColor track = text;
        track.setAlphaF(0.18);
        p->setPen(Qt::NoPen);
        p->setBrush(track);
        p->drawRoundedRect(bar, barH / 2.0, barH / 2.0);
        if (pr.unlocked)
        {
            QRectF fill = bar;
            fill.setWidth(std::max<qreal>(barH, bar.width() * std::min(1.0, (double)pr.unlocked / pr.total)));
            p->setBrush(done ? kGold : QColor(40, 140, 230));
            p->drawRoundedRect(fill, barH / 2.0, barH / 2.0);
        }
        p->restore();
    }
};

// Qt keeps an item's icon at the left even when its text is centered; this
// centers the flag and region name together
class CenteredDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = RowDelegate::prepare(p, option);
        initStyleOption(&opt, index);
        const QPixmap flag = index.data(Qt::DecorationRole).value<QPixmap>();
        const QString text = opt.text;
        const QPalette palette = opt.palette;

        // background as usual, without the content
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
            p->save();
            p->setPen(palette.color(QPalette::Text));
            p->setFont(option.font);
            QRect tr(x, r.top(), r.right() - x - 2, r.height());
            p->drawText(tr, Qt::AlignLeft | Qt::AlignVCenter,
                        option.fontMetrics.elidedText(text, Qt::ElideRight, tr.width()));
            p->restore();
        }
    }
};

// a tile in the grid: the box art (or the game's icon until there is one)
// with the title under it; the selected tile gets an outline
class GridDelegate : public QStyledItemDelegate
{
public:
    explicit GridDelegate(QObject* parent) : QStyledItemDelegate(parent)
    {
        scaled.setMaxCost(48 * 1024); // KB
        // covers changed (another folder, ...): drop what was drawn
        QObject::connect(Covers::get(), &Covers::coversReset, this, [this]() { scaled.clear(); });
    }

    int tile = 144;

    // DS boxes are a little wider than they're tall
    static constexpr double kBoxRatio = 0.9;

    void setTile(int size)
    {
        tile = size;
        scaled.clear();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override
    {
        int text = option.fontMetrics.lineSpacing() * 2;
        return QSize(tile, (int)(tile * kBoxRatio) + text + 12);
    }

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const QString title = index.data(Qt::DisplayRole).toString();
        const QRect r = option.rect.adjusted(4, 4, -4, -4);
        const QRect art(r.left(), r.top(), r.width(), (int)(r.width() * kBoxRatio));
        const QPalette& pal = option.palette;

        // the cover, scaled once per tile size and kept, so scrolling and
        // redrawing don't rescale images
        QPixmap cover;
        const QString key = index.data(Model::CoverKeyRole).toString();
        const qreal dpr = p->device() ? p->device()->devicePixelRatioF() : 1.0;
        const QString cacheKey = key + '@' + QString::number(tile) + '@' + QString::number(dpr);
        if (QPixmap* pm = scaled.object(cacheKey))
            cover = *pm;
        else
        {
            QImage img = index.data(Model::CoverRole).value<QImage>();
            if (!img.isNull())
            {
                QSize s = img.size().scaled(art.size() * dpr, Qt::KeepAspectRatio);
                cover = QPixmap::fromImage(img.scaled(s, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
                cover.setDevicePixelRatio(dpr);
                scaled.insert(cacheKey, new QPixmap(cover), std::max(1, s.width() * s.height() * 4 / 1024));
            }
        }

        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        QRect frame;
        if (!cover.isNull())
        {
            QSize s = (QSizeF(cover.size()) / cover.devicePixelRatio()).toSize();
            frame = QRect(QPoint(art.center().x() - s.width() / 2, art.bottom() - s.height() + 1), s);
            p->drawPixmap(frame, cover);
            p->setPen(QPen(QColor(0, 0, 0, 60), 1));
            p->setBrush(Qt::NoBrush);
            p->drawRect(QRectF(frame).adjusted(0.5, 0.5, -0.5, -0.5));
        }
        else
        {
            // a plain card with the game's own icon
            frame = art.adjusted(art.width() / 12, art.height() / 12, -art.width() / 12, 0);
            p->setPen(QPen(pal.color(QPalette::Mid), 1));
            p->setBrush(pal.color(QPalette::AlternateBase));
            p->drawRoundedRect(QRectF(frame).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
            QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
            if (!icon.isNull())
            {
                int is = std::min(64, std::min(frame.width(), frame.height()) * 2 / 3);
                QPixmap pm = icon.pixmap(32, 32);
                p->setRenderHint(QPainter::SmoothPixmapTransform, false); // pixel art
                p->drawPixmap(QRect(frame.center().x() - is / 2, frame.center().y() - is / 2, is, is), pm);
            }
        }

        // its own settings: a gear in the cover's corner
        if (index.data(Model::HasSettingsRole).toBool() && !frame.isNull())
        {
            const int g = std::clamp(frame.width() / 7, 12, 22);
            const QRectF badge(frame.right() - g - 3, frame.top() + 3, g + 2, g + 2);
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(0, 0, 0, 150));
            p->drawEllipse(badge);
            drawGear(p, badge.adjusted(2, 2, -2, -2), QColor(255, 255, 255, 230));
        }

        // mastered: a star in the other corner
        if (index.data(Model::StarRole).toBool() && !frame.isNull())
        {
            const int g = std::clamp(frame.width() / 7, 12, 22);
            const QRectF badge(frame.left() + 1, frame.top() + 3, g + 2, g + 2);
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(0, 0, 0, 150));
            p->drawEllipse(badge);
            drawStar(p, badge.adjusted(2, 2, -2, -2), kGold);
        }

        // selected: an outline in the theme's selection color around the cover
        if (option.state & QStyle::State_Selected)
        {
            QPen pen(pal.color(QPalette::Active, QPalette::Highlight), 3);
            p->setPen(pen);
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(QRectF(frame).adjusted(-2.5, -2.5, 2.5, 2.5), 4, 4);
        }
        p->restore();

        // two lines of title
        p->save();
        p->setPen(pal.color(QPalette::Text));
        QRect textRect(r.left(), art.bottom() + 6, r.width(), r.bottom() - art.bottom() - 6);
        const QFontMetrics& fm = option.fontMetrics;
        QString line1, rest;
        // break at a word if the title doesn't fit on one line
        if (fm.horizontalAdvance(title) <= textRect.width())
            line1 = title;
        else
        {
            int cut = title.size();
            while (cut > 0 && fm.horizontalAdvance(title.left(cut)) > textRect.width()) cut--;
            int space = title.lastIndexOf(' ', cut);
            if (space > 0) cut = space;
            line1 = title.left(cut).trimmed();
            rest = title.mid(cut).trimmed();
        }
        p->drawText(QRect(textRect.left(), textRect.top(), textRect.width(), fm.lineSpacing()),
                    Qt::AlignHCenter | Qt::AlignTop, line1);
        if (!rest.isEmpty())
            p->drawText(QRect(textRect.left(), textRect.top() + fm.lineSpacing(), textRect.width(), fm.lineSpacing()),
                        Qt::AlignHCenter | Qt::AlignTop, fm.elidedText(rest, Qt::ElideRight, textRect.width()));
        p->restore();
    }

private:
    mutable QCache<QString, QPixmap> scaled;
};

// the List and Grid buttons' icons, drawn in the theme's text color so they
// suit light and dark themes
QIcon viewIcon(bool grid, const QColor& color, qreal dpr)
{
    const int size = 16;
    QPixmap pm(QSize(size, size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    if (grid)
    {
        // four squares
        for (int y = 0; y < 2; y++)
            for (int x = 0; x < 2; x++)
                p.drawRoundedRect(QRectF(2 + x * 7, 2 + y * 7, 5, 5), 1, 1);
    }
    else
    {
        // three rows: a small square and a line
        for (int i = 0; i < 3; i++)
        {
            qreal y = 2.5 + i * 4.5;
            p.drawRoundedRect(QRectF(1.5, y, 3, 3), 0.6, 0.6);
            p.drawRoundedRect(QRectF(6, y + 0.5, 8.5, 2), 0.6, 0.6);
        }
    }
    p.end();
    return QIcon(pm);
}

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
    view->setItemDelegate(new RowDelegate(view));
    view->setItemDelegateForColumn(Model::Col_Region, new CenteredDelegate(view));
    view->setItemDelegateForColumn(Model::Col_Achievements, new AchievementsDelegate(view));
    model->setShowStars(showMasteryStars());
    // no hover effect: nothing to redraw while the mouse moves over the list
    view->viewport()->setAttribute(Qt::WA_Hover, false);
    model->setShowRegionNames(showRegionNames());
    // every column keeps the width it's dragged to; the list scrolls sideways
    // if they add up to more than the window
    view->header()->setStretchLastSection(false);
    view->header()->setSectionResizeMode(QHeaderView::Interactive);
    view->header()->setMinimumSectionSize(40);

    // the grid: box art tiles, same games, sorting and search
    grid = new QListView();
    grid->setModel(proxy);
    grid->setModelColumn(Model::Col_Title);
    grid->setViewMode(QListView::IconMode);
    grid->setResizeMode(QListView::Adjust);
    grid->setMovement(QListView::Static);
    grid->setUniformItemSizes(true);
    grid->setSelectionMode(QAbstractItemView::SingleSelection);
    grid->setEditTriggers(QAbstractItemView::NoEditTriggers);
    grid->setSpacing(0);
    grid->setContextMenuPolicy(Qt::CustomContextMenu);
    grid->setItemDelegate(new GridDelegate(grid));
    grid->viewport()->installEventFilter(this);
    grid->viewport()->setAttribute(Qt::WA_Hover, false);
    grid->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    views = new QStackedWidget();
    views->addWidget(view);
    views->addWidget(grid);
    listLayout->addWidget(views, 1);

    // bottom bar: list or grid on the left, the game count in the middle,
    // cover downloads and the tile size on the right
    auto* barGrid = new QGridLayout();
    barGrid->setContentsMargins(0, 0, 0, 0);
    barGrid->setColumnStretch(0, 1);
    barGrid->setColumnStretch(2, 1);
    auto* bar = new QHBoxLayout();
    auto* barRight = new QHBoxLayout();
    listButton = new QToolButton();
    listButton->setText("List");
    listButton->setToolTip("List");
    gridButton = new QToolButton();
    gridButton->setText("Grid");
    gridButton->setToolTip("Grid");
    const QColor iconColor = palette().color(QPalette::ButtonText);
    const qreal dpr = devicePixelRatioF() > 1 ? devicePixelRatioF() : 2.0; // sharp when moved to a high-DPI screen
    listButton->setIcon(viewIcon(false, iconColor, dpr));
    gridButton->setIcon(viewIcon(true, iconColor, dpr));
    for (QToolButton* b : {listButton, gridButton})
    {
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setIconSize(QSize(16, 16));
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setAutoRaise(true);
        bar->addWidget(b);
    }
    bar->addStretch(1);
    status = new QLabel();
    status->setAlignment(Qt::AlignCenter);

    coverProgress = new QProgressBar();
    coverProgress->setFixedWidth(160);
    coverProgress->setMaximumHeight(16);
    coverProgress->setFormat("Covers: %v / %m");
    coverProgress->setToolTip("Downloading box art");
    coverProgress->hide();
    coverStop = new QToolButton();
    coverStop->setAutoRaise(true);
    coverStop->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
    coverStop->setToolTip("Stop downloading covers");
    coverStop->hide();
    barRight->addStretch(1);
    barRight->addWidget(coverProgress);
    barRight->addWidget(coverStop);
    barRight->addSpacing(8);
    sizeLabel = new QLabel("Size:");
    barRight->addWidget(sizeLabel);
    sizeSlider = new QSlider(Qt::Horizontal);
    sizeSlider->setRange(kMinTile, kMaxTile);
    sizeSlider->setSingleStep(8);
    sizeSlider->setPageStep(32);
    sizeSlider->setFixedWidth(140);
    sizeSlider->setToolTip("Tile size (Ctrl + mouse wheel)");
    barRight->addWidget(sizeSlider);
    barGrid->addLayout(bar, 0, 0);
    barGrid->addWidget(status, 0, 1);
    barGrid->addLayout(barRight, 0, 2);
    listLayout->addLayout(barGrid);

    connect(Covers::get(), &Covers::downloadProgress, this, [this](int done, int total)
    {
        coverProgress->setRange(0, std::max(1, total));
        coverProgress->setValue(done);
        coverProgress->show();
        coverStop->show();
    });
    connect(Covers::get(), &Covers::downloadsFinished, this, [this]()
    {
        coverProgress->hide();
        coverStop->hide();
    });
    connect(coverStop, &QToolButton::clicked, Covers::get(), &Covers::stopDownloads);
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
    connect(grid, &QListView::activated, this, &Widget::bootIndex);
    connect(view, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) { showContextMenu(view, pos); });
    connect(grid, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) { showContextMenu(grid, pos); });
    connect(listButton, &QToolButton::clicked, this, [this]() { setGridMode(false); });
    connect(gridButton, &QToolButton::clicked, this, [this]() { setGridMode(true); });
    connect(sizeSlider, &QSlider::valueChanged, this, &Widget::setTileSize);
    // keep the same game selected in both views
    connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& cur)
    {
        if (cur.isValid() && views->currentWidget() == view)
            grid->setCurrentIndex(proxy->index(cur.row(), Model::Col_Title));
    });
    connect(grid->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& cur)
    {
        if (cur.isValid() && views->currentWidget() == grid)
            view->setCurrentIndex(cur);
    });

    sizeSlider->setValue(tileSize());
    setTileSize(tileSize());
    setGridMode(gridMode());
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

    // RetroAchievements progress for the Achievements column and the stars
    if (RA::Manager* ra = RA::Manager::get())
    {
        connect(&ra->library(), &RA::Library::changed, this, &Widget::pushProgress);
        connect(ra, &RA::Manager::stateChanged, this, [this, ra]()
        {
            // back from playing: the unlocks may have changed
            const bool loaded = ra->isGameLoaded();
            if (raGameWasLoaded && !loaded)
                ra->library().refreshProgress();
            raGameWasLoaded = loaded;
            updateAchievements();
        });
        updateAchievements();
    }

    refresh();
}

bool Widget::wantsAchievements() const
{
    return !view->header()->isSectionHidden(Model::Col_Achievements) || showMasteryStars();
}

void Widget::updateAchievements()
{
    RA::Manager* ra = RA::Manager::get();
    if (!ra) return;
    // the library only goes online once something here shows its results
    if (ra->isEnabled() && wantsAchievements() && !ra->library().isActive())
        ra->library().activate();
    pushProgress();
}

void Widget::pushProgress()
{
    RA::Manager* ra = RA::Manager::get();
    QHash<QString, Model::Progress> byPath;
    if (ra && ra->isEnabled() && ra->isLoggedIn() && ra->library().isActive())
    {
        for (const RA::Library::Game& g : ra->library().games())
        {
            Model::Progress pr;
            if (!g.matched) continue;
            if (!g.gameId)
                pr.state = Model::Progress::NotOnRA;
            else if (g.hasProgress)
            {
                pr.state = Model::Progress::Known;
                pr.total = g.numAchievements;
                pr.unlocked = g.numUnlocked;
                pr.hardcore = g.numUnlockedHardcore;
            }
            else
                continue;
            byPath.insert(g.path, pr);
        }
    }
    model->setProgress(byPath);
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

    // on the first scan, show the saved list right away
    if (!savedListShown)
    {
        savedListShown = true;
        showSavedList();
    }

    // what's on screen now is what the scan checks against
    auto known = std::make_shared<std::map<QString, Game>>();
    for (const Game& g : model->games()) known->emplace(g.path, g);
    for (const Game& g : rejectedFiles) known->emplace(g.path, g);
    const SavedHeader header = currentHeader();

    QPointer<Widget> self(this);
    QThread* thread = QThread::create([self, dirs, recursive, serial, cancel, known, header]()
    {
        int read = 0;
        std::vector<Game> rejected;
        std::vector<Game> games = scanFolders(dirs, recursive, *cancel, known.get(), &read, &rejected);
        if (*cancel) return;
        std::vector<Game> all = games;
        all.insert(all.end(), rejected.begin(), rejected.end());
        const bool changed = read > 0 || !sameFiles(all, *known);
        if (changed || !QFile::exists(savedListPath()))
            saveList(header, all);
        QMetaObject::invokeMethod(qApp, [self, serial, changed, games = std::move(games),
                                         rejected = std::move(rejected)]() mutable
        {
            if (!self) return;
            self->rejectedFiles = std::move(rejected);
            self->scanFinished(serial, std::move(games), changed);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    scanThread = thread;
    thread->start(QThread::LowPriority);
}

bool Widget::showSavedList()
{
    std::vector<Game> games;
    if (!loadList(currentHeader(), games, rejectedFiles) || games.empty()) return false;

    {
        std::lock_guard<std::mutex> guard(scannedLock);
        scanned = games;
    }
    model->setGames(std::move(games));
    emit notifier()->gamesChanged();
    view->sortByColumn(view->header()->sortIndicatorSection(), view->header()->sortIndicatorOrder());
    updatePage();
    return true;
}

void Widget::scanFinished(int serial, std::vector<Game> games, bool changed)
{
    if (serial != scanSerial) return; // a newer scan is running

    scanning = false;
    // nothing new: leave the list (and the selection and scrolling) alone
    if (changed)
    {
        {
            std::lock_guard<std::mutex> guard(scannedLock);
            scanned = games;
        }
        model->setGames(std::move(games));
        emit notifier()->gamesChanged();
        view->sortByColumn(view->header()->sortIndicatorSection(), view->header()->sortIndicatorOrder());
    }
    updatePage();
    // after the list is up, so the question (if any) shows over it
    QTimer::singleShot(0, this, &Widget::maybeDownloadCovers);
}

void Widget::refreshSettingsMarks()
{
    model->refreshSettingsMarks();
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
    if (path.isEmpty()) return;

    // DSiWare has to be installed on an emulated DSi first
    const int row = proxy->mapToSource(index).row();
    if (row >= 0 && row < (int)model->games().size() && model->games()[row].platform == "DSiWare")
    {
        emit dsiWareRequested(path, model->games()[row].title);
        return;
    }
    emit bootRequested(path);
}

void Widget::setGridMode(bool on)
{
    views->setCurrentWidget(on ? (QWidget*)grid : (QWidget*)view);
    listButton->setChecked(!on);
    gridButton->setChecked(on);
    sizeSlider->setEnabled(on);
    sizeLabel->setEnabled(on);
    GameList::setGridMode(on);
    if (on) maybeDownloadCovers();
}

void Widget::maybeDownloadCovers()
{
    if (views->currentWidget() != grid || model->games().empty())
        return;
    if (!Covers::useGameTDB() && !Covers::useLibretro())
        return;

    Covers* covers = Covers::get();
    const QString consent = Covers::downloadConsent();
    if (consent == "no")
        return;

    if (consent.isEmpty())
    {
        // ask once, before the first downloads
        if (askedAboutCovers) return;
        const int missing = covers->countMissing(model->games());
        if (missing == 0) return;
        askedAboutCovers = true;

        QMessageBox box(QMessageBox::Question, "Game Covers - melonRA",
                        QString("Download box art for %1 game%2?").arg(missing).arg(missing == 1 ? "" : "s"),
                        QMessageBox::NoButton, this);
        box.setInformativeText("Covers come from GameTDB and the libretro thumbnails. Each one is downloaded "
                               "once and kept on your computer. You can follow or stop the downloads in the "
                               "bar at the bottom of the list.\n\n"
                               "You can change this later in View → Game Covers.");
        QPushButton* yes = box.addButton("Download Covers", QMessageBox::AcceptRole);
        box.addButton("Not Now", QMessageBox::RejectRole);
        box.setDefaultButton(yes);
        box.exec();
        const bool ok = box.clickedButton() == yes;
        Covers::setDownloadConsent(ok ? "yes" : "no");
        Config::Save();
        if (!ok) return;
    }

    covers->downloadMissing(model->games());
}

void Widget::setTileSize(int size)
{
    size = std::clamp(size, kMinTile, kMaxTile);
    auto* delegate = static_cast<GridDelegate*>(grid->itemDelegate());
    delegate->setTile(size);
    // the delegate's size changed; have the view lay the tiles out again
    QStyleOptionViewItem opt;
    opt.initFrom(grid);
    grid->setGridSize(delegate->sizeHint(opt, QModelIndex()) + QSize(8, 8));
    grid->doItemsLayout();
    GameList::setTileSize(size);
    if (sizeSlider->value() != size)
        sizeSlider->setValue(size);
}

bool Widget::eventFilter(QObject* watched, QEvent* event)
{
    // Ctrl + mouse wheel resizes the tiles
    if (watched == grid->viewport() && event->type() == QEvent::Wheel)
    {
        auto* wheel = static_cast<QWheelEvent*>(event);
        if (wheel->modifiers() & Qt::ControlModifier)
        {
            int steps = wheel->angleDelta().y() / 120;
            if (steps) setTileSize(sizeSlider->value() + steps * 16);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

// Opens the file manager with the file selected
static void revealFile(const QString& path)
{
    const QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
#if defined(_WIN32)
    // explorer wants "/select," and the path as one argument, quoted as-is
    QProcess proc;
    proc.setProgram("explorer.exe");
    proc.setNativeArguments(QString("/select,\"%1\"").arg(native));
    if (proc.startDetached()) return;
#elif defined(__APPLE__)
    if (QProcess::startDetached("open", {"-R", native})) return;
#else
    // the freedesktop file manager interface (Dolphin, Nautilus, Nemo, Thunar...)
    const QString uri = QUrl::fromLocalFile(native).toString(QUrl::FullyEncoded);
    if (QProcess::startDetached("dbus-send",
            {"--session", "--print-reply", "--dest=org.freedesktop.FileManager1", "--type=method_call",
             "/org/freedesktop/FileManager1", "org.freedesktop.FileManager1.ShowItems",
             "array:string:" + uri, "string:"}))
        return;
#endif
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}

void Widget::showContextMenu(QAbstractItemView* from, const QPoint& pos)
{
    QModelIndex index = from->indexAt(pos);
    QString path = index.isValid() ? proxy->data(index, Model::PathRole).toString() : QString();

    QMenu menu(this);
    if (!path.isEmpty())
    {
        QAction* boot = menu.addAction("Boot");
        QFont f = boot->font();
        f.setBold(true);
        boot->setFont(f);
        connect(boot, &QAction::triggered, this, [this, index]() { bootIndex(index); });

#if defined(_WIN32)
        const char* revealText = "Show in Explorer";
#elif defined(__APPLE__)
        const char* revealText = "Show in Finder";
#else
        const char* revealText = "Show in File Manager";
#endif
        connect(menu.addAction(revealText), &QAction::triggered, this, [path]() { revealFile(path); });
        connect(menu.addAction("Open Containing Folder"), &QAction::triggered, this, [path]()
        {
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
        });
        connect(menu.addAction("Copy File Path"), &QAction::triggered, this, [path]()
        {
            QApplication::clipboard()->setText(QDir::toNativeSeparators(path));
        });
        menu.addSeparator();

        // the game's own settings
        const int row = proxy->mapToSource(proxy->index(index.row(), 0)).row();
        if (row >= 0 && row < (int)model->games().size())
        {
            const Game& g = model->games()[row];
            const QString id = settingsId(g);
            const QIcon icon = g.icon.isNull() ? QIcon() : QIcon(QPixmap::fromImage(g.icon));
            const QString title = g.title;
            const QString romPath = g.path;
            connect(menu.addAction("Game Settings..."), &QAction::triggered, this, [this, id, title, icon, romPath]()
            {
                emit gameSettingsRequested(id, title, icon, romPath);
            });

            // its achievements, when RetroAchievements is on
            RA::Manager* ra = RA::Manager::get();
            if (ra && ra->isEnabled())
            {
                const auto pr = model->data(model->index(row, Model::Col_Achievements), Model::ProgressRole)
                                    .value<Model::Progress>();
                QAction* ach = menu.addAction(pr.state == Model::Progress::NotOnRA
                                              ? "Achievements... (not on RetroAchievements)" : "Achievements...");
                ach->setEnabled(pr.state != Model::Progress::NotOnRA);
                connect(ach, &QAction::triggered, this, [this, romPath]() { emit achievementsRequested(romPath); });
            }
            menu.addSeparator();
        }
    }
    addViewOptions(menu);
    menu.addSeparator();
    connect(menu.addAction("Refresh"), &QAction::triggered, this, &Widget::refresh);
    connect(menu.addAction("Game Folders..."), &QAction::triggered, this, &Widget::openSettings);
    menu.exec(from->viewport()->mapToGlobal(pos));
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

    QAction* stars = menu.addAction("Show Mastery Stars");
    stars->setCheckable(true);
    stars->setChecked(showMasteryStars());
    stars->setToolTip("A star in front of games you've mastered on RetroAchievements");
    connect(stars, &QAction::toggled, this, [this](bool on)
    {
        setShowMasteryStars(on);
        model->setShowStars(on);
        updateAchievements();
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
            if (c == Model::Col_Achievements) updateAchievements();
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
    globalConfig().SetInt("GameList.ColumnCount", Model::NumColumns);
}

void Widget::restoreHeader()
{
    QHeaderView* header = view->header();
    QByteArray state = QByteArray::fromBase64(QByteArray::fromStdString(globalConfig().GetString(kColumnsKey)));
    if (!state.isEmpty() && header->restoreState(state))
    {
        header->setSectionResizeMode(QHeaderView::Interactive);
        // columns added since the layout was saved start hidden, at the end
        const int saved = globalConfig().GetInt("GameList.ColumnCount");
        if (saved < Model::NumColumns)
        {
            for (int c = std::max(saved, (int)Model::Col_Achievements); c < Model::NumColumns; c++)
            {
                header->moveSection(header->visualIndex(c), Model::NumColumns - 1);
                header->setSectionHidden(c, true);
                header->resizeSection(c, 170);
            }
            saveHeader();
        }
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
    header->setSectionHidden(Model::Col_Achievements, true);
    header->resizeSection(Model::Col_Achievements, 170);
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
