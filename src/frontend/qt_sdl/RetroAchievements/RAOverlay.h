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

#ifndef RA_OVERLAY_H
#define RA_OVERLAY_H

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <QColor>
#include <QFont>
#include <QImage>
#include <QRectF>
#include <QString>

class QPainter;

namespace RA
{

class ImageCache;

// On-screen RetroAchievements widgets: notification popups, challenge
// indicators, the progress indicator and leaderboard trackers.
//
// State updates come from rc_client events (emulator thread or HTTP worker
// threads); painting happens on the GUI thread (software renderer) or the
// emulator thread (OpenGL renderer). Everything is guarded by one mutex.
class Overlay
{
public:
    enum class PopupKind
    {
        Info,
        Account,
        GameStart,
        Unlock,
        Mastery,
        Leaderboard,
        Error,
    };

    // the groups of widgets that can be placed and styled separately
    enum class Element
    {
        Notifications,            // game placard, unlocks, mastery, account, errors
        LeaderboardNotifications, // leaderboard started/failed/submitted/rank
        Challenges,
        Progress,
        Trackers,
    };
    static constexpr int kNumElements = 5;

    enum class Anchor
    {
        TopLeft,
        TopCenter,
        TopRight,
        BottomLeft,
        BottomCenter,
        BottomRight,
    };
    static constexpr int kNumAnchors = 6;

    struct ElementStyle
    {
        Anchor anchor = Anchor::TopRight;
        bool rounded = true;
        bool bold = true;
        bool border = false;
    };

    struct Style
    {
        ElementStyle elements[kNumElements];
        // multiplies the automatic size, which follows the window size
        float scale = 1.f;

        ElementStyle& operator[](Element e) { return elements[(int)e]; }
        const ElementStyle& operator[](Element e) const { return elements[(int)e]; }

        static Style defaults();
    };

    explicit Overlay(ImageCache& images);

    // --- notifications ---
    void addPopup(PopupKind kind, const std::string& iconUrl,
                  const QString& title, const QString& line1,
                  const QString& line2 = QString(), int durationMs = 5000);

    // --- challenge indicators (one small badge per primed achievement) ---
    void showChallenge(uint32_t achievementId, const std::string& iconUrl);
    void hideChallenge(uint32_t achievementId);

    // --- progress indicator (single, most recent measured progress) ---
    void showProgress(const std::string& iconUrl, const QString& text);
    void hideProgress();

    // --- leaderboard trackers ---
    void showTracker(uint32_t trackerId, const QString& text);
    void hideTracker(uint32_t trackerId);

    // hides challenge/progress/tracker widgets (emulator reset, game unload)
    void clearIndicators();
    // also drops queued and visible popups
    void clearAll();

    // per-widget visibility, from the user's settings
    void setOptions(bool popups, bool challenges, bool progress, bool trackers);

    // positions and looks, from the user's settings
    void setStyle(const Style& style);

    // forces a redraw (e.g. after a badge image finished downloading)
    void invalidate();

    // true if anything would currently be drawn
    bool hasContent();

    // Draws everything into the given logical-pixel area.
    void paint(QPainter& painter, int width, int height);

    // Renders into a premultiplied ARGB image sized for the given surface.
    // lastVersion is owned by the caller (one per target image). Returns
    // false (and leaves the image untouched) if nothing has changed since the
    // last render into that image, so callers can skip re-uploading textures.
    bool renderToImage(QImage& image, uint64_t& lastVersion, int deviceWidth, int deviceHeight, float scale);

private:
    struct Popup
    {
        PopupKind kind;
        std::string iconUrl;
        QString title, line1, line2;
        int durationMs;
        int64_t shownAt; // -1 while still queued
    };

    struct Tracker
    {
        QString text;
        int64_t order;
    };

    // one box on screen, sized first and then placed by its element's anchor
    struct Item
    {
        QSizeF size;
        float enter = 1.f;  // 0 -> 1 while sliding/fading in
        float alpha = 1.f;
        std::function<void(QPainter&, const QRectF&)> draw;
    };

    static int64_t nowMs();
    std::deque<Popup>& queueFor(PopupKind kind);

    void popupItems(std::deque<Popup>& queue, const ElementStyle& st, std::vector<Item>& out,
                    int width, float ui, int64_t now);
    void challengeItems(const ElementStyle& st, std::vector<Item>& out, int width, float ui);
    void progressItems(const ElementStyle& st, std::vector<Item>& out, float ui, int64_t now);
    void trackerItems(const ElementStyle& st, std::vector<Item>& out, float ui);
    void placeItems(QPainter& p, std::vector<Item> (&items)[kNumElements], int width, int height, float ui);

    // the box behind every widget, honoring the shape and border options
    static void drawBox(QPainter& p, const QRectF& box, const ElementStyle& st, float ui,
                        const QColor& fill, const QColor& borderColor, const QColor& accent = QColor());
    void drawIcon(QPainter& p, const std::string& url, const QRectF& rect, bool rounded = true);
    static QColor accentColor(PopupKind kind);

    ImageCache& images;

    // resolved once on the GUI thread; painting may happen on the emu thread
    QFont fixedFont;

    std::mutex lock;

    // achievement/game popups, and leaderboard popups, which can be placed elsewhere
    std::deque<Popup> popups;
    std::deque<Popup> lbPopups;
    std::map<uint32_t, std::string> challenges;
    bool progressVisible = false;
    std::string progressIcon;
    QString progressText;
    int64_t progressShownAt = 0;
    std::map<uint32_t, Tracker> trackers;
    int64_t trackerCounter = 0;

    bool showPopups = true;
    bool showChallenges = true;
    bool showProgressInd = true;
    bool showTrackers = true;

    Style style = Style::defaults();

    // bumped on every visible change, for renderToImage
    uint64_t version = 1;
};

}

#endif // RA_OVERLAY_H
