#include <QtTest>
#include "../device.h"

class DeviceTest : public QObject {
    Q_OBJECT
private slots:
    void identifiesServices() {
        QCOMPARE(deviceKindFor("urn:dial-multiscreen-org:service:dial:1"), DeviceKind::Chromecast);
        QCOMPARE(deviceKindFor("_airplay._tcp"), DeviceKind::AppleTV);
        QCOMPARE(deviceKindFor("something-else"), DeviceKind::Unknown);
    }
    void parsesResponse() {
        const QByteArray data = "HTTP/1.1 200 OK\r\nST: urn:dial-multiscreen-org:service:dial:1\r\n"
                                "LOCATION: http://192.168.1.20:8008/ssdp/device-desc.xml\r\n\r\n";
        const auto devices = parseSsdpResponse(data, QHostAddress("192.168.1.20"));
        QCOMPARE(devices.size(), 1);
        QCOMPARE(devices.first().kind, DeviceKind::Chromecast);
    }
};
QTEST_MAIN(DeviceTest)
#include "test_device.moc"
