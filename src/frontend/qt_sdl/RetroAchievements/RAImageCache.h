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

#ifndef RA_IMAGECACHE_H
#define RA_IMAGECACHE_H

#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

#include <QImage>

namespace RA
{

class Http;

// Downloads and caches achievement badges, game icons and user avatars.
// Images are kept in memory and mirrored to a folder on disk so they are
// available immediately (and offline) on the next run.
//
// get() is safe to call from any thread. The loaded callback is invoked on an
// HTTP worker thread whenever a new image becomes available.
class ImageCache
{
public:
    ImageCache(Http& http, const std::string& cacheDir);

    // Returns the image if available, otherwise a null QImage. A download is
    // started the first time an unknown URL is requested.
    QImage get(const std::string& url);

    // Starts loading an image without returning it (e.g. ahead of a popup).
    void prefetch(const std::string& url) { (void)get(url); }

    void setLoadedCallback(std::function<void()> cb);

private:
    std::string diskPath(const std::string& url) const;
    void store(const std::string& url, const QImage& img);

    Http& http;
    std::string cacheDir;

    std::mutex lock;
    std::unordered_map<std::string, QImage> images;
    std::set<std::string> pending;
    std::set<std::string> failed;
    std::function<void()> onLoaded;
};

}

#endif // RA_IMAGECACHE_H
