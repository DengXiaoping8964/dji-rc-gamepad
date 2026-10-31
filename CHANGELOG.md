# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versioning is 0.x while the protocol
surface is still being explored.

## [0.1.0] - 2026-10-31

First public release. Verified against a real DJI RC-N3 on Windows 10 19045.

### Added
- DUML serial protocol implementation for the DJI RC-N1/RC-N3 `For Protocol` interface:
  framed requests, header CRC-8 and packet CRC-16, incremental stream decoder with
  CRC-verified resynchronisation, and byte-exact golden-vector regression tests.
- Live input decoding: two sticks, gimbal dial, four button bits and the three-position
  flight-mode switch, plus battery level from the remote's unsolicited status push.
- Virtual **Xbox 360 (wired)** controller output through ViGEmBus, so any XInput game sees an
  ordinary pad. Neutral report is forced on startup, on disconnect and on exit.
- Mapping engine: per-axis centre/end-stop/deadzone/expo/invert, exclusive hold layers so one
  physical key can never fire two actions, stick-direction d-pad, and the single dial split into
  **both** analogue triggers by direction with adjustable decay.
- Per-game profiles with foreground-window auto-switching, and a plain-text config written next to
  the executable (no registry, no `%APPDATA%`).
- Watchdog that snaps the pad to neutral when input data stops arriving.
- Diagnostics: `--scan` (probes each candidate port with real DUML instead of trusting port names),
  `--watch`, `--calibrate`, `--listen`, `--raw`, `--sweep`, `--probe`, `--try`, `--baudsweep`,
  `--bulk`, `--libusb`, plus `pad-selftest` and `bridge-tests` which need no hardware.
- Static CRT linking, so each build is a single self-contained `.exe`; x64 and x86 builds.
- Project site, MIT license and a written disclaimer covering affiliation, safety, warranty and
  liability.

### Measured
- Polling rate 86–89 Hz with `channel_hz=100`; serial round-trip 2.3–5.4 ms; 0 bad frames over a
  continuous session; 3609 pad updates with 0 failures in 14 s.

### Known limitations
- The remote has four digital keys, so games that need many simultaneous distinct buttons rely on
  hold layers or on an external remapper (Steam Input / reWASD) on top of the virtual pad.
- The physical power key is not reported over the protocol and cannot be mapped.
- The protocol is undocumented; a firmware update on either side may break compatibility.
- Only the RC-N1/RC-N3 `For Protocol` path was verified on hardware. The vendor bulk interfaces
  (`MI_00`/`MI_01`, which DJI Assistant also uses) are reachable through DJI's own libusb fork but
  are not implemented here.
- Minecraft Java Edition has no native gamepad support; use a controller mod or Bedrock Edition.
