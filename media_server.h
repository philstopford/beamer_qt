#pragma once

#include <QObject>
#include <QTcpServer>

class MediaServer final : public QTcpServer {
    Q_OBJECT
public:
    explicit MediaServer(QObject *parent = nullptr);
    bool serve(const QString &filePath, quint16 preferredPort = 0);
    QUrl mediaUrl(const QHostAddress &receiver) const;
    QString filePath() const { return path; }

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private:
    QString path;
};
