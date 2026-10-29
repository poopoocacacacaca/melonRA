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

#include <cstring>

#include <QEventLoop>
#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QUdpSocket>
#include <QXmlStreamReader>
#include <QtEndian>

#include "PortMapper.h"

namespace Netplay
{

QStringList PortMapper::stunServers = {"stun.l.google.com:19302", "stun.cloudflare.com:3478"};
QHostAddress PortMapper::discoveryAddress = QHostAddress("239.255.255.250");
quint16 PortMapper::discoveryPort = 1900;

static constexpr int kMaxReply = 256 * 1024;
static constexpr int kTimeoutMs = 3000;
static constexpr int kSearchRounds = 3;
static constexpr int kMaxPortTries = 8;

// addresses a home router (or, in tests, this computer) can have
static bool localNetwork(const QHostAddress& a)
{
    if (a.protocol() != QAbstractSocket::IPv4Protocol) return false;
    if (a.isLoopback()) return true;
    const quint32 ip = a.toIPv4Address();
    return (ip >> 24) == 10 || (ip >> 20) == 0xAC1 || (ip >> 16) == 0xC0A8 || (ip >> 16) == 0xA9FE;
}

// addresses the internet can't reach: private, carrier NAT, and the like
static bool notPublic(const QHostAddress& a)
{
    if (a.protocol() != QAbstractSocket::IPv4Protocol) return true;
    const quint32 ip = a.toIPv4Address();
    return localNetwork(a) || (ip >> 22) == (100 << 2 | 1) || ip == 0 || (ip >> 24) == 0 || (ip >> 28) >= 0xE;
}

PortMapper::PortMapper(QObject* parent) : QObject(parent), Net(new QNetworkAccessManager(this))
{
    // the router is on the home network: never through a proxy
    Net->setProxy(QNetworkProxy::NoProxy);

    SearchTimer.setInterval(1000);
    connect(&SearchTimer, &QTimer::timeout, this, [this]
    {
        if (++SearchRounds >= kSearchRounds)
        {
            SearchTimer.stop();
            if (Ssdp) Ssdp->deleteLater();
            giveUp(tr("No router answered. Automatic port forwarding (UPnP) may be off on your router."));
            return;
        }
        discover();
    });

    connect(&RenewTimer, &QTimer::timeout, this, [this]
    {
        // keep the forwarding alive while the room is open
        if (Mapped) addMapping();
    });
}

PortMapper::~PortMapper()
{
    stop(500);
}

void PortMapper::start(quint16 port, bool tryRouter)
{
    Port = port;
    PublicPort = port;
    Attempt = 0;
    Lease = 3600;
    Public.clear();
    Problem.clear();
    Shared = false;
    Mapped = false;

    // the local address that reaches the internet, until the router says better
    for (const QHostAddress& a : QNetworkInterface::allAddresses())
    {
        if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback() && localNetwork(a))
        {
            Local = a.toString();
            break;
        }
    }

    CurState = State::Working;
    emit changed();

    if (!tryRouter)
    {
        CurState = State::NotOpened;
        Problem = tr("turned off when hosting");
        askStun();
        return;
    }

    Ssdp = new QUdpSocket(this);
    if (!Ssdp->bind(QHostAddress::AnyIPv4, 0))
    {
        giveUp(tr("Couldn't look for the router."));
        return;
    }
    connect(Ssdp, &QUdpSocket::readyRead, this, &PortMapper::onDiscoveryReply);
    SearchRounds = 0;
    discover();
    SearchTimer.start();
}

void PortMapper::discover()
{
    if (!Ssdp) return;

    auto send = [this]
    {
        for (const char* st : {"urn:schemas-upnp-org:device:InternetGatewayDevice:1",
                               "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
                               "urn:schemas-upnp-org:service:WANIPConnection:1"})
        {
            QByteArray msg = "M-SEARCH * HTTP/1.1\r\n"
                             "HOST: 239.255.255.250:1900\r\n"
                             "MAN: \"ssdp:discover\"\r\n"
                             "MX: 2\r\n"
                             "ST: " + QByteArray(st) + "\r\n\r\n";
            Ssdp->writeDatagram(msg, discoveryAddress, discoveryPort);
        }
    };

    if (!discoveryAddress.isMulticast())
    {
        send();
        return;
    }

    // out of every network adapter: with VPN or virtual machine adapters
    // around, the system's choice may not be the one the router is on
    bool sent = false;
    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces())
    {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning) ||
            (flags & QNetworkInterface::IsLoopBack) || !(flags & QNetworkInterface::CanMulticast))
            continue;

        bool hasV4 = false;
        for (const QNetworkAddressEntry& e : iface.addressEntries())
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol) hasV4 = true;
        if (!hasV4) continue;

        Ssdp->setMulticastInterface(iface);
        send();
        sent = true;
    }
    if (!sent) send();
}

void PortMapper::onDiscoveryReply()
{
    while (Ssdp && Ssdp->hasPendingDatagrams())
    {
        QNetworkDatagram dg = Ssdp->receiveDatagram(4096);
        if (!Router.isNull()) continue;   // already talking to one

        QHostAddress from(dg.senderAddress().toIPv4Address());
        if (!localNetwork(from)) continue;

        QUrl location;
        for (const QByteArray& line : dg.data().split('\n'))
        {
            QByteArray l = line.trimmed();
            if (l.size() > 9 && l.left(9).toLower() == "location:")
                location = QUrl(QString::fromLatin1(l.mid(9).trimmed()));
        }

        // only the device that answered, over plain HTTP on the home network
        if (!location.isValid() || location.scheme() != "http") continue;
        if (QHostAddress(location.host()) != from) continue;

        Router = from;
        SearchTimer.stop();
        fetchDescription(location);
    }
}

void PortMapper::fetchDescription(const QUrl& url)
{
    QNetworkRequest req(url);
    req.setTransferTimeout(kTimeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply* reply = Net->get(req);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 got, qint64)
    {
        if (got > kMaxReply) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, url] { onDescription(reply, url); });
}

void PortMapper::onDescription(QNetworkReply* reply, const QUrl& url)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
    {
        giveUp(tr("The router didn't describe itself."));
        return;
    }

    const QByteArray body = reply->read(kMaxReply);
    QXmlStreamReader xml(body);
    QUrl base = url;
    QString serviceType, controlUrl;
    QString bestType, bestControl;
    auto rank = [](const QString& t)
    {
        if (t.contains("WANIPConnection:2")) return 3;
        if (t.contains("WANIPConnection:1")) return 2;
        if (t.contains("WANPPPConnection:1")) return 1;
        return 0;
    };

    while (!xml.atEnd())
    {
        xml.readNext();
        if (xml.isStartElement())
        {
            const QStringView name = xml.name();
            if (name == QLatin1String("service")) { serviceType.clear(); controlUrl.clear(); }
            else if (name == QLatin1String("serviceType")) serviceType = xml.readElementText().trimmed();
            else if (name == QLatin1String("controlURL")) controlUrl = xml.readElementText().trimmed();
            else if (name == QLatin1String("URLBase"))
            {
                QUrl b(xml.readElementText().trimmed());
                if (b.isValid() && b.scheme() == "http") base = b;
            }
        }
        else if (xml.isEndElement() && xml.name() == QLatin1String("service"))
        {
            if (rank(serviceType) > rank(bestType) && !controlUrl.isEmpty())
            {
                bestType = serviceType;
                bestControl = controlUrl;
            }
        }
    }

    if (bestType.isEmpty())
    {
        giveUp(tr("The router doesn't offer automatic port forwarding."));
        return;
    }

    QUrl control = base.resolved(QUrl(bestControl));
    if (control.scheme() != "http" || QHostAddress(control.host()) != Router)
    {
        giveUp(tr("The router doesn't offer automatic port forwarding."));
        return;
    }

    Control = control;
    Service = bestType;

    // this computer's address as the router sees it
    QUdpSocket probe;
    probe.connectToHost(Router, control.port(80));
    if (probe.localAddress().protocol() == QAbstractSocket::IPv4Protocol && !probe.localAddress().isNull())
        Local = probe.localAddress().toString();

    addMapping();
}

QNetworkReply* PortMapper::soap(const QString& action, const QString& args)
{
    QByteArray body = QString(
        "<?xml version=\"1.0\"?>\r\n"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:%1 xmlns:u=\"%2\">%3</u:%1></s:Body></s:Envelope>\r\n")
        .arg(action, Service, args).toUtf8();

    QNetworkRequest req(Control);
    req.setTransferTimeout(kTimeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "text/xml; charset=\"utf-8\"");
    req.setRawHeader("SOAPAction", QString("\"%1#%2\"").arg(Service, action).toUtf8());
    QNetworkReply* reply = Net->post(req, body);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 got, qint64)
    {
        if (got > kMaxReply) reply->abort();
    });
    return reply;
}

QString PortMapper::soapValue(const QByteArray& body, const QString& name)
{
    QXmlStreamReader xml(body);
    while (!xml.atEnd())
    {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == name)
            return xml.readElementText().trimmed();
    }
    return QString();
}

int PortMapper::soapError(const QByteArray& body)
{
    bool ok = false;
    int code = soapValue(body, "errorCode").toInt(&ok);
    return ok ? code : -1;
}

void PortMapper::addMapping()
{
    const quint16 external = (quint16)(Port + Attempt);
    QString args = QString(
        "<NewRemoteHost></NewRemoteHost>"
        "<NewExternalPort>%1</NewExternalPort>"
        "<NewProtocol>UDP</NewProtocol>"
        "<NewInternalPort>%2</NewInternalPort>"
        "<NewInternalClient>%3</NewInternalClient>"
        "<NewEnabled>1</NewEnabled>"
        "<NewPortMappingDescription>melonRA netplay</NewPortMappingDescription>"
        "<NewLeaseDuration>%4</NewLeaseDuration>")
        .arg(external).arg(Port).arg(Local).arg(Lease);

    QNetworkReply* reply = soap("AddPortMapping", args);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onMappingReply(reply); });
}

void PortMapper::onMappingReply(QNetworkReply* reply)
{
    reply->deleteLater();
    const QByteArray body = reply->read(kMaxReply);
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (status == 200)
    {
        const bool first = !Mapped;
        Mapped = true;
        PublicPort = (quint16)(Port + Attempt);
        if (Lease > 0 && !RenewTimer.isActive())
            RenewTimer.start(Lease * 1000 / 2);
        if (first)
        {
            CurState = State::Opened;
            emit changed();
            askExternalAddress();
        }
        return;
    }

    if (Mapped)
    {
        // a renewal failed: the forwarding may lapse
        Mapped = false;
        RenewTimer.stop();
        CurState = State::NotOpened;
        Problem = tr("The router stopped forwarding the port.");
        emit changed();
        return;
    }

    switch (soapError(body))
    {
    case 725: // only permanent mappings: removed when the room closes
        if (Lease != 0)
        {
            Lease = 0;
            addMapping();
            return;
        }
        break;

    case 718: // the port is taken by another computer: use a nearby one
        if (++Attempt < kMaxPortTries)
        {
            addMapping();
            return;
        }
        giveUp(tr("Another device on your network is using port %1 and the ones after it.").arg(Port));
        return;

    case 724: // must be the same port outside and inside
        giveUp(tr("Another device on your network is using port %1.").arg(Port));
        return;

    case 606:
    case 620:
        giveUp(tr("Automatic port forwarding (UPnP) is turned off on your router."));
        return;

    default:
        break;
    }

    giveUp(tr("The router refused to forward the port."));
}

void PortMapper::askExternalAddress()
{
    QNetworkReply* reply = soap("GetExternalIPAddress", QString());
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onExternalAddress(reply); });
}

void PortMapper::onExternalAddress(QNetworkReply* reply)
{
    reply->deleteLater();
    const QByteArray body = reply->read(kMaxReply);
    QHostAddress addr(soapValue(body, "NewExternalIPAddress"));

    if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 ||
        addr.protocol() != QAbstractSocket::IPv4Protocol)
    {
        // the router didn't say: ask the internet
        askStun();
        return;
    }

    if (notPublic(addr))
    {
        // the router itself is behind another network
        Shared = true;
        CurState = State::NotOpened;
        Problem = tr("Your internet connection shares its address with other customers (or there's a "
                     "second router in front of yours), so players on the internet can't reach you. "
                     "Players on your home network still can.");
        emit changed();
        return;
    }

    finishAddress(addr.toString());
}

void PortMapper::giveUp(const QString& why)
{
    if (Ssdp) Ssdp->deleteLater();
    SearchTimer.stop();
    CurState = State::NotOpened;
    Problem = why;
    emit changed();

    // still find out the public address, for players who forward the port by hand
    askStun();
}

// --- STUN (RFC 5389), only to learn the public address ---

void PortMapper::askStun()
{
    if (Stun) Stun->deleteLater();
    if (StunIndex >= stunServers.size()) return;

    const QString server = stunServers[StunIndex];
    const int colon = server.lastIndexOf(':');
    if (colon <= 0) { StunIndex++; askStun(); return; }
    const QString host = server.left(colon);
    const quint16 port = (quint16)server.mid(colon + 1).toUInt();

    Stun = new QUdpSocket(this);
    if (!Stun->bind(QHostAddress::AnyIPv4, 0)) return;
    connect(Stun, &QUdpSocket::readyRead, this, &PortMapper::onStunReply);

    StunTxId.resize(12);
    for (int i = 0; i < 12; i++) StunTxId[i] = (char)QRandomGenerator::global()->bounded(256);

    QPointer<QUdpSocket> sock = Stun;
    QHostInfo::lookupHost(host, this, [this, sock, port](const QHostInfo& info)
    {
        if (!sock) return;
        QHostAddress to;
        for (const QHostAddress& a : info.addresses())
            if (a.protocol() == QAbstractSocket::IPv4Protocol) { to = a; break; }
        if (to.isNull()) { StunIndex++; askStun(); return; }

        QByteArray req(20, 0);
        qToBigEndian<quint16>(0x0001, req.data());          // binding request
        qToBigEndian<quint16>(0, req.data() + 2);
        qToBigEndian<quint32>(0x2112A442, req.data() + 4);   // magic cookie
        memcpy(req.data() + 8, StunTxId.constData(), 12);
        sock->writeDatagram(req, to, port);

        QTimer::singleShot(2000, sock, [this, sock]
        {
            if (!sock || !Public.isEmpty()) return;
            StunIndex++;
            askStun();
        });
    });
}

void PortMapper::onStunReply()
{
    while (Stun && Stun->hasPendingDatagrams())
    {
        const QByteArray d = Stun->receiveDatagram(1024).data();
        if (d.size() < 20) continue;
        const uchar* p = (const uchar*)d.constData();
        if (qFromBigEndian<quint16>(p) != 0x0101) continue;
        const int len = qFromBigEndian<quint16>(p + 2);
        if (qFromBigEndian<quint32>(p + 4) != 0x2112A442) continue;
        if (memcmp(p + 8, StunTxId.constData(), 12) != 0) continue;
        if (20 + len > d.size()) continue;

        QString found;
        int off = 20;
        while (off + 4 <= 20 + len)
        {
            const int type = qFromBigEndian<quint16>(p + off);
            const int alen = qFromBigEndian<quint16>(p + off + 2);
            const int vo = off + 4;
            if (vo + alen > 20 + len) break;

            if ((type == 0x0020 || type == 0x0001) && alen >= 8 && p[vo + 1] == 0x01)
            {
                quint32 ip = qFromBigEndian<quint32>(p + vo + 4);
                if (type == 0x0020) ip ^= 0x2112A442;
                found = QHostAddress(ip).toString();
                if (type == 0x0020) break;
            }
            off = vo + ((alen + 3) & ~3);
        }

        if (!found.isEmpty())
        {
            Stun->deleteLater();
            QHostAddress addr(found);
            if (notPublic(addr)) return;
            finishAddress(found);
            return;
        }
    }
}

void PortMapper::finishAddress(const QString& addr)
{
    Public = addr;
    emit changed();
}

void PortMapper::stop(int waitMs)
{
    SearchTimer.stop();
    RenewTimer.stop();
    if (Ssdp) Ssdp->deleteLater();
    if (Stun) Stun->deleteLater();

    if (Mapped && Control.isValid())
    {
        Mapped = false;
        QString args = QString("<NewRemoteHost></NewRemoteHost>"
                               "<NewExternalPort>%1</NewExternalPort>"
                               "<NewProtocol>UDP</NewProtocol>").arg(PublicPort);
        QNetworkReply* reply = soap("DeletePortMapping", args);

        // give the router a moment to close it before melonRA moves on (or quits)
        QEventLoop loop;
        QTimer::singleShot(waitMs, &loop, &QEventLoop::quit);
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        reply->deleteLater();
    }

    CurState = State::Idle;
}

}
