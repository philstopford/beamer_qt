#include "device.h"
#include <QUrl>
#include <QRegularExpression>

DeviceKind deviceKindFor(const QString &service) {
    const auto value = service.toLower();
    if (value.contains("googlecast") || value.contains("dial")) return DeviceKind::Chromecast;
    if (value.contains("airplay") || value.contains("apple-tv")) return DeviceKind::AppleTV;
    return DeviceKind::Unknown;
}

QList<Device> parseSsdpResponse(const QByteArray &response, const QHostAddress &sender) {
    const auto lines = QString::fromLatin1(response).split(QRegularExpression("\\r?\\n"));
    QString service, location;
    for (const auto &line : lines) {
        const auto colon = line.indexOf(':');
        if (colon < 0) continue;
        const auto key = line.left(colon).trimmed().toLower();
        const auto value = line.mid(colon + 1).trimmed();
        if (key == "st" || key == "usn") service = value;
        if (key == "location") location = value;
    }
    const auto kind = deviceKindFor(service + " " + location);
    if (kind == DeviceKind::Unknown) return {};
    Device device;
    device.address = sender; device.descriptionUrl = QUrl(location); device.kind = kind;
    device.id = sender.toString() + ":" + QString::number(device.descriptionUrl.port(80));
    device.name = kind == DeviceKind::Chromecast ? QObject::tr("Chromecast (%1)").arg(sender.toString())
                                                  : QObject::tr("Apple TV (%1)").arg(sender.toString());
    return {device};
}
