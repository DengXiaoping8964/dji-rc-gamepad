# dji-rc-bridge — use a DJI RC-N3 as a PC game controller

Turn a **DJI RC-N3** drone remote into a **virtual Xbox 360 controller** on Windows, so games
that support a gamepad can be played with the remote's sticks, buttons, dial and flight-mode
switch. It is also a drone-simulator input device.

The program reads the remote over USB serial using DJI's undocumented **DUML** protocol and
re-publishes the input through the [ViGEmBus](https://github.com/nefarius/ViGEmBus) kernel
driver. Games see an ordinary `VID_045E&PID_028E` Xbox 360 pad — no DLL injection, no game
patches, no modding.

> ⚠️ **Read [`DISCLAIMER.md`](DISCLAIMER.md) first.** Unofficial project, no affiliation with DJI,
> no warranty, use at your own risk, and **never** while the remote is bound to a powered aircraft.

**[Project site](https://DengXiaoping8964.github.io/dji-rc-gamepad/)** ·
**[Releases](https://github.com/DengXiaoping8964/dji-rc-gamepad/releases)**

## Does my game work?

Anything that accepts an Xbox 360 / XInput controller works the moment the bridge is running —
that covers the large majority of modern PC titles. Two exceptions worth knowing:

| Situation | What to do |
| --- | --- |
| Game is XInput/DInput native (GTA, Saints Row, Just Cause, Battlefield V/2042, most 3A) | Just run the bridge. |
| Game has no controller support on PC (some older entries in the Battlefield series) | Route the virtual pad through Steam Input or reWASD and emit keyboard/mouse. |
| Game needs a gamepad but only on console-style APIs (Minecraft **Java** Edition) | Java Edition has no native gamepad support — use a controller mod (Controllable / Controlify), or play Bedrock Edition. |

## Requirements

- Windows 10 or 11 (x64 or x86). Tested on Windows 10 19045.
- [ViGEmBus](https://github.com/nefarius/ViGEmBus) kernel driver installed (skip this step if
  the bridge reports `virtual gamepad` creation succeeded).
- A **DJI RC-N3** (RC-N1 is supported by the same protocol; other models are untested).
- The remote connected to the PC over its **charge / tuning USB-C port** — *not* the phone
  socket. Plugging into the phone socket makes the remote act as a USB host and it will fail to
  enumerate (`Unknown USB Device (Device Descriptor Request Failed)`).
- The DJI VCOM driver, so the protocol interface is exposed as a COM port. Installing
  **DJI Assistant 2 (Consumer Drones Series)** provides it; without it Windows binds the generic
  in-box `usbser` driver and the port names differ (the bridge copes with both).
- Remote powered on. It is not paired, bound or flashed by anything in this project.

## Quick start

```bat
dji-rc-bridge.exe --scan        :: finds the remote; prints "remote protocol port: COMx"
dji-rc-bridge.exe --watch       :: live raw + normalised values, to confirm your inputs arrive
dji-rc-bridge.exe --calibrate    :: capture stick centres and end-stops, writes bridge.ini
dji-rc-bridge.exe --run          :: start bridging; open a game and play
```

`run-bridge.bat` in the source tree does the last step. Close the window or press `Ctrl+C` to
stop; the virtual pad is removed and the remote is returned to its normal state.

Verified on real hardware: ~86–89 Hz polling, **2.3–5.4 ms** serial round-trip, 0 bad frames over
a continuous session, 3600+ pad updates with 0 failures, and `Xbox 360 Controller for Windows`
(`HID\VID_045E&PID_028E`) visible to the OS.

## Key mapping

The remote has **four** digital keys, one 3-position switch, one wheel and two sticks — fewer
controls than a gamepad. The bridge solves this with **exclusive hold layers** (like a keyboard
`Fn`): while a modifier is held, only that layer's bindings are produced, so one press can never
fire two actions.

| Remote control | Base layer | L layer (hold `Fn`) | R layer (hold pause/RTH) |
| --- | --- | --- | --- |
| Left stick | LX / LY | LX / LY | LX / LY |
| Right stick | RX / RY | RX / RY | RX / RY |
| Wheel, forward | **RT** (analogue) | RT | RT |
| Wheel, backward | **LT** (analogue) | LT | LT |
| Shutter | A | START | A |
| Photo/video switch | X | LS | X |
| Custom key `Fn` | Y | *(enters L layer)* | Y |
| Pause / return-to-home | B | RS | *(enters R layer)* |
| Mode switch **S** | LB | LB | LB |
| Mode switch **N** | RB | RB | RB |
| Mode switch **C** | BACK | BACK | START |
| Right stick pushed to an edge | — | D-pad | — |

The single wheel becomes **both analogue triggers** by direction, which is what makes third-person
shooters and driving games playable (aim with LT, fire with RT). Digital thresholds such as
`stick.rx>0.8` are evaluated on **linear travel**, so they are unaffected by the expo curve applied
to aiming. The `GUIDE`/Xbox button is deliberately unmappable so the Windows game bar cannot be
opened by accident.

## Configuration

Two plain-text files live next to the executable, created on first run.

`bridge.ini`

```ini
[global]
port = auto              ; or COM7
channel_hz = 100         ; stick polling rate
button_hz = 50           ; button polling rate
watchdog_ms = 300        ; no data this long -> pad snaps to neutral
profile = default
auto_profile = on        ; switch profile from the foreground window

[dial]
mode = accumulate        ; accumulate (recommended) | absolute
gain = 3.5
decay_ms = 300
release_per_sec = 4.0

[axis.left_x]            ; one such section per axis: left_x left_y right_x right_y dial
min = 364
center = 1024
max = 1684
deadzone = 0.08
expo = 0.30
invert = off
```

`profiles/<name>.ini` — one per game or genre. Bindings are `slot = source`, and
`slot.<modifier> = source` for a hold layer.

```ini
[auto]
process = bf2042.exe gtav.exe justcause4.exe   ; lowercase, substring match

[hold]
ms = 350

[modifiers]
L = btn.fn
R = btn.rth

[pad]
A = btn.rec
LB = mode.sport
DPAD_RIGHT.L = stick.rx>0.8
LT = dial.neg
RT = dial.pos
```

Sources: `btn.fn` `btn.pv` `btn.rec` `btn.rth`, `mode.sport` `mode.normal` `mode.cine`,
`stick.lx|ly|rx|ry`, `dial` `dial.pos` `dial.neg` `dial.moved` `dial.absolute`,
`always` `never` `zero` `one` `const.<0..1>`, plus comparisons
`<axis>` `>` `>=` `<` `<=` and `!` (absolute value) with a threshold.
Slots: `A B X Y LB RB START BACK LS RS DPAD_UP DPAD_DOWN DPAD_LEFT DPAD_RIGHT` and
`LX LY RX RY LT RT`. An unrecognised slot or source is reported on stderr instead of being
silently dropped.

## Diagnostics

| Command | Purpose |
| --- | --- |
| `--scan` | List COM ports with DJI ranking, probe each with real DUML, name the working port |
| `--watch` | Raw + normalised axes, button bit changes, dial deltas, polling rate, RTT |
| `--calibrate` | Guided centre and end-stop capture, writes `bridge.ini` |
| `--listen` | Hold the port open and dump whatever arrives, without writing anything |
| `--raw` | Byte-level dump; sweep `--baud`, `--dtr`, `--rts`, `--source`, `--target` |
| `--sweep` | Sweep `cmd_type × cmd_set × cmd_id` looking for any reply |
| `--probe` | Walk command ids and print reply lengths |
| `--try` | Send one hand-built request (`--set 0x06 --id 0x01`) and decode the answer |
| `--baudsweep` | Try 27 baud rates in one pass |
| `--bulk`, `--libusb` | Probe the vendor bulk interfaces (`MI_00`/`MI_01`) |
| `pad-selftest` | Create the virtual pad with no hardware attached |
| `bridge-tests` | Offline unit tests: protocol golden vectors, framing, mapping, config |

## Building

```bat
build.bat          :: x64  -> build\dji-rc-bridge.exe
build-x86.bat      :: x86  -> build_x86\dji-rc-bridge.exe
```

Needs Visual Studio Build Tools (MSVC) with the C++ workload, CMake ≥ 3.20 and Ninja; the CRT is
linked statically so the resulting `.exe` is self-contained. Then run `bridge-tests.exe` — it
asserts the framing against byte-exact golden vectors and exercises the mapping engine without any
hardware.

## How the protocol looks on the wire

Every packet, both directions:

```
off  field
0    0x55                      sync
1    length low
2    length high | version<<2  bits 0-1 = length high, bits 10-15 = protocol version (1)
3    header CRC-8              over bytes 0..2, reflected poly 0x8C, seed 0x77
4    source                    0x0A = host application
5    target                    0x06 = remote controller
6-7  sequence                  little-endian
8    command type              0x40 = request; replies use 0x80 / 0x20
9    command set               0x06 = remote controller set
10   command id                see below
11.. payload
N-2  packet CRC-16             over bytes 0..N-3, little-endian, reflected poly 0x8408, seed 0x3692
```

| id | set | reply | contents |
| --- | --- | --- | --- |
| `0x01` | `0x06` | 38 bytes | axes at offsets 13/16/19/22/25, 16-bit LE, rest 1024, range ≈ 364…1684 |
| `0x27` | `0x06` | 58 bytes | byte 28 flight mode (`0x00` S / `0x10` N / `0x20` C), byte 29 button bits `0x02` Fn `0x04` photo/video `0x60` shutter `0x80` pause |
| `0x1E` | `0x06` | 19 bytes | status; byte 15 is battery percent. Pushed by the remote about once a second |
| `0x24` | `0x06` | — | payload `0x01` enables fast input streaming, `0x00` restores normal behaviour |

The physical power key is a hardware switch and is never reported, so it cannot be mapped.

## Credits

The protocol reconstruction stands on public work by others, consulted for cross-checking and
credited here without redistributing their code: [eraycn/dji-rc-n3](https://github.com/eraycn/dji-rc-n3),
[jonbrefe/DJI-RC-winGamePad](https://github.com/jonbrefe/DJI-RC-winGamePad),
[Samukashvili/RC-N1_ControllerBridge](https://github.com/Samukashvili/RC-N1_ControllerBridge),
[desowin/usbpcap](https://github.com/desowin/usbpcap) and the
[Wireshark](https://www.wireshark.org/) dissectors used to read the capture.
Virtual controller emulation uses [nefarius/ViGEmClient](https://github.com/nefarius/ViGEmClient) (MIT,
vendored under `third_party/`).

## License

[MIT](LICENSE) · see [DISCLAIMER.md](DISCLAIMER.md) for the affiliation, safety, warranty and
liability terms, which form part of the license notice.
