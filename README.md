# Beamer Qt

Beamer Qt is a Linux/Qt desktop application for sending local video files to Chromecast and Apple TV devices on the LAN.

## How it works

The Qt application discovers Chromecast/DIAL receivers with SSDP and Apple TV receivers with the native AirFry mDNS backend. A selected file is served from a temporary local HTTP server. Chromecast playback uses the vendored native Rust CASTV2 helper, with `catt` as a fallback. Apple TV playback uses the vendored AirFry AirPlay 2 sender, which handles discovery, pairing, FairPlay setup, RTSP negotiation, encrypted H.264 transport, and ALAC audio.

Files with selected audio or subtitle tracks are prepared with FFmpeg before being served. AirPlay file-audio playback is experimental; video-only playback is the stable fallback.

## Dependencies

- Qt 6 Widgets, Network, and Test
- CMake 4.3+, a C++20 compiler
- FFmpeg (`ffmpeg` and `ffprobe`)
- Rust/Cargo and GStreamer development packages for AirFry
- The vendored `cast-sender` Rust dependency for Chromecast playback (`catt` is optional fallback)

Python/`pyatv` is only an optional legacy fallback when the native AirFry binary is unavailable.

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
BEAMER_AIRFRY="$PWD/third_party/airfry/rust/target/debug/airfry" \
./build/beamer_qt
```

The native Chromecast helper is expected at `third_party/chromecast/target/debug/beamer-chromecast`. Override it with `BEAMER_CHROMECAST` if it is elsewhere.

Drag a video into the window, select a receiver, and click Beam. For the first Apple TV use, click Pair, wait for the PIN screen, and enter the PIN. Pairing credentials are saved by AirFry.

The computer and receiver must be on reachable LANs. mDNS/SSDP and receiver return connections must not be blocked by the firewall.

## Troubleshooting

The Apple TV path enables AirFry debug diagnostics; the status bar shows the latest sender message. Test stable video first. Set `BEAMER_FILE_AUDIO=1` for file audio, or `BEAMER_TEST_AUDIO=1` to use a generated tone.

Useful overrides are `BEAMER_PYTHON`, `BEAMER_AIRFRY`, `BEAMER_FFMPEG`, and `BEAMER_FFPROBE`.

## Repository and third-party source

AirFry and the native Chromecast helper are vendored under `third_party/` and tracked directly by this repository. Rust build output and the FairPlay build blob are excluded by the root `.gitignore`, while all source changes are included in normal Beamer commits. No Git submodule initialization is required.

## Tests

```bash
ctest --test-dir build --output-on-failure
```
