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

#ifndef PORTMAPPER_H
#define PORTMAPPER_H

#include <QHostAddress>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QUdpSocket;

namespace Netplay
{

// Opens a UDP port on the home router while a room is hosted, so players on
// the internet can reach this computer without the host editing router
// settings (UPnP Internet Gateway Device, as most home routers offer). Also
// finds the address the internet sees: from the router, or failing that from
// a public STUN server.
//
// Everything from the network is treated as untrusted: only the router that
// answered discovery is ever talked to, and replies are size-limited.
class PortMapper : public QObject
{
    Q_OBJECT

public:
    enum class State
    {
        Idle,
        Working,      // looking for the router / asking it
        Opened,       // the router forwards the port to this computer
        NotOpened,    // no router answered, or it refused
    };

    explicit PortMapper(QObject* parent = nullptr);
    ~PortMapper() override;

    // open `port` (UDP) on the router for this computer; tryRouter false only
    // looks up the public address
    void start(quint16 port, bool tryRouter);

    // closes the port again; waits up to `waitMs` for the router to confirm
    void stop(int waitMs = 1000);

    State state() const { return CurState; }

    // what the internet sees, once known ("" until then, or if unknown)
    QString publicAddress() const { return Public; }
    quint16 publicPort() const { return PublicPort; }

    // this computer's address on the home network
    QString localAddress() const { return Local; }

    // the router's own internet address is a shared or private one (carrier
    // NAT, or a second router in front): players outside can't get in
    bool behindSharedAddress() const { return Shared; }

    // why the port couldn't be opened, for showing
    QString problem() const { return Problem; }

    // public STUN servers asked when the router doesn't say (host:port)
    static QStringList stunServers;
    // set for tests: where discovery is sent (normally the UPnP multicast group)
    static QHostAddress discoveryAddress;
    static quint16 discoveryPort;

signals:
    void changed();

private:
    void discover();
    void onDiscoveryReply();
    void fetchDescription(const QUrl& url);
    void onDescription(QNetworkReply* reply, const QUrl& url);
    void addMapping();
    void onMappingReply(QNetworkReply* reply);
    void askExternalAddress();
    void onExternalAddress(QNetworkReply* reply);
    void giveUp(const QString& why);
    void askStun();
    void onStunReply();
    void finishAddress(const QString& addr);

    QNetworkReply* soap(const QString& action, const QString& args);
    static QString soapValue(const QByteArray& body, const QString& name);
    static int soapError(const QByteArray& body);

    State CurState = State::Idle;
    quint16 Port = 0;
    quint16 PublicPort = 0;
    QString Public;
    QString Local;
    bool Shared = false;
    QString Problem;

    QNetworkAccessManager* Net;
    QPointer<QUdpSocket> Ssdp;
    QPointer<QUdpSocket> Stun;
    QTimer SearchTimer;
    QTimer RenewTimer;
    int SearchRounds = 0;

    QHostAddress Router;
    QUrl Control;
    QString Service;          // WANIPConnection:1, WANIPConnection:2 or WANPPPConnection:1
    bool Mapped = false;
    int Lease = 3600;
    int Attempt = 0;          // external ports tried
    QByteArray StunTxId;
    int StunIndex = 0;
};

}

#endif // PORTMAPPER_H
