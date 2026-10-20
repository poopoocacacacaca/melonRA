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

#include "RAImageCache.h"
#include "RAHttp.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace RA
{

ImageCache::ImageCache(Http& http, const std::string& cacheDir)
    : http(http), cacheDir(cacheDir)
{
    QDir().mkpath(QString::fromStdString(cacheDir));
}

void ImageCache::setLoadedCallback(std::function<void()> cb)
{
    std::lock_guard<std::mutex> guard(lock);
    onLoaded = std::move(cb);
}

std::string ImageCache::diskPath(const std::string& url) const
{
    // https://media.retroachievements.org/Badge/12345_lock.png -> Badge_12345_lock.png
    std::string path = url;
    size_t scheme = path.find("://");
    if (scheme != std::string::npos) path = path.substr(scheme + 3);
    size_t slash = path.find('/');
    path = (slash != std::string::npos) ? path.substr(slash + 1) : path;

    size_t query = path.find('?');
    if (query != std::string::npos) path = path.substr(0, query);

    for (char& c : path)
    {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '.' || c == '-' || c == '_';
        if (!ok) c = '_';
    }
    if (path.empty()) path = "image.png";

    return cacheDir + "/" + path;
}

void ImageCache::store(const std::string& url, const QImage& img)
{
    std::function<void()> cb;
    {
        std::lock_guard<std::mutex> guard(lock);
        pending.erase(url);
        if (img.isNull())
        {
            failed.insert(url);
            return;
        }
        images[url] = img;
        cb = onLoaded;
    }
    if (cb) cb();
}

QImage ImageCache::get(const std::string& url)
{
    if (url.empty()) return QImage();

    // Qt resources (":/..."), used for the sample notifications in the settings
    if (url.size() > 2 && url[0] == ':' && url[1] == '/')
    {
        std::lock_guard<std::mutex> guard(lock);
        auto it = images.find(url);
        if (it != images.end()) return it->second;
        QImage img(QString::fromStdString(url));
        if (!img.isNull()) img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        images[url] = img;
        return img;
    }

    std::string path;
    {
        std::lock_guard<std::mutex> guard(lock);
        auto it = images.find(url);
        if (it != images.end()) return it->second;
        if (pending.count(url) || failed.count(url)) return QImage();
        pending.insert(url);
        path = diskPath(url);
    }

    // try the disk cache first (synchronously: badge PNGs are tiny)
    QString qpath = QString::fromStdString(path);
    if (QFileInfo::exists(qpath))
    {
        QImage img(qpath);
        if (!img.isNull())
        {
            img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            std::lock_guard<std::mutex> guard(lock);
            pending.erase(url);
            images[url] = img;
            return img;
        }
    }

    http.request(url, "", "", [this, url, path](const HttpResponse& resp)
    {
        QImage img;
        if (resp.status == 200 && !resp.body.empty())
        {
            QByteArray data(resp.body.data(), (int)resp.body.size());
            if (img.loadFromData(data))
            {
                img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);

                QFile file(QString::fromStdString(path));
                if (file.open(QIODevice::WriteOnly))
                {
                    file.write(data);
                    file.close();
                }
            }
        }
        store(url, img);
    });

    return QImage();
}

}
