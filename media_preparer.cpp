#include "media_preparer.h"

QStringList MediaPreparer::remuxArguments(const QString &input, int audioIndex,
                                          int subtitleIndex, const QString &output,
                                          const QString &subtitleCodec, bool copyVideo, bool copyAudio,
                                          const QString &videoEncoder, bool tenBit) {
    QStringList args{"-y", "-i", input, "-map", "0:v:0"};
    if (audioIndex >= 0) args << "-map" << QString("0:%1").arg(audioIndex);
    else args << "-map" << "0:a:0?";
    const auto codec = subtitleCodec.toLower();
    const bool bitmap = codec == "hdmv_pgs_subtitle" || codec == "pgssub" ||
        codec == "dvd_subtitle" || codec == "dvb_subtitle" || codec == "xsub";
    if (bitmap && subtitleIndex >= 0) {
        // FFmpeg exposes bitmap subtitle events as a video-like filter input;
        // overlay burns each event at its original display coordinates.
        args << "-filter_complex" << QString("[0:v:0][0:%1]overlay=shortest=1[v]").arg(subtitleIndex)
             << "-map" << "[v]" << "-c:v" << videoEncoder;
        if (tenBit && videoEncoder.contains("hevc", Qt::CaseInsensitive)) args << "-pix_fmt" << "p010le";
        else args << "-pix_fmt" << "yuv420p";
        if (videoEncoder.endsWith("_nvenc")) args << "-preset" << "p5";
        else if (videoEncoder == "libx264" || videoEncoder == "libx265") args << "-preset" << "veryfast";
    } else {
        args << "-c:v" << (copyVideo ? "copy" : videoEncoder);
        if (!copyVideo) args << "-pix_fmt" << (tenBit && videoEncoder.contains("hevc", Qt::CaseInsensitive) ? "p010le" : "yuv420p");
    }
    args << "-c:a" << (copyAudio ? "copy" : "aac");
    if (!copyAudio) args << "-b:a" << "192k" << "-ac" << "2";
    if (subtitleIndex >= 0 && !bitmap)
        args << "-map" << QString("0:%1").arg(subtitleIndex)
             << "-c:s" << "mov_text";
    args << "-map_metadata" << "0" << "-movflags" << "+faststart+write_colr" << output;
    return args;
}
