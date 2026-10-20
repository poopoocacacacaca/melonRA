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

#include "RAHttp.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#else
#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#endif

namespace RA
{

static constexpr int kTimeoutMs = 30000;

Http::Http(int numWorkers)
{
    shared = std::make_shared<Shared>();
    if (numWorkers < 1) numWorkers = 1;

    for (int i = 0; i < numWorkers; i++)
        workers.emplace_back(workerMain, shared);
}

Http::~Http()
{
    shutdown();
}

void Http::setUserAgent(const std::string& ua)
{
    std::lock_guard<std::mutex> guard(shared->lock);
    shared->userAgent = ua;
}

void Http::request(const std::string& url, const std::string& postData,
                   const std::string& contentType, HttpCallback callback)
{
    {
        std::lock_guard<std::mutex> guard(shared->lock);
        if (!shared->stopping)
        {
            shared->queue.push_back({url, postData, contentType, std::move(callback)});
            shared->cond.notify_one();
            return;
        }
    }

    // already shut down: fail immediately so the caller can clean up
    HttpResponse resp;
    resp.error = "HTTP client is shut down";
    resp.cancelled = true;
    if (callback) callback(resp);
}

void Http::shutdown()
{
    std::deque<Job> dropped;
    {
        std::lock_guard<std::mutex> guard(shared->lock);
        if (shared->stopping && workers.empty()) return;
        shared->stopping = true;
        dropped.swap(shared->queue);
        shared->cond.notify_all();
    }

    for (Job& job : dropped)
    {
        HttpResponse resp;
        resp.error = "Request cancelled";
        resp.cancelled = true;
        if (job.callback) job.callback(resp);
    }

    // Workers that are idle exit right away. Workers stuck on a slow request
    // would block shutdown for up to the timeout, so detach instead of join;
    // they only hold a reference to the shared state, never to this object.
    for (std::thread& t : workers)
        t.detach();
    workers.clear();
}

void Http::workerMain(std::shared_ptr<Shared> shared)
{
    for (;;)
    {
        Job job;
        std::string ua;
        {
            std::unique_lock<std::mutex> guard(shared->lock);
            shared->cond.wait(guard, [&] { return shared->stopping || !shared->queue.empty(); });
            if (shared->stopping) return;

            job = std::move(shared->queue.front());
            shared->queue.pop_front();
            ua = shared->userAgent;
        }

        HttpResponse resp = perform(job, ua);

        {
            // if we were shut down mid-request, the owner may be gone
            std::lock_guard<std::mutex> guard(shared->lock);
            if (shared->stopping) return;
        }

        if (job.callback) job.callback(resp);
    }
}


#ifdef _WIN32

static std::wstring toWide(const std::string& str)
{
    if (str.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), out.data(), len);
    return out;
}

static std::string winError(const char* what)
{
    DWORD err = GetLastError();
    return std::string(what) + " failed (error " + std::to_string(err) + ")";
}

HttpResponse Http::perform(const Job& job, const std::string& userAgent)
{
    HttpResponse resp;

    std::wstring wurl = toWide(job.url);

    URL_COMPONENTS comps = {};
    comps.dwStructSize = sizeof(comps);
    wchar_t host[256] = {};
    comps.lpszHostName = host;
    comps.dwHostNameLength = 256;
    comps.dwUrlPathLength = (DWORD)-1;
    comps.dwExtraInfoLength = (DWORD)-1;

    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &comps))
    {
        resp.error = winError("WinHttpCrackUrl");
        return resp;
    }

    std::wstring path;
    if (comps.lpszUrlPath) path.assign(comps.lpszUrlPath, comps.dwUrlPathLength);
    if (comps.lpszExtraInfo) path.append(comps.lpszExtraInfo, comps.dwExtraInfoLength);
    if (path.empty()) path = L"/";

    std::wstring wua = toWide(userAgent);

    // WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY requires Windows 8.1+; fall back for older systems
    HINTERNET session = WinHttpOpen(wua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        session = WinHttpOpen(wua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
    {
        resp.error = winError("WinHttpOpen");
        return resp;
    }

    WinHttpSetTimeouts(session, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);

    HINTERNET connect = WinHttpConnect(session, host, comps.nPort, 0);
    if (!connect)
    {
        resp.error = winError("WinHttpConnect");
        WinHttpCloseHandle(session);
        return resp;
    }

    const bool post = !job.postData.empty();
    DWORD flags = (comps.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(connect, post ? L"POST" : L"GET", path.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req)
    {
        resp.error = winError("WinHttpOpenRequest");
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return resp;
    }

    std::wstring headers;
    if (post)
    {
        std::string ct = job.contentType.empty() ? "application/x-www-form-urlencoded" : job.contentType;
        headers = L"Content-Type: " + toWide(ct) + L"\r\n";
    }

    BOOL ok = WinHttpSendRequest(req,
                                 headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                                 headers.empty() ? 0 : (DWORD)-1L,
                                 post ? (LPVOID)job.postData.data() : WINHTTP_NO_REQUEST_DATA,
                                 post ? (DWORD)job.postData.size() : 0,
                                 post ? (DWORD)job.postData.size() : 0,
                                 0);
    if (ok) ok = WinHttpReceiveResponse(req, nullptr);

    if (!ok)
    {
        resp.error = winError("HTTP request");
    }
    else
    {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        resp.status = (int)status;

        for (;;)
        {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail))
            {
                resp.status = 0;
                resp.error = winError("WinHttpQueryDataAvailable");
                break;
            }
            if (avail == 0) break;

            size_t offset = resp.body.size();
            resp.body.resize(offset + avail);
            DWORD read = 0;
            if (!WinHttpReadData(req, resp.body.data() + offset, avail, &read))
            {
                resp.status = 0;
                resp.error = winError("WinHttpReadData");
                break;
            }
            resp.body.resize(offset + read);
        }
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return resp;
}

#else // !_WIN32

HttpResponse Http::perform(const Job& job, const std::string& userAgent)
{
    // One network manager per worker thread. Qt supports QObjects and event
    // loops in threads it did not create ("adopted" threads).
    thread_local std::unique_ptr<QNetworkAccessManager> nam;
    if (!nam) nam = std::make_unique<QNetworkAccessManager>();

    HttpResponse resp;

    QNetworkRequest req(QUrl(QString::fromStdString(job.url)));
    req.setHeader(QNetworkRequest::UserAgentHeader, QString::fromStdString(userAgent));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply;
    if (!job.postData.empty())
    {
        QString ct = job.contentType.empty() ? QString("application/x-www-form-urlencoded")
                                             : QString::fromStdString(job.contentType);
        req.setHeader(QNetworkRequest::ContentTypeHeader, ct);
        reply = nam->post(req, QByteArray::fromStdString(job.postData));
    }
    else
    {
        reply = nam->get(req);
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(kTimeoutMs);
    loop.exec();

    if (!reply->isFinished())
    {
        reply->abort();
        resp.error = "Request timed out";
    }
    else
    {
        QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        resp.status = status.isValid() ? status.toInt() : 0;
        QByteArray data = reply->readAll();
        resp.body.assign(data.constData(), (size_t)data.size());

        if (resp.status == 0)
            resp.error = reply->errorString().toStdString();
    }

    reply->deleteLater();
    // process the deferred delete before the loop object goes away
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return resp;
}

#endif

}
