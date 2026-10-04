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
#include <QTextEdit>
#include <QTcpSocket>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QSettings>
#include <QRegularExpression>
#ifdef Q_OS_UNIX
#include <signal.h>
#endif
#include "device.h"
#include "media_server.h"
#include "media_preparer.h"
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

        auto *encoderRow = new QHBoxLayout;
        encoderRow->addWidget(new QLabel(tr("Transcode encoder:"), central));
        videoEncoder = new QComboBox(central);
        populateEncoders();
        encoderRow->addWidget(videoEncoder); encoderRow->addStretch();
        root->addLayout(encoderRow);

        auto *devices = new QGroupBox(tr("Devices on this network"), central);
        auto *deviceLayout = new QVBoxLayout(devices);
        auto *deviceToolbar = new QHBoxLayout;
        pair = new QPushButton(tr("Pair…"), devices);
        pair->setVisible(false);
        resetPairing = new QPushButton(tr("Reset pairing"), devices);
        resetPairing->setVisible(false);
        deviceToolbar->addWidget(new QLabel(tr("Target:"), devices));
        deviceToolbar->addWidget(new QLabel(tr("Protocol:"), devices));
        transportChoice = new QComboBox(devices);
        transportChoice->addItem(tr("Automatic"), "auto");
        transportChoice->addItem(tr("Chromecast"), "chromecast");
        transportChoice->addItem(tr("AirPlay"), "airplay");
        transportChoice->setEnabled(false);
        deviceToolbar->addWidget(transportChoice);
        deviceToolbar->addStretch(); deviceToolbar->addWidget(resetPairing); deviceToolbar->addWidget(pair);
        deviceLayout->addLayout(deviceToolbar);
        deviceList = new QListWidget(devices);
        deviceList->addItem(tr("Searching for Chromecast and Apple TV devices…"));
        deviceLayout->addWidget(deviceList);
        auto *refresh = new QPushButton(tr("Refresh devices"), devices);
        deviceLayout->addWidget(refresh);
        root->addWidget(devices, 1);

        auto *debug = new QGroupBox(tr("Debug diagnostics"), central);
        auto *debugLayout = new QVBoxLayout(debug);
        auto *debugButtons = new QHBoxLayout;
        auto *runProbe = new QPushButton(tr("Probe input"), debug);
        auto *runHttp = new QPushButton(tr("Test HTTP ranges"), debug);
        auto *runDevice = new QPushButton(tr("Test selected device"), debug);
        auto *clearDebug = new QPushButton(tr("Clear"), debug);
        debugButtons->addWidget(runProbe); debugButtons->addWidget(runHttp); debugButtons->addWidget(runDevice); debugButtons->addStretch(); debugButtons->addWidget(clearDebug);
        debugLayout->addLayout(debugButtons);
        debugLog = new QTextEdit(debug); debugLog->setReadOnly(true); debugLog->setMinimumHeight(110); debugLog->setPlaceholderText(tr("Diagnostics are non-destructive. Select a file/device, then run a test."));
        debugLayout->addWidget(debugLog);
        root->addWidget(debug);

        auto *controls = new QHBoxLayout;
        play = new QPushButton(tr("Beam"), central); play->setEnabled(false);
        pause = new QPushButton(tr("Pause"), central); pause->setEnabled(false);
        stop = new QPushButton(tr("Stop"), central); stop->setEnabled(false);
        progress = new QSlider(Qt::Horizontal, central); progress->setRange(0, 1000); progress->setEnabled(false);
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
        connect(volume, &QSlider::valueChanged, this, [this](int value) { setReceiverVolume(value); });
        connect(clearDebug, &QPushButton::clicked, this, [this] { debugLog->clear(); });
        connect(runProbe, &QPushButton::clicked, this, [this] { runProbeTest(); });
        connect(runHttp, &QPushButton::clicked, this, [this] { runHttpTest(); });
        connect(runDevice, &QPushButton::clicked, this, [this] { runDeviceTest(); });
        connect(deviceList, &QListWidget::currentRowChanged, this, [this](int row) {
            const bool ready = row >= 0 && row < found.size() && !selectedPath.isEmpty();
            play->setEnabled(ready); pause->setEnabled(ready); stop->setEnabled(ready); pair->setEnabled(row >= 0 && row < found.size());
            resetPairing->setVisible(row >= 0 && row < found.size() && found[row].kind == DeviceKind::AppleTV);
            resetPairing->setEnabled(resetPairing->isVisible());
            transportChoice->setEnabled(row >= 0 && row < found.size());
            if (row >= 0 && row < found.size()) {
                const auto saved = QSettings().value("transport/" + found[row].address.toString(), "auto").toString();
                const int choice = transportChoice->findData(saved);
                transportChoice->setCurrentIndex(choice >= 0 ? choice : 0);
            }
            if (row >= 0 && row < found.size()) checkPairStatus(row);
        });
        connect(resetPairing, &QPushButton::clicked, this, [this] {
            const int row = deviceList->currentRow();
            if (row < 0 || row >= found.size()) return;
            const auto airfry = qEnvironmentVariable("BEAMER_AIRFRY", QDir(QCoreApplication::applicationDirPath()).filePath("backends/airfry"));
            if (!QFileInfo::exists(airfry)) { statusBar()->showMessage(tr("AirPlay backend is not installed")); return; }
            auto *reset = new QProcess(this);
            connect(reset, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, reset, row](int code, QProcess::ExitStatus) {
                const auto error = QString::fromLocal8Bit(reset->readAllStandardError()).trimmed();
                statusBar()->showMessage(code == 0 ? tr("Pairing information reset; pair again when ready") : tr("Could not reset pairing: %1").arg(error));
                reset->deleteLater();
                if (code == 0) checkPairStatus(row);
            });
            reset->start(airfry, {"credentials-reset", found[row].address.toString()});
        });
        connect(transportChoice, &QComboBox::currentIndexChanged, this, [this](int index) {
            const int row = deviceList->currentRow();
            if (row < 0 || row >= found.size() || index < 0) return;
            const auto choice = transportChoice->itemData(index).toString();
            QSettings().setValue("transport/" + found[row].address.toString(), choice);
            const auto kind = choice == "chromecast" ? DeviceKind::Chromecast : DeviceKind::AppleTV;
            if (choice != "auto") found[row].kind = kind;
            pair->setVisible(found[row].kind == DeviceKind::AppleTV);
        });
        connect(play, &QPushButton::clicked, this, [this] { beam(); });
        connect(progress, &QSlider::sliderPressed, this, [this] { seeking = true; });
        connect(progress, &QSlider::sliderReleased, this, [this] {
            seeking = false;
            if (deviceList->currentRow() < 0 || found[deviceList->currentRow()].kind != DeviceKind::AppleTV) return;
            if (durationSeconds <= 0) { statusBar()->showMessage(tr("Cannot seek: input has no usable duration metadata")); return; }
            seekTo(progress->value() * durationSeconds / 1000.0);
        });
        playbackTimer = new QTimer(this);
        connect(playbackTimer, &QTimer::timeout, this, [this] {
            if (!paused && !seeking && process && process->state() == QProcess::Running && durationSeconds > 0) {
                if (playbackClock.isValid()) elapsedSeconds = clockBaseSeconds + playbackClock.elapsed() / 1000.0;
                progress->setValue(qBound(0, int(elapsedSeconds / durationSeconds * 1000.0), 1000));
            }
        });
        connect(stop, &QPushButton::clicked, this, [this] {
            pendingBeam = false;
            ++playbackGeneration;
            if (process && process->state() != QProcess::NotRunning) {
                stopping = true;
                process->kill();
                process->disconnect(this);
                process->deleteLater();
                process = nullptr;
                play->setEnabled(false);
                pause->setEnabled(false);
            }
            if (ffmpeg && ffmpeg->state() != QProcess::NotRunning) {
                ffmpeg->kill();
                ffmpeg->deleteLater();
                ffmpeg = nullptr;
            }
            playbackTimer->stop();
            playbackClock.invalidate();
            clockBaseSeconds = 0.0;
            elapsedSeconds = 0.0;
            progress->setValue(0);
            paused = false;
            pause->setText(tr("Pause"));
            seekSeconds = -1.0;
        });
        connect(pause, &QPushButton::clicked, this, [this] {
#ifdef Q_OS_UNIX
            if (process && process->state() == QProcess::Running) {
                if (deviceList->currentRow() >= 0 && deviceList->currentRow() < found.size() && found[deviceList->currentRow()].kind == DeviceKind::AppleTV) {
                    const bool nextPaused = !paused;
                    if (nextPaused) {
                        process->write("pause\n");
                        clockBaseSeconds = elapsedSeconds;
                        playbackClock.invalidate();
                        paused = true;
                        pause->setText(tr("Resume"));
                        statusBar()->showMessage(tr("Playback paused"));
                        return;
                    }

                    // The mirror transport has no source PTS and its audio and
                    // video decoders buffer independently. Resuming the same
                    // process replays different buffered durations and drifts.
                    // Rebuild both streams at the exact paused playhead, using
                    // the same path as a slider seek, so they share one origin.
                    const double resumeAt = clockBaseSeconds;
                    ++playbackGeneration;
                    paused = false;
                    pause->setText(tr("Pause"));
                    pause->setEnabled(false);
                    process->disconnect(this);
                    process->kill();
                    process->waitForFinished(1000);
                    process->deleteLater();
                    process = nullptr;
                    seekSeconds = qMax(0.0, resumeAt);
                    elapsedSeconds = seekSeconds;
                    clockBaseSeconds = seekSeconds;
                    playbackClock.start();
                    statusBar()->showMessage(tr("Resuming from %1…").arg(QString::number(seekSeconds, 'f', 1)));
                    beamNow();
                    seekSeconds = -1.0;
                    return;
                }
                const int sig = paused ? SIGCONT : SIGSTOP;
                if (::kill(static_cast<pid_t>(process->processId()), sig) == 0) {
                    const bool nextPaused = !paused;
                    if (nextPaused) { clockBaseSeconds = elapsedSeconds; playbackClock.invalidate(); }
                    else playbackClock.start();
                    paused = nextPaused; pause->setText(paused ? tr("Resume") : tr("Pause"));
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
                connect(pairing, &QProcess::errorOccurred, this, [this, pairing](QProcess::ProcessError error) {
                    if (error == QProcess::FailedToStart)
                        statusBar()->showMessage(tr("Apple TV pairing could not start: %1").arg(pairing->errorString()));
                });
                // Always force a fresh pairing from the UI. This is also the
                // recovery path for receivers whose saved identity became
                // stale (common after resetting an AirPlay receiver).
                pairing->start(airfry, {"pair", found[row].address.toString(), "--pair", "--debug"});
                QTimer::singleShot(1500, this, [this, pairing] {
                    bool accepted = false;
                    const auto pin = QInputDialog::getText(this, tr("Pair Apple TV"), tr("Enter the PIN now shown on your Apple TV:"), QLineEdit::Normal, {}, &accepted);
                    if (accepted) { pairing->write(pin.toUtf8() + '\n'); pairing->closeWriteChannel(); }
                });
                connect(pairing, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, pairing](int code, QProcess::ExitStatus) {
                    const auto error = QString::fromLocal8Bit(pairing->readAllStandardError()).trimmed();
                    const auto output = QString::fromLocal8Bit(pairing->readAllStandardOutput()).trimmed();
                    if (code == 0) {
                        statusBar()->showMessage(tr("AirPlay pairing handshake completed; playback has not been verified"));
                    } else {
                        const auto detail = !error.isEmpty() ? error : output;
                        statusBar()->showMessage(detail.isEmpty()
                            ? tr("Apple TV pairing failed (code %1)").arg(code)
                            : tr("Apple TV pairing failed: %1").arg(detail.split('\n').last()));
                    }
                    pairing->deleteLater();
                });
                return;
            }
            bool ok = false;
            const auto pin = QInputDialog::getText(this, tr("Pair Apple TV"), tr("Enter the PIN shown on your Apple TV:"), QLineEdit::Normal, {}, &ok);
            if (!ok || pin.isEmpty()) return;
            if (QFileInfo::exists(airfry)) {
                auto *pairing = new QProcess(this);
                connect(pairing, &QProcess::readyReadStandardError, this, [this, pairing] { statusBar()->showMessage(QString::fromLocal8Bit(pairing->readAllStandardError()).trimmed().split('\n').last()); });
                connect(pairing, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, pairing](int code, QProcess::ExitStatus) {
                    statusBar()->showMessage(code == 0 ? tr("AirPlay pairing handshake completed; playback has not been verified") : tr("AirPlay pairing failed (code %1)").arg(code)); pairing->deleteLater();
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
    static double jsonNumber(const QJsonValue &value) {
        if (value.isDouble()) return value.toDouble();
        bool ok = false;
        const double result = value.toString().toDouble(&ok);
        return ok ? result : 0.0;
    }

    void populateEncoders() {
        QProcess probe;
        probe.start(qEnvironmentVariable("BEAMER_FFMPEG", "ffmpeg"), {"-hide_banner", "-encoders"});
        const QString text = probe.waitForFinished(2500) ? QString::fromLocal8Bit(probe.readAllStandardOutput()) : QString();
        const auto has = [&text](const QString &name) { return text.contains(QRegularExpression(QStringLiteral("\\s") + QRegularExpression::escape(name) + QStringLiteral("\\s"))); };
        videoEncoder->addItem(tr("CPU (automatic codec)"), "cpu");
        // Mirror mode always sends H.264, so it only needs h264_nvenc.  Requiring
        // the HEVC encoder here incorrectly hides NVENC on otherwise capable
        // NVIDIA installations.
        if (has("h264_nvenc")) videoEncoder->addItem(tr("NVIDIA NVENC (automatic codec)"), "nvenc");
        QSettings settings;
        const auto wanted = qEnvironmentVariable("BEAMER_VIDEO_ENCODER", settings.value("encoding/videoEncoder", "libx264").toString());
        const int index = videoEncoder->findData(wanted == "nvenc" ? "nvenc" : "cpu");
        videoEncoder->setCurrentIndex(index >= 0 ? index : 0);
        connect(videoEncoder, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
            QSettings().setValue("encoding/videoEncoder", videoEncoder->currentData());
        });
    }
    QString selectedVideoEncoder(bool chromecast) const {
        const bool hevc = videoCodec.contains("hevc", Qt::CaseInsensitive) || videoCodec.contains("h265", Qt::CaseInsensitive);
        const bool nvenc = videoEncoder && videoEncoder->currentData().toString() == "nvenc";
        Q_UNUSED(chromecast);
        if (nvenc) return hevc ? QStringLiteral("hevc_nvenc") : QStringLiteral("h264_nvenc");
        return hevc ? QStringLiteral("libx265") : QStringLiteral("libx264");
    }
    void setReceiverVolume(int value) {
        const int row = deviceList->currentRow(); if (row < 0 || row >= found.size()) return;
        const auto &d = found[row]; if (d.kind == DeviceKind::AppleTV) { if (process && process->state() == QProcess::Running) process->write("volume " + QByteArray::number(value / 100.0, 'f', 3) + "\n"); return; }
        if (volumeProcess) { volumeProcess->kill(); volumeProcess->deleteLater(); }
        volumeProcess = new QProcess(this);
        const auto helper = qEnvironmentVariable("BEAMER_CHROMECAST", QDir(QCoreApplication::applicationDirPath()).filePath("backends/beamer-chromecast"));
        connect(volumeProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus) { if (code != 0) debug(tr("FAIL volume: %1").arg(QString::fromLocal8Bit(volumeProcess->readAllStandardError()).trimmed())); volumeProcess->deleteLater(); volumeProcess = nullptr; });
        volumeProcess->start(helper, {"volume", d.address.toString(), QString::number(value / 100.0, 'f', 2)});
    }
    void debug(const QString &message) { debugLog->append(QDateTime::currentDateTime().toString("HH:mm:ss ") + message); }
    void runProbeTest() {
        if (selectedPath.isEmpty()) { debug(tr("FAIL probe: select an input file first")); return; }
        auto *p = new QProcess(this); debug(tr("RUN probe: %1").arg(QFileInfo(selectedPath).fileName()));
        connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus) { const auto out = QString::fromLocal8Bit(p->readAllStandardOutput()).trimmed(); debug((code == 0 ? tr("PASS probe: ") : tr("FAIL probe: ")) + out.left(1200)); p->deleteLater(); });
        p->start(qEnvironmentVariable("BEAMER_FFPROBE", "ffprobe"), {"-v", "error", "-show_streams", "-show_format", "-of", "json", selectedPath});
    }
    void runHttpTest() {
        if (!server || selectedPath.isEmpty()) { debug(tr("FAIL HTTP: select an input file first")); return; }
        const auto url = server->mediaUrl(QHostAddress::LocalHost); auto *s = new QTcpSocket(this); debug(tr("RUN HTTP: range request against %1").arg(url.toString()));
        connect(s, &QTcpSocket::connected, this, [s, url] { s->write("GET /video HTTP/1.1\r\nHost: " + url.host().toUtf8() + "\r\nRange: bytes=0-15\r\nConnection: close\r\n\r\n"); });
        connect(s, &QTcpSocket::readyRead, this, [this, s] { const auto response = s->readAll(); if (response.contains("206 Partial Content") && response.contains("Content-Range: bytes 0-15/")) debug(tr("PASS HTTP: server returned 206 and Content-Range")); else if (response.contains("200 OK")) debug(tr("WARN HTTP: receiver/server ignored range")); else debug(tr("FAIL HTTP: unexpected response")); s->disconnectFromHost(); });
        connect(s, &QAbstractSocket::errorOccurred, this, [this, s] { debug(tr("FAIL HTTP: %1").arg(s->errorString())); s->deleteLater(); });
        connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater); s->connectToHost(QHostAddress::LocalHost, server->serverPort());
    }
    void runDeviceTest() {
        const int row = deviceList->currentRow(); if (row < 0 || row >= found.size()) { debug(tr("FAIL device: select a discovered device")); return; }
        const auto &d = found[row]; debug(tr("INFO device: %1 (%2) at %3").arg(d.name, d.kind == DeviceKind::AppleTV ? "AirPlay" : "Chromecast", d.address.toString()));
        const quint16 port = d.kind == DeviceKind::AppleTV ? 7000 : 8009;
        QElapsedTimer timer; timer.start(); QTcpSocket baseline; baseline.connectToHost(d.address, port);
        if (baseline.waitForConnected(3000)) { debug(tr("PASS network: TCP %1 connected in %2 ms").arg(port).arg(timer.elapsed())); baseline.disconnectFromHost(); }
        else debug(tr("FAIL network: TCP %1 connection failed after %2 ms: %3").arg(port).arg(timer.elapsed()).arg(baseline.errorString()));
        const auto executable = d.kind == DeviceKind::AppleTV ? qEnvironmentVariable("BEAMER_AIRFRY", QDir(QCoreApplication::applicationDirPath()).filePath("backends/airfry")) : qEnvironmentVariable("BEAMER_CHROMECAST", QDir(QCoreApplication::applicationDirPath()).filePath("backends/beamer-chromecast"));
        const auto command = d.kind == DeviceKind::AppleTV ? QStringList{"probe", d.address.toString(), "7000"} : QStringList{"probe", d.address.toString()};
        auto *p = new QProcess(this); connect(p, &QProcess::finished, this, [this, p](int code, QProcess::ExitStatus) { const auto out = QString::fromLocal8Bit(p->readAllStandardOutput()).trimmed(); const auto err = QString::fromLocal8Bit(p->readAllStandardError()).trimmed(); debug((code == 0 ? tr("PASS device: ") : tr("FAIL device: ")) + (out.isEmpty() ? err : out)); p->deleteLater(); });
        p->start(executable, command); if (!p->waitForStarted(1000)) { debug(tr("FAIL device: backend unavailable: %1").arg(executable)); p->deleteLater(); }
    }
    void selectFile(const QString &path) {
        videoCodec.clear(); videoPixelFormat.clear(); videoProfile.clear(); audioCodec.clear(); subtitleCodec.clear(); subtitleCodecs.clear();
        chromecastFallbackAttempted = false;
        fileLabel->setText(QFileInfo(path).fileName());
        fileLabel->setToolTip(path);
        selectedPath = path;
        durationSeconds = 0;
        elapsedSeconds = 0;
        progress->setValue(0); progress->setEnabled(true);
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
                const auto document = QJsonDocument::fromJson(probe->readAllStandardOutput());
                const auto root = document.object();
                const auto probedFormatDuration = jsonNumber(root.value("format").toObject().value("duration"));
                if (probedFormatDuration > 0) durationSeconds = probedFormatDuration;
                const auto streams = root.value("streams").toArray();
                int audioNumber = 1, subtitleNumber = 1;
                for (const auto &value : streams) {
                    const auto stream = value.toObject(); const auto type = stream.value("codec_type").toString();
                    const int index = stream.value("index").toInt(); const auto tags = stream.value("tags").toObject();
                    const auto language = tags.value("language").toString(); const auto title = tags.value("title").toString();
                    const auto label = QString("%1 %2%3").arg(type == "audio" ? tr("Audio") : tr("Subtitles"))
                        .arg(language.isEmpty() ? tr("track %1").arg(type == "audio" ? audioNumber : subtitleNumber) : language)
                        .arg(title.isEmpty() ? QString() : " – " + title);
                    if (type == "audio") { audioTrack->addItem(label, index); ++audioNumber; if (index == 1 || audioTrack->count() == 1) audioCodec = stream.value("codec_name").toString(); }
                    if (type == "video" && videoCodec.isEmpty()) { videoCodec = stream.value("codec_name").toString(); videoPixelFormat = stream.value("pix_fmt").toString(); videoProfile = stream.value("profile").toString(); }
                    if (type == "subtitle") { subtitleTrack->addItem(label, index); ++subtitleNumber; subtitleCodecs.insert(index, stream.value("codec_name").toString()); }
                }
            } else {
                debug(tr("WARN ffprobe stream inspection failed: %1").arg(QString::fromLocal8Bit(probe->readAllStandardError()).trimmed()));
            }
            probe->deleteLater();
        });
        probe->start(qEnvironmentVariable("BEAMER_FFPROBE", "ffprobe"), {"-v", "error", "-print_format", "json", "-show_streams", "-show_format", path});
        auto *durationProbe = new QProcess(this);
        connect(durationProbe, &QProcess::finished, this, [this, durationProbe](int code, QProcess::ExitStatus) {
            if (code == 0) {
                const auto object = QJsonDocument::fromJson(durationProbe->readAllStandardOutput()).object();
                const auto probedFormatDuration = jsonNumber(object.value("format").toObject().value("duration"));
                if (probedFormatDuration > 0) durationSeconds = probedFormatDuration;
                for (const auto &value : object.value("streams").toArray())
                    durationSeconds = qMax(durationSeconds, jsonNumber(value.toObject().value("duration")));
                // Keep the control interactive even when a container omits
                // duration; release-time validation provides the explanation.
                progress->setEnabled(true);
            } else {
                debug(tr("WARN ffprobe duration probe failed: %1").arg(QString::fromLocal8Bit(durationProbe->readAllStandardError()).trimmed()));
            }
            durationProbe->deleteLater();
        });
        durationProbe->start(qEnvironmentVariable("BEAMER_FFPROBE", "ffprobe"), {"-v", "quiet", "-print_format", "json", "-show_entries", "format=duration:stream=duration", path});
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
                    // A television may advertise Chromecast/DIAL and AirPlay
                    // independently. Treat services from the same address as
                    // one receiver; Chromecast is the preferred route when
                    // both advertisements are present, so an AirPlay PIN
                    // dialog cannot be triggered accidentally for Chromecast.
                    const auto existing = std::find_if(found.cbegin(), found.cend(), [&device](const auto &d) { return d.address == device.address; });
                    if (existing != found.cend()) {
                        const int index = int(std::distance(found.cbegin(), existing));
                        // Discovery is asynchronous: AirPlay mDNS can arrive
                        // before SSDP. Upgrade that row when Chromecast is
                        // subsequently confirmed for the same receiver.
                        if (device.kind == DeviceKind::Chromecast && found[index].kind == DeviceKind::AppleTV) {
                            found[index] = device;
                            if (auto *container = deviceList->itemWidget(deviceList->item(index))) {
                                const auto labels = container->findChildren<QLabel*>();
                                const auto buttons = container->findChildren<QPushButton*>();
                                if (!labels.isEmpty()) labels.first()->setText(device.name);
                                if (!buttons.isEmpty()) { buttons.first()->setVisible(false); buttons.first()->setEnabled(false); }
                            }
                        }
                        continue;
                    }
                    bool duplicate = false;
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
                    if (!buttons.isEmpty()) {
                        buttons.first()->setText(paired ? tr("Re-pair") : tr("Pair"));
                        buttons.first()->setEnabled(true);
                        buttons.first()->setVisible(true);
                    }
                    resetPairing->setVisible(paired && row == deviceList->currentRow() && found[row].kind == DeviceKind::AppleTV);
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
                    bool duplicate = false; for (const auto &d : found) duplicate |= d.address == device.address;
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
                bool duplicate = false; for (const auto &d : found) duplicate |= d.address == device.address;
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
        // Prepare a selected subtitle/audio stream once before handing the
        // resulting URL/path to a backend.
        const int selectedSubtitle = subtitleTrack->currentData().toInt();
        const int selectedAudio = audioTrack->currentData().isValid() ? audioTrack->currentData().toInt() : -1;
        subtitleCodec = subtitleCodecs.value(selectedSubtitle);
        const auto requestedEncoder = selectedVideoEncoder(false);
        // AirFry reads and encodes the original file incrementally. The
        // track-remux below is only needed for Chromecast's HTTP playback;
        // doing it first would add a full-file delay on Apple TV.
        if (found[row].kind == DeviceKind::AppleTV) {
            beamNow();
            return;
        }
        const bool selectedTracks = audioTrack->count() > 0 && audioTrack->currentData().isValid();
        const bool chromecastSafe = QFileInfo(selectedPath).suffix().compare("mp4", Qt::CaseInsensitive) == 0 &&
            videoCodec == "h264" && (videoPixelFormat.isEmpty() || videoPixelFormat == "yuv420p") &&
            (audioCodec.isEmpty() || audioCodec == "aac" || audioCodec == "mp3");
        if (selectedTracks || !chromecastSafe) {
            const auto output = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("beamer-selected.mp4");
            const auto args = MediaPreparer::remuxArguments(selectedPath,
                selectedTracks ? audioTrack->currentData().toInt() : -1,
                selectedSubtitle, output, subtitleCodec, chromecastSafe, false,
                chromecastFallbackAttempted ? QStringLiteral("libx264") : selectedVideoEncoder(true),
                videoPixelFormat.contains("10") || videoPixelFormat.contains("p010"));
            if (ffmpeg) ffmpeg->kill();
            ffmpeg = new QProcess(this);
            connect(ffmpeg, &QProcess::readyReadStandardError, this, [this] { statusBar()->showMessage(tr("Preparing selected tracks… %1").arg(QString::fromLocal8Bit(ffmpeg->readAllStandardError()).section(' ', -1).trimmed())); });
            connect(ffmpeg, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, output](int code, QProcess::ExitStatus) {
                if (code != 0 || !server->serve(output)) { statusBar()->showMessage(tr("Could not prepare selected tracks")); return; }
                ffmpeg->deleteLater(); ffmpeg = nullptr; beamNow();
            });
            ffmpeg->start(qEnvironmentVariable("BEAMER_FFMPEG", "ffmpeg"), args);
            play->setEnabled(false); statusBar()->showMessage(tr("Preparing selected tracks…")); return;
        }
        beamNow();
    }
    void beamNow() {
        const quint64 generation = ++playbackGeneration;
        const int row = deviceList->currentRow();
        if (row < 0 || row >= found.size()) return;
        const auto &device = found.at(row);
        if (seekSeconds < 0.0) {
            elapsedSeconds = 0.0;
            progress->setValue(0);
        }
        clockBaseSeconds = seekSeconds >= 0.0 ? seekSeconds : 0.0;
        playbackClock.start();
        if (device.kind != DeviceKind::Chromecast) {
            const auto airfry = qEnvironmentVariable("BEAMER_AIRFRY", QDir(QCoreApplication::applicationDirPath()).filePath("backends/airfry"));
            if (QFileInfo::exists(airfry)) {
                if (!process) process = new QProcess(this);
                if (process->state() != QProcess::NotRunning) {
                    pendingBeam = true;
                    stopping = false;
                    process->kill();
                    play->setEnabled(false);
                    pause->setEnabled(false);
                    statusBar()->showMessage(tr("Stopping the previous AirPlay session…"));
                    return;
                }
                airplayErrorBuffer.clear();
                connect(process, &QProcess::readyReadStandardError, this, [this] {
                    const auto text = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
                    if (!text.isEmpty()) { airplayErrorBuffer += text + '\n'; debug(text); }
                });
                connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
                    if (error == QProcess::FailedToStart)
                        statusBar()->showMessage(tr("AirPlay backend could not start: %1").arg(process->errorString()));
                });
                connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, generation](int code, QProcess::ExitStatus) {
                    if (generation != playbackGeneration) return;
                    const auto error = (airplayErrorBuffer + QString::fromLocal8Bit(process->readAllStandardError())).trimmed();
                    const bool restart = pendingBeam && !stopping;
                    const bool userStopped = stopping;
                    if (code != 0 && !userStopped) statusBar()->showMessage(error.isEmpty()
                        ? tr("AirPlay sender exited with code %1").arg(code)
                        : tr("AirPlay playback failed: %1").arg(error.split('\n').last()));
                    process->deleteLater(); process = nullptr;
                    stopping = false;
                    paused = false;
                    pause->setText(tr("Pause"));
                    pause->setEnabled(false);
                    if (restart) {
                        pendingBeam = false;
                        QTimer::singleShot(0, this, [this] { beamNow(); });
                    }
                });
                // Keep the stable video path isolated while file-audio
                // negotiation is validated independently.
                QStringList airplayArgs{"mirror", device.address.toString(), "--file", selectedPath, "--debug"};
                // AirFry's mirror encoder is independent of FFmpeg's media
                // preparation encoder.  Explicitly select the matching capture
                // path instead of silently forcing software encoding.
                if (videoEncoder && videoEncoder->currentData().toString() == "nvenc")
                    airplayArgs << "--hwaccel" << "nvenc";
                else
                    airplayArgs << "--sw";
                if (seekSeconds >= 0.0) airplayArgs << "--start" << QString::number(seekSeconds, 'f', 3);
                // File audio is streamed by AirFry alongside the video. Set
                // BEAMER_NO_AUDIO=1 only when video-only playback is desired.
                if (qEnvironmentVariableIsSet("BEAMER_NO_AUDIO")) airplayArgs << "--no-audio";
                if (qEnvironmentVariableIsSet("BEAMER_TEST_AUDIO")) airplayArgs << "--test-audio";
                process->start(airfry, airplayArgs);
                if (process->waitForStarted(1000)) {
                    // AirFry retains this value if the mirror control channel
                    // is still negotiating, then applies it after SETUP.
                    process->write("volume " + QByteArray::number(volume->value() / 100.0, 'f', 3) + "\n");
                    paused = false; stopping = false; pause->setText(tr("Pause")); pause->setEnabled(true); playbackTimer->start(250); statusBar()->showMessage(tr("AirPlay session starting on %1…").arg(device.name)); return;
                }
                statusBar()->showMessage(tr("AirPlay backend unavailable: %1").arg(process->errorString()));
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
        connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
                statusBar()->showMessage(tr("Chromecast backend could not start: %1").arg(process->errorString()));
        });
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, generation](int code, QProcess::ExitStatus) {
            if (generation != playbackGeneration) return;
            const auto error = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
            if (code != 0 && !chromecastFallbackAttempted) {
                chromecastFallbackAttempted = true;
                debug(tr("INFO Chromecast direct play failed; preparing compatibility fallback"));
                process->disconnect(this);
                process->kill();
                process->deleteLater();
                process = nullptr;
                beam();
            } else if (code != 0) {
                statusBar()->showMessage(error.isEmpty()
                    ? tr("Chromecast playback failed (code %1)").arg(code)
                    : tr("Chromecast playback failed: %1").arg(error.split('\n').last()));
            }
            process->deleteLater();
            process = nullptr;
        });
        process->start(executable, args);
        if (!process->waitForStarted(1000)) statusBar()->showMessage(tr("Chromecast backend unavailable: build beamer-chromecast"));
        else statusBar()->showMessage(tr("Sending video to %1…").arg(device.name));
    }
    void seekTo(double seconds) {
        if (found.isEmpty() || !process) return;
        if (found[deviceList->currentRow()].kind == DeviceKind::Chromecast) {
            const auto helper = qEnvironmentVariable("BEAMER_CHROMECAST", QDir(QCoreApplication::applicationDirPath()).filePath("backends/beamer-chromecast"));
            auto *seek = new QProcess(this); connect(seek, &QProcess::finished, seek, &QObject::deleteLater);
            seek->start(helper, {"seek", found[deviceList->currentRow()].address.toString(), QString::number(seconds, 'f', 3)});
            elapsedSeconds = seconds; clockBaseSeconds = seconds; playbackClock.start(); return;
        }
        statusBar()->showMessage(tr("Seeking…"));
        ++playbackGeneration;
        // A seek is implemented by replacing the mirror process. Clear the
        // old pause state first so the replacement cannot start frozen.
        paused = false;
        pause->setText(tr("Pause"));
        // Disconnect the old process lifecycle handlers before restarting.
        // Otherwise its queued finished signal can delete the replacement
        // process created by beamNow().
        process->disconnect(this);
        process->kill();
        process->waitForFinished(1000);
        process->deleteLater();
        process = nullptr;
        seekSeconds = qMax(0.0, seconds); elapsedSeconds = seekSeconds; clockBaseSeconds = seekSeconds; playbackClock.start();
        beamNow();
        seekSeconds = -1.0;
    }
    QLabel *fileLabel{};
    QListWidget *deviceList{};
    QPushButton *browse{}, *play{}, *pause{}, *stop{}, *pair{}, *resetPairing{};
    QSlider *progress{}, *volume{};
    QComboBox *audioTrack{}, *subtitleTrack{};
    QComboBox *videoEncoder{};
    QComboBox *transportChoice{};
    QList<Device> found;
    QString selectedPath;
    QUdpSocket *socket{};
    MediaServer *server{};
    QProcess *process{};
    QProcess *ffmpeg{};
    QProcess *volumeProcess{};
    QString videoCodec, videoPixelFormat, videoProfile, audioCodec, subtitleCodec;
    QHash<int, QString> subtitleCodecs;
    QTextEdit *debugLog{};
    bool chromecastFallbackAttempted{};
    QString airplayErrorBuffer;
    bool pendingBeam{};
    bool stopping{};
    bool paused{};
    QTimer *playbackTimer{};
    double durationSeconds{}, elapsedSeconds{}, seekSeconds{-1.0};
    QElapsedTimer playbackClock;
    double clockBaseSeconds{};
    bool seeking{};
    quint64 playbackGeneration{};
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Beamer Qt");
    MainWindow window;
    window.show();
    return app.exec();
}
