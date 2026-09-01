#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

struct PreparedMedia {
    QString path;
    bool temporary = false;
    QString description;
};

// Backend-neutral planning and FFmpeg preparation for a selected input stream.
class MediaPreparer final : public QObject {
    Q_OBJECT
public:
    explicit MediaPreparer(QObject *parent = nullptr) : QObject(parent) {}
    static QStringList remuxArguments(const QString &input, int audioIndex,
                                      int subtitleIndex, const QString &output,
                                      const QString &subtitleCodec = {},
                                      bool copyVideo = true, bool copyAudio = true,
                                      const QString &videoEncoder = "libx264",
                                      bool tenBit = false);
};
