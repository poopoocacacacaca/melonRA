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

#include "RAOverlay.h"
#include "RAImageCache.h"

#include <algorithm>
#include <chrono>
#include <vector>

#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

namespace RA
{

// layout constants, in logical pixels at a UI scale of 1.0
static constexpr int kMaxVisiblePopups = 3;
static constexpr int kSlideInMs = 220;
static constexpr int kFadeOutMs = 450;
static constexpr int kProgressHoldMs = 2500;
static constexpr float kMargin = 10.f;
static constexpr float kPopupWidth = 260.f;      // narrowest popup
static constexpr float kPopupMaxShare = 0.42f;   // widest popup, as a share of the window
static constexpr float kPopupIcon = 38.f;
static constexpr float kPopupPad = 6.f;
static constexpr float kChallengeIcon = 26.f;

Overlay::Style Overlay::Style::defaults()
{
    Style st;
    st[Element::Notifications].anchor = Anchor::TopRight;
    st[Element::LeaderboardNotifications].anchor = Anchor::TopRight;
    st[Element::Challenges].anchor = Anchor::BottomLeft;
    st[Element::Progress].anchor = Anchor::BottomLeft;
    st[Element::Trackers].anchor = Anchor::BottomRight;
    return st;
}

Overlay::Overlay(ImageCache& images) : images(images)
{
    fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
}

int64_t Overlay::nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::deque<Overlay::Popup>& Overlay::queueFor(PopupKind kind)
{
    return kind == PopupKind::Leaderboard ? lbPopups : popups;
}

void Overlay::addPopup(PopupKind kind, const std::string& iconUrl,
                       const QString& title, const QString& line1,
                       const QString& line2, int durationMs)
{
    images.prefetch(iconUrl);

    std::lock_guard<std::mutex> guard(lock);
    queueFor(kind).push_back({kind, iconUrl, title, line1, line2, durationMs, -1});
    version++;
}

void Overlay::showChallenge(uint32_t achievementId, const std::string& iconUrl)
{
    images.prefetch(iconUrl);

    std::lock_guard<std::mutex> guard(lock);
    challenges[achievementId] = iconUrl;
    version++;
}

void Overlay::hideChallenge(uint32_t achievementId)
{
    std::lock_guard<std::mutex> guard(lock);
    challenges.erase(achievementId);
    version++;
}

void Overlay::showProgress(const std::string& iconUrl, const QString& text)
{
    images.prefetch(iconUrl);

    std::lock_guard<std::mutex> guard(lock);
    progressVisible = true;
    progressIcon = iconUrl;
    progressText = text;
    progressShownAt = nowMs();
    version++;
}

void Overlay::hideProgress()
{
    std::lock_guard<std::mutex> guard(lock);
    progressVisible = false;
    version++;
}

void Overlay::showTracker(uint32_t trackerId, const QString& text)
{
    std::lock_guard<std::mutex> guard(lock);
    auto it = trackers.find(trackerId);
    if (it == trackers.end())
        trackers[trackerId] = {text, trackerCounter++};
    else
        it->second.text = text;
    version++;
}

void Overlay::hideTracker(uint32_t trackerId)
{
    std::lock_guard<std::mutex> guard(lock);
    trackers.erase(trackerId);
    version++;
}

void Overlay::clearIndicators()
{
    std::lock_guard<std::mutex> guard(lock);
    challenges.clear();
    progressVisible = false;
    trackers.clear();
    version++;
}

void Overlay::clearAll()
{
    std::lock_guard<std::mutex> guard(lock);
    challenges.clear();
    progressVisible = false;
    trackers.clear();
    popups.clear();
    lbPopups.clear();
    version++;
}

void Overlay::setOptions(bool popups_, bool challenges_, bool progress_, bool trackers_)
{
    std::lock_guard<std::mutex> guard(lock);
    showPopups = popups_;
    showChallenges = challenges_;
    showProgressInd = progress_;
    showTrackers = trackers_;
    if (!showPopups)
    {
        popups.clear();
        lbPopups.clear();
    }
    version++;
}

void Overlay::setStyle(const Style& st)
{
    std::lock_guard<std::mutex> guard(lock);
    style = st;
    version++;
}

void Overlay::invalidate()
{
    std::lock_guard<std::mutex> guard(lock);
    version++;
}

bool Overlay::hasContent()
{
    std::lock_guard<std::mutex> guard(lock);
    int64_t now = nowMs();

    // drop expired popups here too, so an idle overlay reports "empty"
    for (std::deque<Popup>* queue : {&popups, &lbPopups})
    {
        while (!queue->empty() && queue->front().shownAt >= 0 &&
               now - queue->front().shownAt > queue->front().durationMs)
        {
            queue->pop_front();
            version++;
        }
    }

    if (progressVisible && now - progressShownAt > kProgressHoldMs + kFadeOutMs)
    {
        progressVisible = false;
        version++;
    }

    return (showPopups && (!popups.empty() || !lbPopups.empty())) ||
           (showChallenges && !challenges.empty()) ||
           (showProgressInd && progressVisible) ||
           (showTrackers && !trackers.empty());
}

QColor Overlay::accentColor(PopupKind kind)
{
    switch (kind)
    {
    case PopupKind::Unlock:      return QColor(0xE8, 0xB9, 0x3A);
    case PopupKind::Mastery:     return QColor(0xF0, 0xD0, 0x60);
    case PopupKind::GameStart:   return QColor(0x5A, 0xA9, 0xE6);
    case PopupKind::Account:     return QColor(0xEB, 0xED, 0xF0);  // plain, no color
    case PopupKind::Leaderboard: return QColor(0x6C, 0xC4, 0x8C);
    case PopupKind::Error:       return QColor(0xE0, 0x5A, 0x5A);
    case PopupKind::Info:
    default:                     return QColor(0x9A, 0xA4, 0xB0);
    }
}

void Overlay::drawBox(QPainter& p, const QRectF& box, const ElementStyle& st, float ui,
                      const QColor& fill, const QColor& borderColor, const QColor& accent)
{
    const float radius = st.rounded ? 5 * ui : 0.f;
    QPainterPath path;
    path.addRoundedRect(box, radius, radius);

    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawPath(path);

    if (accent.isValid())
    {
        // accent bar along the left edge, following the box's corners
        p.save();
        p.setClipPath(path);
        p.fillRect(QRectF(box.left(), box.top(), 3 * ui, box.height()), accent);
        p.restore();
    }

    if (st.border)
    {
        const float w = std::max(1.f, 1.5f * ui);
        QPainterPath inner;
        QRectF r = box.adjusted(w / 2, w / 2, -w / 2, -w / 2);
        inner.addRoundedRect(r, std::max(0.f, radius - w / 2), std::max(0.f, radius - w / 2));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(borderColor, w));
        p.drawPath(inner);
    }
}

void Overlay::drawIcon(QPainter& p, const std::string& url, const QRectF& rect, bool rounded)
{
    QImage img = images.get(url);

    QPainterPath clip;
    float radius = rounded ? rect.width() * 0.12f : 0.f;
    clip.addRoundedRect(rect, radius, radius);

    p.save();
    p.setClipPath(clip);
    if (img.isNull())
        p.fillRect(rect, QColor(0x3A, 0x3D, 0x46));  // until the badge has downloaded
    else
        p.drawImage(rect, img);
    p.restore();
}

void Overlay::paint(QPainter& p, int width, int height)
{
    std::lock_guard<std::mutex> guard(lock);

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    // scale the widgets with the window, within sane limits
    // (melonDS defaults to a small 256x384 window, so keep a readable floor)
    float ui = std::clamp(std::min(width / 640.f, height / 480.f), 0.65f, 1.3f) * style.scale;
    int64_t now = nowMs();

    std::vector<Item> items[kNumElements];
    if (showPopups)
    {
        popupItems(popups, style[Element::Notifications], items[(int)Element::Notifications], width, ui, now);
        popupItems(lbPopups, style[Element::LeaderboardNotifications],
                   items[(int)Element::LeaderboardNotifications], width, ui, now);
    }
    if (showChallenges) challengeItems(style[Element::Challenges], items[(int)Element::Challenges], width, ui);
    if (showProgressInd) progressItems(style[Element::Progress], items[(int)Element::Progress], ui, now);
    if (showTrackers) trackerItems(style[Element::Trackers], items[(int)Element::Trackers], ui);

    placeItems(p, items, width, height, ui);
}

void Overlay::popupItems(std::deque<Popup>& queue, const ElementStyle& st, std::vector<Item>& out,
                         int width, float ui, int64_t now)
{
    // retire finished popups, promote queued ones
    for (auto it = queue.begin(); it != queue.end();)
    {
        if (it->shownAt >= 0 && now - it->shownAt > it->durationMs)
            it = queue.erase(it);
        else
            ++it;
    }

    const float margin = kMargin * ui;
    const float icon = kPopupIcon * ui;
    const float pad = kPopupPad * ui;
    const float bar = 3 * ui;
    const float minW = kPopupWidth * ui;
    // use the room a wide window has, so long names and descriptions fit on fewer lines
    const float maxW = std::max(minW, width * kPopupMaxShare);
    const float limitW = width - 2 * margin;

    QFont titleFont;
    titleFont.setPixelSize(std::max(10, (int)(13 * ui)));
    titleFont.setBold(st.bold);
    QFont bodyFont = titleFont;
    bodyFont.setBold(false);
    bodyFont.setPixelSize(std::max(9, (int)(11 * ui)));
    QFont smallFont = bodyFont;
    smallFont.setPixelSize(std::max(8, (int)(10 * ui)));
    QFontMetricsF tfm(titleFont), bfm(bodyFont), sfm(smallFont);

    const int wrap = Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap;

    int visible = 0;
    for (Popup& pop : queue)
    {
        if (visible >= kMaxVisiblePopups) break;
        visible++;
        if (pop.shownAt < 0) pop.shownAt = now;

        const bool hasIcon = !pop.iconUrl.empty();
        const float chrome = bar + pad + (hasIcon ? icon + pad : 0.f) + pad;

        // as wide as the longest line needs, between the minimum and maximum width
        float want = std::max({tfm.horizontalAdvance(pop.title), bfm.horizontalAdvance(pop.line1),
                               pop.line2.isEmpty() ? 0.0 : sfm.horizontalAdvance(pop.line2)});
        float w = std::clamp(want + chrome + 2.f, minW, maxW);
        w = std::min(w, limitW);
        const float textW = std::max(1.f, w - chrome);

        // anything still too long wraps onto more lines instead of being cut off
        auto textHeight = [&](const QFontMetricsF& fm, const QString& text)
        {
            if (text.isEmpty()) return 0.0;
            return fm.boundingRect(QRectF(0, 0, textW, 1e6), wrap, text).height();
        };
        const float th = textHeight(tfm, pop.title);
        const float bh = textHeight(bfm, pop.line1);
        const float sh = textHeight(sfm, pop.line2);
        const float textH = th + bh + sh;
        const float h = std::max(hasIcon ? icon : 0.f, textH) + 2 * pad;

        Item item;
        item.size = QSizeF(w, h);
        int64_t age = now - pop.shownAt;
        float enter = std::min(1.f, age / (float)kSlideInMs);
        item.enter = 1.f - (1.f - enter) * (1.f - enter); // ease out
        int64_t remaining = pop.durationMs - age;
        if (remaining < kFadeOutMs) item.alpha = std::max(0.f, remaining / (float)kFadeOutMs);

        const Popup* pp = &pop;
        item.draw = [=](QPainter& p, const QRectF& box)
        {
            QColor accent = accentColor(pp->kind);
            // the account popup is plain black, without a colored stripe
            const bool plain = pp->kind == PopupKind::Account;
            drawBox(p, box, st, ui, QColor(18, 20, 26, 232),
                    plain ? QColor(255, 255, 255, 110) : accent, plain ? QColor() : accent);

            QRectF iconRect(box.left() + bar + pad, box.top() + (box.height() - icon) / 2, icon, icon);
            if (hasIcon)
                drawIcon(p, pp->iconUrl, iconRect, st.rounded);

            float x = hasIcon ? iconRect.right() + pad : box.left() + bar + pad;
            float y = box.top() + (box.height() - textH) / 2;

            p.setPen(accent);
            p.setFont(titleFont);
            p.drawText(QRectF(x, y, textW, th), wrap, pp->title);
            y += th;

            p.setPen(QColor(235, 237, 240));
            p.setFont(bodyFont);
            p.drawText(QRectF(x, y, textW, bh), wrap, pp->line1);
            y += bh;

            if (sh > 0)
            {
                p.setPen(QColor(160, 166, 176));
                p.setFont(smallFont);
                p.drawText(QRectF(x, y, textW, sh), wrap, pp->line2);
            }
        };
        out.push_back(std::move(item));
    }
}

void Overlay::challengeItems(const ElementStyle& st, std::vector<Item>& out, int width, float ui)
{
    if (challenges.empty()) return;

    const float size = kChallengeIcon * ui;
    const float pad = 3 * ui;
    const float gap = 6 * ui;
    const float limitW = width - 2 * kMargin * ui;

    // a row of small badges
    std::vector<std::string> urls;
    float w = 0;
    for (const auto& [id, url] : challenges)
    {
        float next = w + (urls.empty() ? 0 : gap) + size + 2 * pad;
        if (next > limitW) break;
        urls.push_back(url);
        w = next;
    }
    if (urls.empty()) return;

    Item item;
    item.size = QSizeF(w, size + 2 * pad);
    item.draw = [=](QPainter& p, const QRectF& box)
    {
        float x = box.left();
        for (const std::string& url : urls)
        {
            QRectF cell(x, box.top(), size + 2 * pad, size + 2 * pad);
            drawBox(p, cell, st, ui, QColor(0, 0, 0, 150), QColor(255, 255, 255, 110));
            drawIcon(p, url, cell.adjusted(pad, pad, -pad, -pad), st.rounded);
            x += cell.width() + gap;
        }
    };
    out.push_back(std::move(item));
}

void Overlay::progressItems(const ElementStyle& st, std::vector<Item>& out, float ui, int64_t now)
{
    if (!progressVisible) return;

    int64_t age = now - progressShownAt;
    float alpha = 1.f;
    if (age > kProgressHoldMs)
        alpha = std::max(0.f, 1.f - (age - kProgressHoldMs) / (float)kFadeOutMs);
    if (alpha <= 0.f)
    {
        progressVisible = false;
        return;
    }

    QFont font;
    font.setPixelSize(std::max(8, (int)(12 * ui)));
    font.setBold(st.bold);
    QFontMetricsF fm(font);

    const float icon = 24 * ui;
    const float pad = 4 * ui;
    const float textW = fm.horizontalAdvance(progressText);

    Item item;
    item.size = QSizeF(icon + textW + 3 * pad, icon + 2 * pad);
    item.alpha = alpha;
    const std::string iconUrl = progressIcon;
    const QString text = progressText;
    item.draw = [=](QPainter& p, const QRectF& box)
    {
        drawBox(p, box, st, ui, QColor(18, 20, 26, 225), QColor(255, 255, 255, 110));
        drawIcon(p, iconUrl, QRectF(box.left() + pad, box.top() + pad, icon, icon), st.rounded);
        p.setFont(font);
        p.setPen(QColor(235, 237, 240));
        p.drawText(QRectF(box.left() + 2 * pad + icon, box.top(), textW + pad, box.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, text);
    };
    out.push_back(std::move(item));
}

void Overlay::trackerItems(const ElementStyle& st, std::vector<Item>& out, float ui)
{
    if (trackers.empty()) return;

    // fixed-width font so timers don't jitter as digits change
    QFont font = fixedFont;
    font.setPixelSize(std::max(9, (int)(13 * ui)));
    font.setBold(st.bold);
    QFontMetricsF fm(font);

    std::vector<const Tracker*> ordered;
    for (const auto& [id, tr] : trackers) ordered.push_back(&tr);
    std::sort(ordered.begin(), ordered.end(),
              [](const Tracker* a, const Tracker* b) { return a->order < b->order; });

    const float padX = 7 * ui, padY = 2 * ui;
    for (const Tracker* tr : ordered)
    {
        const QString text = tr->text;
        Item item;
        item.size = QSizeF(fm.horizontalAdvance(text) + 2 * padX, fm.height() + 2 * padY);
        item.draw = [=](QPainter& p, const QRectF& box)
        {
            drawBox(p, box, st, ui, QColor(18, 20, 26, 215), QColor(255, 255, 255, 110));
            p.setFont(font);
            p.setPen(QColor(240, 242, 245));
            p.drawText(box, Qt::AlignCenter, text);
        };
        out.push_back(std::move(item));
    }
}

void Overlay::placeItems(QPainter& p, std::vector<Item> (&items)[kNumElements], int width, int height, float ui)
{
    const float margin = kMargin * ui;
    const float gap = 6 * ui;       // between boxes of one element
    const float groupGap = 10 * ui; // between elements sharing a position

    for (int a = 0; a < kNumAnchors; a++)
    {
        const Anchor anchor = (Anchor)a;

        // everything placed here, in element order
        std::vector<std::pair<Item*, float>> list; // item, space before it
        for (int e = 0; e < kNumElements; e++)
        {
            if (style.elements[e].anchor != anchor) continue;
            for (size_t i = 0; i < items[e].size(); i++)
            {
                float before = list.empty() ? 0.f : (i == 0 ? groupGap : gap);
                list.push_back({&items[e][i], before});
            }
        }
        if (list.empty()) continue;

        const bool left = anchor == Anchor::TopLeft || anchor == Anchor::BottomLeft;
        const bool right = anchor == Anchor::TopRight || anchor == Anchor::BottomRight;
        const bool bottom = anchor == Anchor::BottomLeft || anchor == Anchor::BottomCenter ||
                            anchor == Anchor::BottomRight;

        // top positions stack downwards, bottom ones upwards (the first box
        // nearest the edge)
        float y = bottom ? height - margin : margin;

        for (auto& [item, before] : list)
        {
            const float w = item->size.width(), h = item->size.height();
            if (bottom) { y -= before + h; }
            else        { y += before; }

            float x = left ? margin : right ? width - margin - w : (width - w) / 2;
            float boxY = y;
            float alpha = item->alpha;

            // entrance: slide in from the nearest side edge, or fade in for centered ones
            const float t = 1.f - item->enter;
            if (left)       x -= t * (w + margin);
            else if (right) x += t * (w + margin);
            else
            {
                alpha *= item->enter;
                boxY += (bottom ? 1.f : -1.f) * t * 12 * ui;
            }

            if (alpha > 0.f)
            {
                p.save();
                p.setOpacity(alpha);
                item->draw(p, QRectF(x, boxY, w, h));
                p.restore();
            }

            if (!bottom) y += h;
        }
    }
}

bool Overlay::renderToImage(QImage& image, uint64_t& lastVersion, int deviceWidth, int deviceHeight, float scale)
{
    if (deviceWidth <= 0 || deviceHeight <= 0) return false;
    if (scale <= 0.f) scale = 1.f;

    bool sizeOk = !image.isNull() && image.width() == deviceWidth && image.height() == deviceHeight;
    {
        std::lock_guard<std::mutex> guard(lock);
        // popups and the progress indicator animate, so redraw every frame
        bool animating = (showPopups && (!popups.empty() || !lbPopups.empty())) ||
                         (showProgressInd && progressVisible);
        if (!animating && sizeOk && version == lastVersion)
            return false;
        lastVersion = version;
    }

    if (!sizeOk || image.format() != QImage::Format_ARGB32_Premultiplied)
        image = QImage(deviceWidth, deviceHeight, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(scale);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    paint(painter, (int)(deviceWidth / scale), (int)(deviceHeight / scale));
    painter.end();
    return true;
}

}
