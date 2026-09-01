# Direct AirPlay media playback

## Goal

Add a separate AirPlay media-playback backend so compatible local files can be
sent without passing through the H.264 mirroring encoder. Preserve HEVC,
10-bit video, HDR, original frame rate, and multichannel audio when the
receiver and protocol support them. Keep the existing mirror path as a
fallback.

## Required work

1. Probe the input with `ffprobe` (container, codec/profile/level, dimensions,
   pixel format, HDR metadata, audio codec, sample rate, channel layout).
2. Query `/info` and retain all receiver capability fields, including display,
   HDR, audio-format, and feature data.
3. Implement AirPlay media-session negotiation and authenticated transport.
   Do not confuse DRM with required AirPlay session encryption.
4. Add direct media streaming with HTTP range/seek support and receiver
   feedback.
5. Negotiate audio codec, sample format, sample rate, and channel layout;
   explicitly downmix only when required.
6. Add startup and sustained-playback watchdogs. On failure, fall back to the
   existing H.264 mirror/transcode path and record the reason.
7. Add generated test fixtures for H.264, HEVC, 10-bit, HDR10/HLG, stereo,
   5.1, 7.1, 24-bit, and 96 kHz media.
8. Extend the Debug panel with structured per-test JSON results and measured
   connection latency, throughput, buffering, and startup time.

## Prompt for future implementation

> Implement the direct AirPlay media-playback backend in this repository.
> Reuse discovery, pairing, `/info`, the media server, diagnostics UI, and
> existing mirror fallback. First inspect the current protocol code and keep
> normal Beam behavior unchanged. Probe every input and negotiate the highest
> verified receiver-compatible format. Preserve HEVC/10-bit/HDR/audio layout
> only when the receiver and media protocol confirm support; otherwise use a
> deliberate conversion. Add structured diagnostics, watchdog-based fallback,
> tests, and documentation. Validate against real Apple TV hardware and never
> infer decoder support solely from a missing `/info` field.
