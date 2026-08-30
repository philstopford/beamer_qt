#pragma once

#include <QHostAddress>
#include <QString>
#include <QList>
#include <QUrl>

enum class DeviceKind { Chromecast, AppleTV, Unknown };

struct Device {
    QString id;
    QString name;
    QHostAddress address;
    QUrl descriptionUrl;
    DeviceKind kind = DeviceKind::Unknown;
};

DeviceKind deviceKindFor(const QString &service);
QList<Device> parseSsdpResponse(const QByteArray &response, const QHostAddress &sender);
