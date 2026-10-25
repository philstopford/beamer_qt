# Beamer Qt

Beamer Qt is a desktop application for mirroring local video to Chromecast and Apple TV devices on the LAN, supporting Linux and macOS with initial Windows support.

## Platform support

- **Linux (x86_64 / Intel / AMD)**: Full support with native Qt 6, FFmpeg media preparation, local HTTP media streaming, and optional hardware acceleration via NVIDIA NVENC.
- **macOS (Apple Silicon / arm64 and Intel)**: Native Qt 6 GUI, bundle layout support, FFmpeg media preparation with hardware-accelerated Apple VideoToolbox (`h264_videotoolbox` and `hevc_videotoolbox`), local HTTP streaming, system Bonjour and SSDP discovery, and network subnet scan fallback for AirPlay/Chromecast endpoints.
- **Windows 10/11 (experimental)**: The Qt application and native Chromecast helper build with `.exe` packaging. SSDP and subnet discovery are available, and mDNS is enabled when a Bonjour SDK-compatible `dns_sd` library or pyatv is installed. The native AirFry sender is currently Unix-only, so Windows supports Chromecast playback while Apple TV discovery and diagnostics are an initial foundation for a future sender port.

## How it works

The Qt application discovers Chromecast/DIAL receivers with SSDP and Apple TV receivers with mDNS / native AirFry backend. If mDNS does not return an Apple TV, Beamer automatically falls back to a bounded TCP scan of the local physical WiFi/Ethernet network for AirPlay on port 7000. Video is prepared as needed and sent using the mirroring path. Chromecast mirroring uses the native Rust CASTV2 helper or local HTTP streaming, with `catt` as an optional fallback. Apple TV uses AirPlay mirroring.

### Receiver discovery

Apple TV discovery normally uses mDNS (`_airplay._tcp`). Beamer Qt uses the system DNS-SD daemon when available: Bonjour on macOS, Avahi's `libdns_sd` compatibility API on Linux, and a Bonjour SDK-compatible library on Windows. This provides the system's cross-interface view and retains advertised names such as `LivingRoom`. Services that resolve to one of the computer's own addresses are ignored. The portable AirFry mDNS browser runs alongside it where available. Some mesh WiFi systems do not forward multicast between mesh nodes, so an Apple TV on wired Ethernet may still be reachable but absent from mDNS results. The fallback scan covers `/22` through `/30` networks on active physical interfaces and probes the AirPlay TCP endpoint directly. Virtual adapters such as VMware, Docker, bridge, utun, and tunnel interfaces are excluded.

### Media preparation

Beamer probes the input with `ffprobe` and combines its properties with the selected audio/subtitle tracks and target backend. It chooses the least invasive compatible operation:

- MP4 remux when only the container or selected tracks need changing.
- Subtitle conversion to MP4 `mov_text` for text subtitles such as SubRip/WebVTT/ASS/SSA. The Apple TV or Chromecast then renders the timed-text track.
- Video burn-in for bitmap subtitles such as PGS/HDMV, DVD/VobSub, DVB, and XSUB. This necessarily re-encodes the video.

Video and audio are copied whenever possible. A re-encode is selected only when the subtitle type, target compatibility, or audio format requires it. Input HEVC/H.265 remains HEVC when re-encoding; H.264 remains H.264. Ten-bit input requests a ten-bit HEVC output where supported, and container/color metadata is retained as far as FFmpeg permits. This allows Apple TV and Chromecast to do the subtitle rendering themselves for compatible text tracks.

### Transcoding and Hardware Acceleration

The encoder selector dynamically detects and offers:
- **Apple VideoToolbox** (`videotoolbox`) on macOS for high-performance hardware H.264 and HEVC encoding.
- **NVIDIA NVENC** (`nvenc`) on Linux systems with compatible NVIDIA hardware.
- **CPU** software encoding (`libx264` / `libx265`).

The user choice is saved with `QSettings`. Overrides can be supplied via `BEAMER_VIDEO_ENCODER=videotoolbox`, `BEAMER_VIDEO_ENCODER=nvenc`, or `BEAMER_VIDEO_ENCODER=cpu`.

## Dependencies

- Qt 6 Widgets, Network, and Test
- CMake 4.3+, a C++20 compiler (Clang / AppleClang / GCC)
- FFmpeg (`ffmpeg` and `ffprobe`)
- Optional: Rust/Cargo for compiling native helper backends (Chromecast sender, AirFry)
- Optional: Python 3 with `pyatv` or `catt` for CLI helper fallbacks

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

When Cargo is installed, CMake will automatically build the native helpers in `third_party/`.

## Run

On Linux:
```bash
./build/beamer_qt
```

On macOS:
```bash
open ./build/beamer_qt.app
# or directly:
./build/beamer_qt.app/Contents/MacOS/beamer_qt
```

On Windows (experimental):
```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\beamer_qt.exe
```

For system-integrated discovery on Debian/Ubuntu install `libavahi-compat-libdnssd-dev`; Fedora provides the equivalent Avahi compatibility development package. Windows detects a Bonjour SDK installation exposed through the normal compiler and library search paths; without one, SSDP, the bounded address scan, and optional pyatv discovery remain available.

The native Chromecast helper is expected at `third_party/chromecast/target/debug/beamer-chromecast`. Override it with `BEAMER_CHROMECAST` if it is elsewhere.

The normal Linux/macOS CMake build places both native backends beside the executable, so no environment variables are required. The Windows build packages the Chromecast helper beside the executable. Drag a video into the window, select a receiver, and click Beam. For the first Apple TV use on Linux or macOS, click Pair, wait for the PIN screen, and enter the PIN. Pairing credentials are saved by AirFry.

The computer and receiver must be on reachable LANs. mDNS/SSDP and receiver return connections must not be blocked by the firewall. Mesh WiFi systems sometimes filter multicast between nodes even when wired and wireless clients share an IP subnet; the automatic TCP fallback handles that case when both devices are on the same physical local network. **Add Apple TV by IP…** remains available when the network is routed or uses a subnet outside the bounded automatic scan. The direct path requires TCP port 7000 and the return connections used by AirPlay to be allowed.

## Troubleshooting

The Apple TV path enables AirFry debug diagnostics; the status bar shows the latest sender message. Discovery details, including interfaces, addresses, prefixes, excluded adapters, scan subnets, and probe counts, appear in the Debug diagnostics panel. If automatic discovery still fails, check that the Apple TV is reachable on TCP port 7000 or use **Add Apple TV by IP…**. File audio plays by default; set `BEAMER_NO_AUDIO=1` for video-only playback, or `BEAMER_TEST_AUDIO=1` to use a generated tone.

For an isolated direct-media test, launch Beamer with `BEAMER_DIRECT_MEDIA=1` and select an MP4 file. AirFry asks the Apple TV to fetch and decode the original file, without mirroring or transcoding it. The Debug diagnostics panel logs the media URL, incoming HTTP requests, and receiver playback status or errors. Direct mode is experimental and does not automatically fall back to mirroring; unset the variable to return to the working mirror path. The receiver must support the file's video and audio codecs. Track and subtitle selection are not applied in this mode.

Useful overrides are `BEAMER_PYTHON` (legacy fallback), `BEAMER_AIRFRY`, `BEAMER_FFMPEG`, `BEAMER_FFPROBE`, and `BEAMER_VIDEO_ENCODER`.

## Repository and third-party source

AirFry and the native Chromecast helper are vendored under `third_party/` and tracked directly by this repository. Rust build output and the FairPlay build blob are excluded by the root `.gitignore`, while all source changes are included in normal Beamer commits. No Git submodule initialization is required.

## Tests

```bash
ctest --test-dir build --output-on-failure
```
