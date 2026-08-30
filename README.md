# Beamer Qt

Beamer Qt is a Linux/Qt desktop application for sending local video files to Chromecast and Apple TV devices on the LAN.

## How it works

The Qt application discovers Chromecast/DIAL receivers with SSDP and Apple TV receivers with the `pyatv` mDNS backend. A selected file is served from a temporary local HTTP server. Chromecast playback uses `catt`. Apple TV playback uses the AirFry AirPlay 2 sender, which handles pairing, FairPlay setup, RTSP negotiation, encrypted H.264 transport, and ALAC audio.

Files with selected audio or subtitle tracks are prepared with FFmpeg before being served. AirPlay file-audio playback is experimental; video-only playback is the stable fallback.

## Dependencies

- Qt 6 Widgets, Network, and Test
- CMake 4.3+, a C++20 compiler
- FFmpeg (`ffmpeg` and `ffprobe`)
- Python 3 with `pyatv==0.18.0`
- Rust/Cargo and GStreamer development packages for AirFry
- `catt` and `pychromecast` for Chromecast playback

Install Python dependencies:

```bash
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
```

## Build

```bash
git submodule update --init --recursive
cargo build --manifest-path third_party/airfry/rust/Cargo.toml --bin airfry
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Run

```bash
BEAMER_PYTHON="$PWD/.venv/bin/python" \
BEAMER_AIRFRY="$PWD/third_party/airfry/rust/target/debug/airfry" \
./build/beamer_qt
```

Drag a video into the window, select a receiver, and click Beam. For the first Apple TV use, click Pair, wait for the PIN screen, and enter the PIN. Pairing credentials are saved by AirFry.

The computer and receiver must be on reachable LANs. mDNS/SSDP and receiver return connections must not be blocked by the firewall.

## Troubleshooting

The Apple TV path enables AirFry debug diagnostics; the status bar shows the latest sender message. Test stable video first. Set `BEAMER_FILE_AUDIO=1` for file audio, or `BEAMER_TEST_AUDIO=1` to use a generated tone.

Useful overrides are `BEAMER_PYTHON`, `BEAMER_AIRFRY`, `BEAMER_FFMPEG`, and `BEAMER_FFPROBE`.

## Repository and third-party source

AirFry is vendored under `third_party/airfry` and tracked directly by this repository, including the required FairPlay source. Generated `rust/target` output and the FairPlay build blob are excluded by the root `.gitignore`, while all local AirFry changes are included in normal Beamer commits.

## Tests

```bash
ctest --test-dir build --output-on-failure
```
