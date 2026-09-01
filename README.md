# Beamer Qt

Beamer Qt is a Linux/Qt desktop application for sending local video files to Chromecast and Apple TV devices on the LAN.

## Platform support

Linux is the supported platform today. The Qt frontend, HTTP media server, FFmpeg preparation, Direct Media AirPlay path, and Chromecast protocol are conceptually portable, but the native AirFry mirror backend currently depends on Linux-oriented GStreamer, desktop-capture, tray/DBus, and credential integrations. macOS and Windows support may be possible in the future, particularly for Direct Media and Chromecast, but is not currently maintained or tested.

## How it works

The Qt application discovers Chromecast/DIAL receivers with SSDP and Apple TV receivers with the native AirFry mDNS backend. A selected file (or a prepared derivative) is served from a local HTTP server. Chromecast playback uses the native Rust CASTV2 helper, with `catt` as an optional fallback. Apple TV has separate native mirror and Direct Media paths.

### Media preparation

Beamer probes the input with `ffprobe` and combines its properties with the selected audio/subtitle tracks and target backend. It chooses the least invasive compatible operation:

- Direct URL playback when the target can use the source as-is.
- MP4 remux when only the container or selected tracks need changing.
- Subtitle conversion to MP4 `mov_text` for text subtitles such as SubRip/WebVTT/ASS/SSA. The Apple TV or Chromecast then renders the timed-text track.
- Video burn-in for bitmap subtitles such as PGS/HDMV, DVD/VobSub, DVB, and XSUB. This necessarily re-encodes the video.

Video and audio are copied whenever possible. A re-encode is selected only when the subtitle type, target compatibility, or audio format requires it. Input HEVC/H.265 remains HEVC when re-encoding; H.264 remains H.264. Ten-bit input requests a ten-bit HEVC output where supported, and container/color metadata is retained as far as FFmpeg permits. This allows Apple TV and Chromecast to do the subtitle rendering themselves for compatible text tracks.

### Apple TV playback

Mirror is the default and remains the fallback. It is the compatibility path for desktop/video mirroring and uses the AirPlay mirror protocol. Direct Media uses AirFry's native AirPlay media session: the Apple TV fetches the local HTTP URL and handles supported HEVC, HDR, audio, pause/resume, and timeline seeking natively. Direct Media is the path to test original-format playback and receiver-side subtitle rendering.

The Apple TV backend selector and encoder-policy selector are independent. The encoder selector offers CPU or NVIDIA NVENC when the host supports it; it does not ask the user to choose H.264 or HEVC. Beamer derives the codec from the source and selected subtitle preparation. The choice is saved with `QSettings`. `BEAMER_VIDEO_ENCODER=cpu` or `BEAMER_VIDEO_ENCODER=nvenc` can override it for testing.

Chromecast support varies by model and firmware. HEVC/HDR may work when the receiver, container, profile, and bit depth support it. Beamer therefore permits source-matched HEVC/HDR transcoding, observes playback failure, and falls back to H.264 when necessary. The user does not need to select a codec manually.

## Dependencies

- Qt 6 Widgets, Network, and Test
- CMake 4.3+, a C++20 compiler
- FFmpeg (`ffmpeg` and `ffprobe`)
- Rust/Cargo and GStreamer development packages for AirFry
- The vendored `cast-sender` Rust dependency for Chromecast playback (`catt` is optional fallback)

Python/`pyatv` is only an optional legacy fallback when the native AirFry binary is unavailable; normal playback does not require it.

## Build

```bash
cargo build --manifest-path third_party/airfry/rust/Cargo.toml --bin airfry
cargo build --manifest-path third_party/chromecast/Cargo.toml
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Run

```bash
./build/beamer_qt
```

The native Chromecast helper is expected at `third_party/chromecast/target/debug/beamer-chromecast`. Override it with `BEAMER_CHROMECAST` if it is elsewhere.

The normal CMake build places both native backends beside the executable, so no environment variables are required. Drag a video into the window, select a receiver, and click Beam. For the first Apple TV use, click Pair, wait for the PIN screen, and enter the PIN. Pairing credentials are saved by AirFry.

Set `BEAMER_APPLETV_BACKEND=direct` to start testing Direct Media. The Debug panel's probe, HTTP-range, and device tests are useful before testing receiver playback.

The computer and receiver must be on reachable LANs. mDNS/SSDP and receiver return connections must not be blocked by the firewall.

## Troubleshooting

The Apple TV path enables AirFry debug diagnostics; the status bar shows the latest sender message. Test stable video first. Set `BEAMER_FILE_AUDIO=1` for file audio, or `BEAMER_TEST_AUDIO=1` to use a generated tone.

Useful overrides are `BEAMER_PYTHON` (legacy fallback), `BEAMER_AIRFRY`, `BEAMER_FFMPEG`, `BEAMER_FFPROBE`, `BEAMER_APPLETV_BACKEND`, and `BEAMER_VIDEO_ENCODER`.

## Repository and third-party source

AirFry and the native Chromecast helper are vendored under `third_party/` and tracked directly by this repository. Rust build output and the FairPlay build blob are excluded by the root `.gitignore`, while all source changes are included in normal Beamer commits. No Git submodule initialization is required.

## Tests

```bash
ctest --test-dir build --output-on-failure
```
