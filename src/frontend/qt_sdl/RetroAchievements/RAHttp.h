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

#ifndef RA_HTTP_H
#define RA_HTTP_H

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace RA
{

struct HttpResponse
{
    // HTTP status code, or 0 if the request never reached the server
    int status = 0;
    std::string body;
    // transport-level error description (only set when status == 0)
    std::string error;
    // the request was dropped because the client is shutting down
    bool cancelled = false;
};

using HttpCallback = std::function<void(const HttpResponse&)>;

// Small asynchronous HTTP(S) client used for RetroAchievements API calls and
// badge downloads.
//
// Requests are executed on a pool of worker threads, and callbacks are invoked
// on those worker threads. rc_client is thread-safe, so its server callbacks
// may run there directly; anything touching the UI must be marshalled to the
// GUI thread by the callback itself.
//
// Windows uses WinHTTP (native TLS, no dependency on the Qt TLS backend being
// available in static builds). Other platforms use QNetworkAccessManager.
class Http
{
public:
    explicit Http(int numWorkers = 2);
    ~Http();

    Http(const Http&) = delete;
    Http& operator=(const Http&) = delete;

    void setUserAgent(const std::string& ua);

    // postData empty => GET, otherwise POST with the given content type
    void request(const std::string& url, const std::string& postData,
                 const std::string& contentType, HttpCallback callback);

    // Stops accepting work. Pending requests are dropped (their callbacks are
    // still invoked with status 0 so rc_client can clean up). Busy workers are
    // detached so shutdown never blocks on a slow network.
    void shutdown();

private:
    struct Job
    {
        std::string url;
        std::string postData;
        std::string contentType;
        HttpCallback callback;
    };

    struct Shared
    {
        std::mutex lock;
        std::condition_variable cond;
        std::deque<Job> queue;
        bool stopping = false;
        std::string userAgent = "melonDS-RA";
    };

    static void workerMain(std::shared_ptr<Shared> shared);
    static HttpResponse perform(const Job& job, const std::string& userAgent);

    std::shared_ptr<Shared> shared;
    std::vector<std::thread> workers;
};

}

#endif // RA_HTTP_H
