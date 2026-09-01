#include "media_server.h"
#include <QFile>
#include <QFileInfo>
#include <QTcpSocket>
#include <QUrl>
#include <QNetworkInterface>
#include <QByteArray>
#include <QRegularExpression>

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
        if (!request.startsWith("GET /video ") && !request.startsWith("HEAD /video ")) { socket->write("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n"); socket->disconnectFromHost(); return; }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) { socket->write("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n"); socket->disconnectFromHost(); return; }
        const qint64 size = file.size();
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
        file.seek(first);
        qint64 remaining = length;
        while (remaining > 0) { const QByteArray chunk = file.read(qMin<qint64>(remaining, 256 * 1024)); if (chunk.isEmpty()) break; socket->write(chunk); remaining -= chunk.size(); }
        socket->disconnectFromHost();
    });
}
