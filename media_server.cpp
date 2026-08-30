#include "media_server.h"
#include <QFile>
#include <QFileInfo>
#include <QTcpSocket>
#include <QUrl>
#include <QNetworkInterface>

MediaServer::MediaServer(QObject *parent) : QTcpServer(parent) {}

bool MediaServer::serve(const QString &filePath, quint16 preferredPort) {
    if (!QFileInfo::exists(filePath)) return false;
    if (isListening()) close();
    if (!listen(QHostAddress::Any, preferredPort)) return false;
    path = filePath;
    return true;
}

QUrl MediaServer::mediaUrl(const QHostAddress &receiver) const {
    for (const auto &iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp) || (iface.flags() & QNetworkInterface::IsLoopBack)) continue;
        for (const auto &entry : iface.addressEntries()) {
            const auto address = entry.ip();
            if (address.protocol() == receiver.protocol() && address.protocol() == QAbstractSocket::IPv4Protocol)
                return QUrl(QStringLiteral("http://%1:%2/video").arg(address.toString()).arg(serverPort()));
        }
    }
    return QUrl(QStringLiteral("http://127.0.0.1:%1/video").arg(serverPort()));
}

void MediaServer::incomingConnection(qintptr descriptor) {
    auto *socket = new QTcpSocket(this);
    socket->setSocketDescriptor(descriptor);
    connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
        const auto request = socket->readAll();
        if (!request.startsWith("GET /video ")) { socket->write("HTTP/1.1 404 Not Found\r\n\r\n"); socket->disconnectFromHost(); return; }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) { socket->write("HTTP/1.1 404 Not Found\r\n\r\n"); socket->disconnectFromHost(); return; }
        const auto body = file.readAll();
        const auto type = QFileInfo(path).suffix().toLower() == "webm" ? "video/webm" : "video/mp4";
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: " + QByteArray(type) + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n");
        socket->write(body);
        socket->disconnectFromHost();
    });
}
