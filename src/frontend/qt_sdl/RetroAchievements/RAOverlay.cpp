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
static constexpr float kPopupWidth = 330.f;
static constexpr float kPopupIcon = 52.f;
static constexpr float kPopupPad = 8.f;
static constexpr float kChallengeIcon = 30.f;

Overlay::Overlay(ImageCache& images) : images(images)
{
    fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
}

int64_t Overlay::nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void Overlay::addPopup(PopupKind kind, const std::string& iconUrl,
                       const QString& title, const QString& line1,
                       const QString& line2, int durationMs)
{
    images.prefetch(iconUrl);

    std::lock_guard<std::mutex> guard(lock);
    popups.push_back({kind, iconUrl, title, line1, line2, durationMs, -1});
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
    version++;
}

void Overlay::setOptions(bool popups_, bool challenges_, bool progress_, bool trackers_)
{
    std::lock_guard<std::mutex> guard(lock);
    showPopups = popups_;
    showChallenges = challenges_;
    showProgressInd = progress_;
    showTrackers = trackers_;
    if (!showPopups) popups.clear();
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
    while (!popups.empty() && popups.front().shownAt >= 0 &&
           now - popups.front().shownAt > popups.front().durationMs)
    {
        popups.pop_front();
        version++;
    }

    if (progressVisible && now - progressShownAt > kProgressHoldMs + kFadeOutMs)
    {
        progressVisible = false;
        version++;
    }

    return (showPopups && !popups.empty()) ||
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
    case PopupKind::Leaderboard: return QColor(0x6C, 0xC4, 0x8C);
    case PopupKind::Error:       return QColor(0xE0, 0x5A, 0x5A);
    case PopupKind::Info:
    default:                     return QColor(0x9A, 0xA4, 0xB0);
    }
}

void Overlay::drawIcon(QPainter& p, const std::string& url, const QRectF& rect, bool grayscale)
{
    QImage img = images.get(url);

    QPainterPath clip;
    float radius = rect.width() * 0.12f;
    clip.addRoundedRect(rect, radius, radius);

    p.save();
    p.setClipPath(clip);
    if (img.isNull())
    {
        // placeholder until the badge has downloaded
        p.fillRect(rect, QColor(0x3A, 0x3D, 0x46));
    }
    else
    {
        if (grayscale)
        {
            QImage gray = img.convertToFormat(QImage::Format_Grayscale8)
                             .convertToFormat(QImage::Format_ARGB32_Premultiplied);
            p.drawImage(rect, gray);
        }
        else
        {
            p.drawImage(rect, img);
        }
    }
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
    float ui = std::clamp(std::min(width / 560.f, height / 420.f), 0.7f, 1.6f);
    int64_t now = nowMs();

    if (showPopups) paintPopups(p, width, height, ui, now);
    paintIndicators(p, width, height, ui, now);
    if (showTrackers) paintTrackers(p, width, height, ui);
}

void Overlay::paintPopups(QPainter& p, int width, int height, float ui, int64_t now)
{
    // retire finished popups, promote queued ones
    for (auto it = popups.begin(); it != popups.end();)
    {
        if (it->shownAt >= 0 && now - it->shownAt > it->durationMs)
            it = popups.erase(it);
        else
            ++it;
    }

    int visible = 0;
    for (Popup& pop : popups)
    {
        if (visible >= kMaxVisiblePopups) break;
        if (pop.shownAt < 0) pop.shownAt = now;
        visible++;
    }

    const float margin = kMargin * ui;
    const float w = std::min(kPopupWidth * ui, width - 2 * margin);
    const float icon = kPopupIcon * ui;
    const float pad = kPopupPad * ui;

    QFont titleFont = p.font();
    titleFont.setPixelSize(std::max(11, (int)(14 * ui)));
    titleFont.setBold(true);
    QFont bodyFont = titleFont;
    bodyFont.setBold(false);
    bodyFont.setPixelSize(std::max(10, (int)(12 * ui)));
    QFont smallFont = bodyFont;
    smallFont.setPixelSize(std::max(9, (int)(11 * ui)));
    QFontMetricsF tfm(titleFont), bfm(bodyFont), sfm(smallFont);

    float y = margin;
    int drawn = 0;
    for (const Popup& pop : popups)
    {
        if (drawn >= visible) break;
        drawn++;

        // size the box to its content: icon height, or text height if taller
        const bool hasIcon = !pop.iconUrl.empty();
        float textH = tfm.height() + bfm.height() + (pop.line2.isEmpty() ? 0.f : sfm.height());
        float h = std::max(hasIcon ? icon : 0.f, textH) + 2 * pad;

        int64_t age = now - pop.shownAt;
        float slide = std::min(1.f, age / (float)kSlideInMs);
        slide = 1.f - (1.f - slide) * (1.f - slide); // ease out
        float alpha = 1.f;
        int64_t remaining = pop.durationMs - age;
        if (remaining < kFadeOutMs) alpha = std::max(0.f, remaining / (float)kFadeOutMs);

        float x = width - margin - w + (1.f - slide) * (w + margin);
        QRectF box(x, y, w, h);

        p.save();
        p.setOpacity(alpha);

        QColor accent = accentColor(pop.kind);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(18, 20, 26, 232));
        p.drawRoundedRect(box, 6 * ui, 6 * ui);
        // accent bar
        p.setBrush(accent);
        p.drawRoundedRect(QRectF(box.left(), box.top(), 4 * ui, box.height()), 2 * ui, 2 * ui);

        QRectF iconRect(box.left() + pad + 2 * ui, box.top() + (h - icon) / 2, icon, icon);
        if (hasIcon)
            drawIcon(p, pop.iconUrl, iconRect);

        float textX = hasIcon ? iconRect.right() + pad : box.left() + pad + 4 * ui;
        float textW = box.right() - pad - textX;
        float ly = box.top() + (h - textH) / 2;

        p.setPen(accent);
        p.setFont(titleFont);
        p.drawText(QRectF(textX, ly, textW, tfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                   tfm.elidedText(pop.title, Qt::ElideRight, textW));
        ly += tfm.height();

        p.setPen(QColor(235, 237, 240));
        p.setFont(bodyFont);
        p.drawText(QRectF(textX, ly, textW, bfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                   bfm.elidedText(pop.line1, Qt::ElideRight, textW));
        ly += bfm.height();

        if (!pop.line2.isEmpty())
        {
            p.setPen(QColor(160, 166, 176));
            p.setFont(smallFont);
            p.drawText(QRectF(textX, ly, textW, sfm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                       sfm.elidedText(pop.line2, Qt::ElideRight, textW));
        }

        p.restore();

        y += h + margin * 0.6f;
        if (y > height) break;
    }
}

void Overlay::paintIndicators(QPainter& p, int width, int height, float ui, int64_t now)
{
    const float margin = kMargin * ui;
    float bottom = height - margin;

    // challenge indicators: a row of small badges in the bottom-left corner
    if (showChallenges && !challenges.empty())
    {
        const float size = kChallengeIcon * ui;
        float x = margin;
        for (const auto& [id, url] : challenges)
        {
            if (x + size > width - margin) break;
            QRectF rect(x, bottom - size, size, size);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 140));
            p.drawRoundedRect(rect.adjusted(-2 * ui, -2 * ui, 2 * ui, 2 * ui), 5 * ui, 5 * ui);
            drawIcon(p, url, rect);
            x += size + 6 * ui;
        }
        bottom -= size + 8 * ui;
    }

    // progress indicator: locked badge + "12/50" just above the challenges
    if (showProgressInd && progressVisible)
    {
        int64_t age = now - progressShownAt;
        float alpha = 1.f;
        if (age > kProgressHoldMs)
            alpha = std::max(0.f, 1.f - (age - kProgressHoldMs) / (float)kFadeOutMs);
        if (alpha <= 0.f)
        {
            progressVisible = false;
            return;
        }

        QFont font = p.font();
        font.setPixelSize(std::max(8, (int)(13 * ui)));
        font.setBold(true);
        QFontMetricsF fm(font);

        const float icon = 28 * ui;
        const float pad = 5 * ui;
        float textW = fm.horizontalAdvance(progressText);
        QRectF box(margin, bottom - (icon + 2 * pad), icon + textW + 3 * pad, icon + 2 * pad);

        p.save();
        p.setOpacity(alpha);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(18, 20, 26, 225));
        p.drawRoundedRect(box, 5 * ui, 5 * ui);
        drawIcon(p, progressIcon, QRectF(box.left() + pad, box.top() + pad, icon, icon));
        p.setFont(font);
        p.setPen(QColor(235, 237, 240));
        p.drawText(QRectF(box.left() + 2 * pad + icon, box.top(), textW + pad, box.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, progressText);
        p.restore();
    }
}

void Overlay::paintTrackers(QPainter& p, int width, int height, float ui)
{
    if (trackers.empty()) return;

    // fixed-width font so timers don't jitter as digits change
    QFont font = fixedFont;
    font.setPixelSize(std::max(9, (int)(15 * ui)));
    font.setBold(true);
    QFontMetricsF fm(font);

    std::vector<const Tracker*> ordered;
    for (const auto& [id, tr] : trackers) ordered.push_back(&tr);
    std::sort(ordered.begin(), ordered.end(),
              [](const Tracker* a, const Tracker* b) { return a->order < b->order; });

    const float margin = kMargin * ui;
    const float padX = 8 * ui, padY = 3 * ui;
    float y = height - margin;

    p.setFont(font);
    for (auto it = ordered.rbegin(); it != ordered.rend(); ++it)
    {
        const Tracker* tr = *it;
        float tw = fm.horizontalAdvance(tr->text);
        float bh = fm.height() + 2 * padY;
        QRectF box(width - margin - tw - 2 * padX, y - bh, tw + 2 * padX, bh);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(18, 20, 26, 215));
        p.drawRoundedRect(box, 4 * ui, 4 * ui);
        p.setPen(QColor(240, 242, 245));
        p.drawText(box, Qt::AlignCenter, tr->text);

        y -= bh + 4 * ui;
        if (y < margin) break;
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
        bool animating = (showPopups && !popups.empty()) || (showProgressInd && progressVisible);
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
