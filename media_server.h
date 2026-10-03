#pragma once

#include <QObject>
#include <QTcpServer>
#include <QHostAddress>

class MediaServer final : public QTcpServer {
    Q_OBJECT
public:
    explicit MediaServer(QObject *parent = nullptr);
    bool serve(const QString &filePath, quint16 preferredPort = 0);
    QUrl mediaUrl(const QHostAddress &receiver) const;
    QString filePath() const { return path; }
    quint64 requestCount() const { return requests; }

signals:
    void mediaRequestReceived(const QHostAddress &peer);

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private:
    QString path;
    quint64 requests{};
};
