#include "media_server.h"
#include <QFile>
#include <QFileInfo>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QUrl>
#include <QNetworkInterface>
#include <QByteArray>
#include <QRegularExpression>
#include <functional>
#include <memory>

MediaServer::MediaServer(QObject *parent) : QTcpServer(parent) {}

bool MediaServer::serve(const QString &filePath, quint16 preferredPort) {
    if (!QFileInfo::exists(filePath)) return false;
    if (isListening()) close();
    if (!listen(QHostAddress::Any, preferredPort)) return false;
    path = filePath;
    return true;
}

QUrl MediaServer::mediaUrl(const QHostAddress &receiver) const {
    const auto forcedHost = qEnvironmentVariable("BEAMER_MEDIA_HOST").trimmed();
    if (!forcedHost.isEmpty())
        return QUrl(QStringLiteral("http://%1:%2/video.mp4").arg(forcedHost).arg(serverPort()));

    // The receiver can be on a routed network rather than directly on one of
    // our subnets.  Let the kernel choose the same source address it would use
    // to reach the receiver instead of falling back to whichever interface Qt
    // happens to enumerate first (often a VPN, Docker bridge, or Wi-Fi NIC).
    QUdpSocket routeProbe;
    routeProbe.connectToHost(receiver, 9);
    if (routeProbe.waitForConnected(100)) {
        const auto source = routeProbe.localAddress();
        if (!source.isNull() && !source.isLoopback() && source.protocol() == receiver.protocol())
            return QUrl(QStringLiteral("http://%1:%2/video.mp4").arg(source.toString()).arg(serverPort()));
    }

    QUrl fallback;
    for (const auto &iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp) || (iface.flags() & QNetworkInterface::IsLoopBack)) continue;
        for (const auto &entry : iface.addressEntries()) {
            const auto address = entry.ip();
            if (address.protocol() != receiver.protocol() || address.protocol() != QAbstractSocket::IPv4Protocol) continue;
            const auto candidate = QUrl(QStringLiteral("http://%1:%2/video.mp4").arg(address.toString()).arg(serverPort()));
            if (!fallback.isValid()) fallback = candidate;
            const auto mask = entry.netmask().toIPv4Address();
            if (mask && ((address.toIPv4Address() & mask) == (receiver.toIPv4Address() & mask))) return candidate;
        }
    }
    return fallback.isValid() ? fallback : QUrl(QStringLiteral("http://127.0.0.1:%1/video.mp4").arg(serverPort()));
}

void MediaServer::incomingConnection(qintptr descriptor) {
    auto *socket = new QTcpSocket(this);
    socket->setSocketDescriptor(descriptor);
    connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
        auto request = socket->property("beamerRequest").toByteArray();
        request += socket->readAll();
        if (!request.contains("\r\n\r\n")) {
            socket->setProperty("beamerRequest", request);
            return;
        }
        socket->setProperty("beamerRequest", {});
        ++requests;
        emit mediaRequestReceived(socket->peerAddress());
        const bool videoRequest = request.startsWith("GET /video ") || request.startsWith("HEAD /video ")
            || request.startsWith("GET /video.mp4 ") || request.startsWith("HEAD /video.mp4 ");
        if (!videoRequest) { socket->write("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n"); socket->disconnectFromHost(); return; }
        auto *file = new QFile(path, socket);
        if (!file->open(QIODevice::ReadOnly)) { socket->write("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n"); socket->disconnectFromHost(); return; }
        const qint64 size = file->size();
        const auto type = QFileInfo(path).suffix().toLower() == "webm" ? "video/webm" : "video/mp4";
        qint64 first = 0, last = size - 1;
        QByteArray rangeLine;
        for (const auto &line : request.split('\n')) if (line.startsWith("Range:")) { rangeLine = line; break; }
        if (!rangeLine.isEmpty()) {
            const auto match = QRegularExpression("bytes=(\\d*)-(\\d*)").match(QString::fromLatin1(rangeLine).trimmed());
            if (match.hasMatch()) {
                if (!match.captured(1).isEmpty()) first = match.captured(1).toLongLong();
                if (!match.captured(2).isEmpty()) last = match.captured(2).toLongLong();
                else last = size - 1;
                if (match.captured(1).isEmpty()) { const auto suffix = match.captured(2).toLongLong(); first = qMax<qint64>(0, size - suffix); last = size - 1; }
            }
        }
        if (first < 0 || first >= size || last < first || last >= size) { socket->write("HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */" + QByteArray::number(size) + "\r\nConnection: close\r\n\r\n"); socket->disconnectFromHost(); return; }
        const bool partial = !rangeLine.isEmpty();
        const qint64 length = last - first + 1;
        QByteArray headers = partial ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
        headers += "Content-Type: " + QByteArray(type) + "\r\nAccept-Ranges: bytes\r\nContent-Length: " + QByteArray::number(length) + "\r\n";
        if (partial) headers += "Content-Range: bytes " + QByteArray::number(first) + "-" + QByteArray::number(last) + "/" + QByteArray::number(size) + "\r\n";
        headers += "Connection: close\r\n\r\n";
        socket->write(headers);
        if (request.startsWith("HEAD ")) { socket->disconnectFromHost(); return; }
        file->seek(first);
        auto remaining = std::make_shared<qint64>(length);
        auto pump = std::make_shared<std::function<void()>>();
        *pump = [socket, file, remaining] {
            constexpr qint64 bufferedLimit = 512 * 1024;
            while (*remaining > 0 && socket->bytesToWrite() < bufferedLimit) {
                const auto chunk = file->read(qMin<qint64>(*remaining, 128 * 1024));
                if (chunk.isEmpty()) { socket->disconnectFromHost(); return; }
                socket->write(chunk);
                *remaining -= chunk.size();
            }
            if (*remaining == 0 && socket->bytesToWrite() == 0)
                socket->disconnectFromHost();
        };
        connect(socket, &QTcpSocket::bytesWritten, socket, [pump](qint64) { (*pump)(); });
        (*pump)();
    });
}
