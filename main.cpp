#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMimeData>
#include <QPushButton>
#include <QSlider>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QUdpSocket>
#include <QTimer>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QInputDialog>
#include <QStandardPaths>
#ifdef Q_OS_UNIX
#include <signal.h>
#endif
#include "device.h"
#include "media_server.h"
#include <functional>

class DropZone final : public QFrame {
public:
    explicit DropZone(QWidget *parent = nullptr) : QFrame(parent) {
        setAcceptDrops(true);
        setMinimumHeight(170);
        setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
        auto *layout = new QVBoxLayout(this);
        title = new QLabel(tr("Drop a video here"), this);
        title->setAlignment(Qt::AlignCenter);
        title->setStyleSheet("font-size: 20px; font-weight: 600;");
        hint = new QLabel(tr("or click Browse to choose a file"), this);
        hint->setAlignment(Qt::AlignCenter);
        layout->addStretch(); layout->addWidget(title); layout->addWidget(hint); layout->addStretch();
    }

    std::function<void(const QString &)> fileDropped;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override {
        if (event->mimeData()->hasUrls()) event->acceptProposedAction();
    }
    void dropEvent(QDropEvent *event) override {
        const auto urls = event->mimeData()->urls();
        if (!urls.isEmpty() && urls.first().isLocalFile()) {
            if (fileDropped) fileDropped(urls.first().toLocalFile());
            event->acceptProposedAction();
        }
    }
private:
    QLabel *title{};
    QLabel *hint{};
};

class MainWindow final : public QMainWindow {
public:
    MainWindow() {
        setWindowTitle(tr("Beamer Qt"));
        resize(760, 560);
        auto *central = new QWidget(this);
        auto *root = new QVBoxLayout(central);

        auto *drop = new DropZone(central);
        root->addWidget(drop);

        auto *fileRow = new QHBoxLayout;
        fileLabel = new QLabel(tr("No video selected"), central);
        browse = new QPushButton(tr("Browse…"), central);
        fileRow->addWidget(fileLabel, 1); fileRow->addWidget(browse);
        root->addLayout(fileRow);

        auto *tracks = new QGroupBox(tr("Playback tracks"), central);
        auto *trackLayout = new QHBoxLayout(tracks);
        audioTrack = new QComboBox(tracks); subtitleTrack = new QComboBox(tracks);
        subtitleTrack->addItem(tr("Subtitles: Off"), -1);
        trackLayout->addWidget(audioTrack, 1); trackLayout->addWidget(subtitleTrack, 1);
        root->addWidget(tracks);

        auto *devices = new QGroupBox(tr("Devices on this network"), central);
        auto *deviceLayout = new QVBoxLayout(devices);
        auto *deviceToolbar = new QHBoxLayout;
        pair = new QPushButton(tr("Pair…"), devices);
        pair->setVisible(false);
        deviceToolbar->addWidget(new QLabel(tr("Target:"), devices));
        deviceToolbar->addStretch(); deviceToolbar->addWidget(pair);
        deviceLayout->addLayout(deviceToolbar);
        deviceList = new QListWidget(devices);
        deviceList->addItem(tr("Searching for Chromecast and Apple TV devices…"));
        deviceLayout->addWidget(deviceList);
        auto *refresh = new QPushButton(tr("Refresh devices"), devices);
        deviceLayout->addWidget(refresh);
        root->addWidget(devices, 1);

        auto *controls = new QHBoxLayout;
        play = new QPushButton(tr("Beam"), central); play->setEnabled(false);
        pause = new QPushButton(tr("Pause"), central); pause->setEnabled(false);
        stop = new QPushButton(tr("Stop"), central); stop->setEnabled(false);
        auto *progress = new QSlider(Qt::Horizontal, central); progress->setRange(0, 1000); progress->setEnabled(false);
        volume = new QSlider(Qt::Horizontal, central); volume->setRange(0, 100); volume->setValue(100); volume->setToolTip(tr("Volume"));
        controls->addWidget(play); controls->addWidget(pause); controls->addWidget(stop); controls->addWidget(progress, 1);
        controls->addWidget(new QLabel(tr("Volume"), central)); controls->addWidget(volume);
        root->addLayout(controls);
        setCentralWidget(central);

        drop->fileDropped = [this](const QString &path) { selectFile(path); };
        connect(browse, &QPushButton::clicked, this, [this] {
            const auto path = QFileDialog::getOpenFileName(this, tr("Choose a video"), {},
                tr("Video files (*.mp4 *.m4v *.webm *.mkv *.mov *.avi);;All files (*)"));
            if (!path.isEmpty()) selectFile(path);
        });
        connect(refresh, &QPushButton::clicked, this, [this] { discover(); });
        connect(deviceList, &QListWidget::currentRowChanged, this, [this](int row) {
            const bool ready = row >= 0 && row < found.size() && !selectedPath.isEmpty();
            play->setEnabled(ready); pause->setEnabled(ready); stop->setEnabled(ready); pair->setEnabled(row >= 0 && row < found.size());
            if (row >= 0 && row < found.size()) checkPairStatus(row);
        });
        connect(play, &QPushButton::clicked, this, [this] { beam(); });
        connect(stop, &QPushButton::clicked, this, [this] { if (process) process->kill(); });
        connect(pause, &QPushButton::clicked, this, [this] {
#ifdef Q_OS_UNIX
            if (process && process->state() == QProcess::Running) {
                const int sig = paused ? SIGCONT : SIGSTOP;
                if (::kill(static_cast<pid_t>(process->processId()), sig) == 0) {
                    paused = !paused; pause->setText(paused ? tr("Resume") : tr("Pause"));
                    statusBar()->showMessage(paused ? tr("Playback paused") : tr("Playback resumed"));
                }
            }
#else
            statusBar()->showMessage(tr("Pause is only available on Unix in this build"));
#endif
        });
        connect(pair, &QPushButton::clicked, this, [this] {
            const int row = deviceList->currentRow();
            if (row < 0 || row >= found.size() || found[row].kind != DeviceKind::AppleTV) { statusBar()->showMessage(tr("This device does not require application pairing")); return; }
            const auto airfry = qEnvironmentVariable("BEAMER_AIRFRY", QDir(QCoreApplication::applicationDirPath()).filePath("backends/airfry"));
            if (QFileInfo::exists(airfry)) {
                auto *pairing = new QProcess(this);
                connect(pairing, &QProcess::readyReadStandardError, this, [this, pairing] { const auto s = QString::fromLocal8Bit(pairing->readAllStandardError()).trimmed(); if (!s.isEmpty()) statusBar()->showMessage(s.split('\n').last()); });
                pairing->start(airfry, {"pair", found[row].address.toString(), "--debug"});
                QTimer::singleShot(1500, this, [this, pairing] {
                    bool accepted = false;
                    const auto pin = QInputDialog::getText(this, tr("Pair Apple TV"), tr("Enter the PIN now shown on your Apple TV:"), QLineEdit::Normal, {}, &accepted);
                    if (accepted) { pairing->write(pin.toUtf8() + '\n'); pairing->closeWriteChannel(); }
                });
                connect(pairing, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, pairing](int code, QProcess::ExitStatus) { statusBar()->showMessage(code == 0 ? tr("Apple TV paired for video playback") : tr("Apple TV pairing failed (code %1)").arg(code)); pairing->deleteLater(); });
                return;
            }
            bool ok = false;
            const auto pin = QInputDialog::getText(this, tr("Pair Apple TV"), tr("Enter the PIN shown on your Apple TV:"), QLineEdit::Normal, {}, &ok);
            if (!ok || pin.isEmpty()) return;
            if (QFileInfo::exists(airfry)) {
                auto *pairing = new QProcess(this);
                connect(pairing, &QProcess::readyReadStandardError, this, [this, pairing] { statusBar()->showMessage(QString::fromLocal8Bit(pairing->readAllStandardError()).trimmed().split('\n').last()); });
                connect(pairing, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, pairing](int code, QProcess::ExitStatus) {
                    statusBar()->showMessage(code == 0 ? tr("Apple TV paired for video playback") : tr("Apple TV pairing failed (code %1)").arg(code)); pairing->deleteLater();
                });
                pairing->start(airfry, {"pair", found[row].address.toString(), "--pin", pin, "--debug"});
                return;
            }
            const auto script = QDir(QCoreApplication::applicationDirPath()).filePath("backends/pyatv_bridge.py");
            auto *pairing = new QProcess(this);
            connect(pairing, &QProcess::finished, this, [this, pairing](int, QProcess::ExitStatus) {
                const auto result = QJsonDocument::fromJson(pairing->readAllStandardOutput()).object();
                statusBar()->showMessage(result.value("ok").toBool() ? tr("Apple TV paired") : tr("Pairing failed: %1").arg(result.value("error").toString())); pairing->deleteLater();
            });
            pairing->start(qEnvironmentVariable("BEAMER_PYTHON", "python3"), {script});
            pairing->write(QJsonDocument(QJsonObject{{"command", "pair"}, {"id", found[row].id}, {"pin", pin}}).toJson(QJsonDocument::Compact) + '\n');
            pairing->closeWriteChannel();
        });
        discover();
    }

private:
    void selectFile(const QString &path) {
        fileLabel->setText(QFileInfo(path).fileName());
        fileLabel->setToolTip(path);
        selectedPath = path;
        inspectTracks(path);
        if (!server) server = new MediaServer(this);
        if (!server->isListening() && !server->serve(path)) {
            statusBar()->showMessage(tr("Could not start local media server"));
            return;
        }
        const bool ready = deviceList->currentRow() >= 0 && deviceList->currentRow() < found.size();
        play->setEnabled(ready); pause->setEnabled(ready); stop->setEnabled(ready);
        statusBar()->showMessage(tr("Ready to beam %1 (port %2)").arg(QFileInfo(path).fileName()).arg(server->serverPort()));
    }
    void inspectTracks(const QString &path) {
        audioTrack->clear(); subtitleTrack->clear(); subtitleTrack->addItem(tr("Subtitles: Off"), -1);
        auto *probe = new QProcess(this);
        connect(probe, &QProcess::finished, this, [this, probe](int code, QProcess::ExitStatus) {
            if (code == 0) {
                const auto streams = QJsonDocument::fromJson(probe->readAllStandardOutput()).object().value("streams").toArray();
                int audioNumber = 1, subtitleNumber = 1;
                for (const auto &value : streams) {
                    const auto stream = value.toObject(); const auto type = stream.value("codec_type").toString();
                    const int index = stream.value("index").toInt(); const auto tags = stream.value("tags").toObject();
                    const auto language = tags.value("language").toString(); const auto title = tags.value("title").toString();
                    const auto label = QString("%1 %2%3").arg(type == "audio" ? tr("Audio") : tr("Subtitles"))
                        .arg(language.isEmpty() ? tr("track %1").arg(type == "audio" ? audioNumber : subtitleNumber) : language)
                        .arg(title.isEmpty() ? QString() : " – " + title);
                    if (type == "audio") { audioTrack->addItem(label, index); ++audioNumber; }
                    if (type == "subtitle") { subtitleTrack->addItem(label, index); ++subtitleNumber; }
                }
            }
            probe->deleteLater();
        });
        probe->start(qEnvironmentVariable("BEAMER_FFPROBE", "ffprobe"), {"-v", "quiet", "-print_format", "json", "-show_streams", path});
    }
    void discover() {
        found.clear(); deviceList->clear(); deviceList->addItem(tr("Searching…"));
        socket = new QUdpSocket(this);
        if (!socket->bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            statusBar()->showMessage(tr("Could not open a LAN discovery socket: %1").arg(socket->errorString()));
            return;
        }
        connect(socket, &QUdpSocket::readyRead, this, [this] {
            while (socket->hasPendingDatagrams()) {
                QByteArray data; data.resize(int(socket->pendingDatagramSize()));
                QHostAddress sender; quint16 port;
                socket->readDatagram(data.data(), data.size(), &sender, &port);
                for (const auto &device : parseSsdpResponse(data, sender)) {
                    bool duplicate = false; for (const auto &d : found) duplicate |= d.id == device.id;
                    if (!duplicate) {
                        if (found.isEmpty()) deviceList->clear();
                        found.append(device); addDeviceRow(device);
                    }
                }
            }
        });
        const QByteArray request = "M-SEARCH * HTTP/1.1\\r\\nHOST: 239.255.255.250:1900\\r\\n"
            "MAN: \"ssdp:discover\"\\r\\nMX: 2\\r\\nST: ssdp:all\\r\\n\\r\\n";
        socket->writeDatagram(request, QHostAddress("239.255.255.250"), 1900);
        scanAppleTvs();
        QTimer::singleShot(6000, this, [this] {
            if (found.isEmpty()) { deviceList->clear(); deviceList->addItem(tr("No compatible devices found (check LAN/firewall settings)")); statusBar()->showMessage(tr("No receivers detected")); }
            else statusBar()->showMessage(tr("Found %1 receiver(s)").arg(found.size()));
        });
    }
    void addDeviceRow(const Device &device) {
        auto *item = new QListWidgetItem(deviceList); item->setSizeHint(QSize(0, 36));
        auto *row = new QWidget(deviceList); auto *layout = new QHBoxLayout(row); layout->setContentsMargins(8, 0, 8, 0);
        layout->addWidget(new QLabel(device.name, row), 1);
        auto *action = new QPushButton(tr("Pair"), row); action->setVisible(device.kind == DeviceKind::AppleTV); layout->addWidget(action);
        const int index = found.size() - 1;
        connect(action, &QPushButton::clicked, this, [this, index] { deviceList->setCurrentRow(index); pair->click(); });
        deviceList->setItemWidget(item, row);
        if (device.kind == DeviceKind::AppleTV) QTimer::singleShot(0, this, [this, index] { checkPairStatus(index); });
    }

    void checkPairStatus(int row) {
        if (row < 0 || row >= found.size()) return;
        const auto airfry = qEnvironmentVariable("BEAMER_AIRFRY", QDir(QCoreApplication::applicationDirPath()).filePath("backends/airfry"));
        if (!QFileInfo::exists(airfry)) return;
        auto *check = new QProcess(this);
        connect(check, &QProcess::finished, this, [this, check, row](int, QProcess::ExitStatus) {
            if (row < deviceList->count() && row < found.size()) {
                const bool paired = QString::fromLocal8Bit(check->readAllStandardOutput()).trimmed() == "paired";
                if (auto *container = deviceList->itemWidget(deviceList->item(row))) {
                    const auto labels = container->findChildren<QLabel*>(); const auto buttons = container->findChildren<QPushButton*>();
                    if (!labels.isEmpty()) labels.first()->setText(found[row].name + (paired ? tr("  (Paired)") : tr("  (Needs pairing)")));
                    if (!buttons.isEmpty()) { buttons.first()->setEnabled(!paired); buttons.first()->setVisible(!paired); }
                }
            }
            check->deleteLater();
        });
        check->start(airfry, {"credentials-check", found[row].address.toString()});
    }

    void scanAppleTvs() {
        const auto airfry = qEnvironmentVariable("BEAMER_AIRFRY", QDir(QCoreApplication::applicationDirPath()).filePath("backends/airfry"));
        if (QFileInfo::exists(airfry)) {
            auto *scan = new QProcess(this);
            connect(scan, &QProcess::finished, this, [this, scan](int code, QProcess::ExitStatus) {
                if (code != 0) { statusBar()->showMessage(tr("Apple TV discovery failed: %1").arg(QString::fromLocal8Bit(scan->readAllStandardError()).trimmed())); scan->deleteLater(); return; }
                for (const auto &value : QJsonDocument::fromJson(scan->readAllStandardOutput()).array()) {
                    const auto object = value.toObject(); Device device; device.id = object.value("id").toString(); device.name = object.value("name").toString(); device.address = QHostAddress(object.value("address").toString()); device.kind = DeviceKind::AppleTV;
                    bool duplicate = false; for (const auto &d : found) duplicate |= d.id == device.id;
                    if (!duplicate) { if (found.isEmpty()) deviceList->clear(); found.append(device); addDeviceRow(device); }
                }
                scan->deleteLater();
            });
            scan->start(airfry, {"discover-json"}); return;
        }
        const auto script = QDir(QCoreApplication::applicationDirPath()).filePath("backends/pyatv_bridge.py");
        if (!QFileInfo::exists(script)) return;
        auto *scan = new QProcess(this);
        connect(scan, &QProcess::finished, this, [this, scan](int code, QProcess::ExitStatus) {
            if (code != 0) {
                statusBar()->showMessage(tr("Apple TV discovery failed: %1").arg(QString::fromLocal8Bit(scan->readAllStandardError()).trimmed()));
                scan->deleteLater(); return;
            }
            const auto result = QJsonDocument::fromJson(scan->readAllStandardOutput()).object();
            if (result.contains("error")) statusBar()->showMessage(tr("Apple TV discovery failed: %1").arg(result.value("error").toString()));
            for (const auto &value : result.value("devices").toArray()) {
                const auto object = value.toObject();
                Device device; device.id = object.value("id").toString(); device.name = object.value("name").toString();
                device.address = QHostAddress(object.value("address").toString()); device.kind = DeviceKind::AppleTV;
                bool duplicate = false; for (const auto &d : found) duplicate |= d.id == device.id;
                if (!duplicate) {
                    if (found.isEmpty()) deviceList->clear();
                    found.append(device); addDeviceRow(device);
                }
            }
            scan->deleteLater();
        });
        const auto python = qEnvironmentVariable("BEAMER_PYTHON", "python3");
        scan->start(python, {script}); scan->write("{\"command\":\"scan\"}\n"); scan->closeWriteChannel();
    }
    void beam() {
        const int row = deviceList->currentRow();
        if (!server || row < 0 || row >= found.size()) return;
        if (audioTrack->count() > 0 && audioTrack->currentData().isValid()) {
            const auto output = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("beamer-selected.mp4");
            QStringList args{"-y", "-i", selectedPath, "-map", "0:v:0", "-map", QString("0:%1").arg(audioTrack->currentData().toInt()), "-c:v", "copy", "-c:a", "aac"};
            if (subtitleTrack->currentData().toInt() >= 0) args << "-map" << QString("0:%1").arg(subtitleTrack->currentData().toInt()) << "-c:s" << "mov_text";
            if (ffmpeg) ffmpeg->kill();
            ffmpeg = new QProcess(this);
            connect(ffmpeg, &QProcess::readyReadStandardError, this, [this] { statusBar()->showMessage(tr("Preparing selected tracks… %1").arg(QString::fromLocal8Bit(ffmpeg->readAllStandardError()).section(' ', -1).trimmed())); });
            connect(ffmpeg, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, output](int code, QProcess::ExitStatus) {
                if (code != 0 || !server->serve(output)) { statusBar()->showMessage(tr("Could not prepare selected tracks")); return; }
                ffmpeg->deleteLater(); ffmpeg = nullptr; beamNow();
            });
            args << output; ffmpeg->start(qEnvironmentVariable("BEAMER_FFMPEG", "ffmpeg"), args);
            play->setEnabled(false); statusBar()->showMessage(tr("Preparing selected tracks…")); return;
        }
        beamNow();
    }
    void beamNow() {
        const int row = deviceList->currentRow();
        if (row < 0 || row >= found.size()) return;
        const auto &device = found.at(row);
        if (device.kind != DeviceKind::Chromecast) {
            const auto airfry = qEnvironmentVariable("BEAMER_AIRFRY", QDir(QCoreApplication::applicationDirPath()).filePath("backends/airfry"));
            if (QFileInfo::exists(airfry)) {
                if (!process) process = new QProcess(this);
                connect(process, &QProcess::readyReadStandardError, this, [this] {
                    const auto text = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
                    if (!text.isEmpty()) statusBar()->showMessage(text.split('\n').last());
                }, Qt::UniqueConnection);
                connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus) {
                    if (code != 0) statusBar()->showMessage(tr("Apple TV sender exited with code %1").arg(code));
                    process->deleteLater(); process = nullptr;
                }, Qt::UniqueConnection);
                // Keep the stable video path isolated while file-audio
                // negotiation is validated independently.
                QStringList airplayArgs{"mirror", device.address.toString(), "--file", selectedPath, "--sw", "--debug"};
                if (!qEnvironmentVariableIsSet("BEAMER_FILE_AUDIO") && !qEnvironmentVariableIsSet("BEAMER_TEST_AUDIO")) airplayArgs << "--no-audio";
                if (qEnvironmentVariableIsSet("BEAMER_TEST_AUDIO")) airplayArgs << "--test-audio";
                process->start(airfry, airplayArgs);
                if (process->waitForStarted(1000)) { paused = false; pause->setText(tr("Pause")); statusBar()->showMessage(tr("Sending local video to %1…").arg(device.name)); return; }
            }
            const auto script = QDir(QCoreApplication::applicationDirPath()).filePath("backends/pyatv_bridge.py");
            if (!QFileInfo::exists(script)) { statusBar()->showMessage(tr("Apple TV backend is not installed")); return; }
            auto *atv = new QProcess(this);
            connect(atv, &QProcess::finished, this, [this, atv](int, QProcess::ExitStatus) {
                const auto response = QJsonDocument::fromJson(atv->readAllStandardOutput()).object();
                statusBar()->showMessage(response.value("ok").toBool() ? tr("Playing on Apple TV") : tr("Apple TV error: %1").arg(response.value("error").toString()));
                atv->deleteLater();
            });
            const auto python = qEnvironmentVariable("BEAMER_PYTHON", "python3");
            atv->start(python, {script});
            const auto request = QJsonObject{{"command", "play"}, {"id", device.id}, {"url", server->mediaUrl(device.address).toString()}};
            atv->write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n'); atv->closeWriteChannel();
            return;
        }
        if (!process) process = new QProcess(this);
        const auto url = server->mediaUrl(device.address).toString();
        const auto nativeCast = qEnvironmentVariable("BEAMER_CHROMECAST", QDir(QCoreApplication::applicationDirPath()).filePath("backends/beamer-chromecast"));
        const auto executable = QFileInfo::exists(nativeCast) ? nativeCast : QStringLiteral("catt");
        const auto args = executable == nativeCast ? QStringList{"cast", device.address.toString(), url} : QStringList{"-d", device.address.toString(), "cast", url};
        process->start(executable, args);
        if (!process->waitForStarted(1000)) statusBar()->showMessage(tr("Chromecast backend unavailable: build beamer-chromecast"));
        else statusBar()->showMessage(tr("Sending video to %1…").arg(device.name));
    }
    QLabel *fileLabel{};
    QListWidget *deviceList{};
    QPushButton *browse{}, *play{}, *pause{}, *stop{}, *pair{};
    QSlider *volume{};
    QComboBox *audioTrack{}, *subtitleTrack{};
    QList<Device> found;
    QString selectedPath;
    QUdpSocket *socket{};
    MediaServer *server{};
    QProcess *process{};
    QProcess *ffmpeg{};
    bool paused{};
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Beamer Qt");
    MainWindow window;
    window.show();
    return app.exec();
}
