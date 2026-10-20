#include <QtTest>
#include "../device.h"
#include "../media_preparer.h"

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
    void preparesVideoToolboxRemux() {
        const auto args = MediaPreparer::remuxArguments("input.mkv", 1, 2, "output.mp4", "subrip", true, false, "h264_videotoolbox");
        QVERIFY(args.contains("-c:v"));
        QVERIFY(args.contains("copy"));
        QVERIFY(args.contains("-c:a"));
        QVERIFY(args.contains("aac"));
        QVERIFY(args.contains("-c:s"));
        QVERIFY(args.contains("mov_text"));
    }
    void preparesBitmapSubtitlesWithVideoToolbox() {
        const auto args = MediaPreparer::remuxArguments("input.mkv", 1, 2, "output.mp4", "hdmv_pgs_subtitle", false, false, "h264_videotoolbox");
        QVERIFY(args.contains("-filter_complex"));
        QVERIFY(args.contains("-c:v"));
        QVERIFY(args.contains("h264_videotoolbox"));
        QVERIFY(!args.contains("-preset")); // VideoToolbox does not use x264/nvenc -preset flags
    }
    void preparesNvencAndCpuPresets() {
        const auto nvencArgs = MediaPreparer::remuxArguments("input.mkv", 1, 2, "output.mp4", "hdmv_pgs_subtitle", false, false, "h264_nvenc");
        QVERIFY(nvencArgs.contains("-preset"));
        QVERIFY(nvencArgs.contains("p5"));

        const auto cpuArgs = MediaPreparer::remuxArguments("input.mkv", 1, 2, "output.mp4", "hdmv_pgs_subtitle", false, false, "libx264");
        QVERIFY(cpuArgs.contains("-preset"));
        QVERIFY(cpuArgs.contains("veryfast"));
    }
    void prepares10BitHevc() {
        const auto vtArgs = MediaPreparer::remuxArguments("input.mkv", 1, -1, "output.mp4", {}, false, true, "hevc_videotoolbox", true);
        QVERIFY(vtArgs.contains("-pix_fmt"));
        QVERIFY(vtArgs.contains("p010le"));
    }
    void parsesAirplayService() {
        QCOMPARE(deviceKindFor("airplay"), DeviceKind::AppleTV);
        QCOMPARE(deviceKindFor("apple-tv"), DeviceKind::AppleTV);
    }
};
QTEST_MAIN(DeviceTest)
#include "test_device.moc"
