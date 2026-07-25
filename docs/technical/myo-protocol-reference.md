# Myo armband: hardware, signals, and BLE protocol reference

Technical reference for the Thalmic Labs Myo gesture armband as used in this
project. Written from the reverse-engineered BLE protocol (there is no
surviving official specification; Thalmic Labs discontinued the product and
shut down its cloud services) and from values actually read off a physical
unit during development (see the "Verified on real hardware" notes
throughout). Companion doc: [firmware-implementation.md](firmware-implementation.md)
covers the ESP32/ESP-IDF side that talks to the device described here.

## 1. Background

The Myo is an 8-sensor surface-EMG + 9-axis IMU armband released by Thalmic
Labs (Canada) in 2013, discontinued around 2018. It was designed to be worn
on the forearm and communicate over Bluetooth Low Energy with a host
(originally Windows/macOS via "Myo Connect", later a mobile SDK). No new
units are manufactured; working hardware only exists second-hand. The
manufacturer's SDK and cloud pairing services are gone, so any modern
integration — this one included — talks directly to the device's BLE GATT
interface using UUIDs and command formats recovered by the community (the
`sparthan-myo` Arduino library this project's [`myo_ble`
component](../../firmware/myo_ble/) descends from is one such community
reimplementation; see
[firmware-implementation.md](firmware-implementation.md) for the porting
details).

**Verified on real hardware** (read via this project's firmware): firmware
version **1.5.1970**, hardware revision **2 (`myohw_hardware_rev_revd`,
i.e. "Myo REV-D")** — the standard consumer hardware revision, not the
earlier alpha "REV-C" boards. SKU field read as **0 (unknown)**, which the
protocol header documents as "default value for old firmwares."

## 2. BLE role and connection model

The Myo is a BLE **peripheral** (GATT server); a host (this project's ESP32
firmware) connects to it as a **central** (GATT client). It supports one
active central connection at a time. On power-on it advertises with a
service UUID matching its `ControlService` (see §4); a central scans for
that UUID to find it rather than relying on a fixed name.

The device ships in a **locked** state and must be explicitly unlocked
before most functionality (streaming, classifier events) becomes fully
active — see §7 (Commands) and §8 (Locking model).

## 3. UUID scheme

All of Myo's custom services/characteristics share a 128-bit base UUID with
a 16-bit "short code" spliced into it:

```
d506<CODE>-a904-deb9-4748-2c7f4a124842
```

e.g. `ControlService` (short code `0x0001`) is
`d5060001-a904-deb9-4748-2c7f4a124842`. This project builds these
programmatically from the short codes (`myo_uuid()` helper in
[`myo_armband.cpp`](../../firmware/myo_ble/myo_armband.cpp)) rather than
hardcoding full UUID strings per characteristic.

Two characteristics (Battery) use the **standard Bluetooth SIG** base UUID
instead (`0000<CODE>-0000-1000-8000-00805F9B34FB`), since battery reporting
is a standard BLE service, not Myo-specific.

## 4. GATT service/characteristic map

| Service | Code | Characteristic | Code | Full UUID | Access | Payload |
|---|---|---|---|---|---|---|
| ControlService | `0x0001` | MyoInfoCharacteristic | `0x0101` | `d5060101-...` | Read | `myohw_fw_info_t` (20 B) |
| ControlService | `0x0001` | FirmwareVersionCharacteristic | `0x0201` | `d5060201-...` | Read | `myohw_fw_version_t` (8 B) |
| ControlService | `0x0001` | CommandCharacteristic | `0x0401` | `d5060401-...` | Write | `myohw_command_*_t` (variable) |
| ImuDataService | `0x0002` | IMUDataCharacteristic | `0x0402` | `d5060402-...` | Notify | `myohw_imu_data_t` (20 B) |
| ImuDataService | `0x0002` | MotionEventCharacteristic | `0x0502` | `d5060502-...` | Indicate | `myohw_motion_event_t` (3 B) — **not implemented in this port** |
| ClassifierService | `0x0003` | ClassifierEventCharacteristic | `0x0103` | `d5060103-...` | Indicate | `myohw_classifier_event_t` (3 B) |
| EmgDataService | `0x0005` | EmgData0-3Characteristic (×4) | `0x0105/0205/0305/0405` | `d5060105-...` etc. | Notify (×4) | `myohw_emg_data_t` (16 B) each |
| Battery Service (standard) | `0x180F` | Battery Level (standard) | `0x2A19` | `0000180f-.../00002a19-...` | Read + Notify | 1 byte, 0-100% |

## 5. EMG (electromyography)

- **8 channels**, one per electrode pad around the armband, each an
  `int8_t` (signed, range -128..127) — a raw/filtered amplitude sample of
  the electrical signal at that skin contact point, not a physical unit.
  The signal is AC-like around a zero baseline (muscle fiber action
  potentials), so sign is part of the waveform, not a magnitude indicator.
- **Streaming rate**: `MYOHW_EMG_DEFAULT_STREAMING_RATE` = **200 Hz**
  aggregate. Each BLE notification packet (`myohw_emg_data_t`) carries
  **2 samples** (`sample1`, `sample2`) of all 8 channels = 16 bytes, and
  this 200 Hz stream is **split across the 4 `EmgDataN` characteristics**
  rather than sent on one — each characteristic delivers roughly a quarter
  of the total packet rate. All 4 must be subscribed to receive the full
  stream (see §6.1 of the implementation doc for a real bug this caused in
  the original Arduino library).
- **EMG modes** (set via the `set_mode` command, §7):
  - `send_emg` (`0x02`) — filtered EMG (the mode used in this project).
  - `send_emg_raw` (`0x03`) — unfiltered/raw EMG.
  - `none` (`0x00`) — EMG streaming off.
- **Verified on real hardware**: at rest, channel values sit near 0 with
  small noise; flexing the forearm produces swings in the tens (observed
  e.g. `28 86 88 12 4 -7 -13 0`), consistent with a live, correctly-decoded
  8-channel signal.

## 6. IMU (inertial measurement unit)

`myohw_imu_data_t` (20 bytes) carries three sub-signals, all `int16_t`,
default sample rate `MYOHW_DEFAULT_IMU_SAMPLE_RATE` = **50 Hz**:

| Field | Description | Scale constant | Physical unit after scaling |
|---|---|---|---|
| `orientation.w/x/y/z` | Unit quaternion (absolute 3D orientation) | `MYOHW_ORIENTATION_SCALE` = 16384.0 | dimensionless, [-1, 1] |
| `accelerometer[0..2]` | Linear acceleration incl. gravity, per axis | `MYOHW_ACCELEROMETER_SCALE` = 2048.0 | g (9.81 m/s²) |
| `gyroscope[0..2]` | Angular velocity, per axis | `MYOHW_GYROSCOPE_SCALE` = 16.0 | degrees/second |

**IMU modes** (set via `set_mode`): `none` (`0x00`), `send_data` (`0x01`,
continuous accel/gyro/orientation — used in this project),
`send_events` (`0x02`, discrete motion events like taps, delivered via
`MotionEventCharacteristic` instead), `send_all` (`0x03`), `send_raw`
(`0x04`).

**How to sanity-check IMU data** (useful for a paper's validation section):
quaternion magnitude (`w²+x²+y²+z²`) should be constant regardless of
orientation (unit quaternion); accelerometer magnitude
(`√(ax²+ay²+az²)`) should be ≈1g at rest regardless of orientation, since
gravity redistributes across axes as the arm tilts rather than
disappearing; gyroscope should sit near 0 at rest and spike proportionally
to rotation speed.

## 7. Onboard gesture classifier

The Myo has a **built-in, non-trainable classifier** running on the device
itself that recognizes a fixed set of poses from the EMG pattern and
reports discrete events over BLE — no host-side ML/signal processing is
required to get basic gesture recognition, at the cost of being limited to
this fixed vocabulary (a custom/trained classifier is a documented but
unused capability, `myohw_classifier_model_custom`).

### 7.1 Pose vocabulary (`myohw_pose_t`)

| Value | Pose |
|---|---|
| `0x0000` | rest |
| `0x0001` | fist |
| `0x0002` | wave in |
| `0x0003` | wave out |
| `0x0004` | fingers spread |
| `0x0005` | double tap |
| `0xFFFF` | unknown |

### 7.2 Classifier events (`myohw_classifier_event_t`, via `ClassifierEventCharacteristic`)

This is an **indicate**-only characteristic (not notify — requires
acknowledged delivery), and carries more than just poses:

| Event type | Value | Payload |
|---|---|---|
| `arm_synced` | `0x01` | which arm (`myohw_arm_right`/`left`/`unknown`) + orientation of the +x axis relative to the wrist/elbow |
| `arm_unsynced` | `0x02` | — |
| `pose` | `0x03` | a `myohw_pose_t` value (§7.1) |
| `unlocked` | `0x04` | — |
| `locked` | `0x05` | — |
| `sync_failed` | `0x06` | reason (currently only "sync gesture performed too hard") |

**Arm sync requirement**: the classifier's pose output is only meaningful
once the device reports `arm_synced`. Normally a user performs a "sync
gesture" (a firm fist-and-hold shortly after putting the armband on) which
the official Myo Connect software would prompt for; without that software,
this project observes the sync events directly over BLE instead and relies
on the gesture happening organically or being deliberately performed.

## 8. Locking model

The Myo ships in a locked/idle state on every connection and must be
explicitly unlocked (`unlock` command, §9) before streaming and classifier
output work as expected. This is a power-management feature, not security —
there's no pairing/authentication requirement layered on top for basic GATT
access (bonding is not required for the operations this project performs).

**Sleep behavior**: independently of locking, the Myo auto-sleeps
(disconnecting) after a period of inactivity to save its fixed, non-swappable
battery. `set_sleep_mode(never_sleep)` (§9) disables this for applications
that need continuous streaming, at the cost of battery life — this project
sets it on every (re)connection.

## 9. Commands (`CommandCharacteristic`, write-only)

Every command shares a 2-byte header (`command` id, `payload_size`) followed
by a command-specific payload — all defined as packed C structs in
`myohw.h`/`myo_bluetooth.h`, which this project writes directly via
`writeValue<T>()` rather than hand-building byte arrays.

| Command | ID | Payload | Total bytes | Purpose |
|---|---|---|---|---|
| `set_mode` | `0x01` | emg_mode, imu_mode, classifier_mode (1 B each) | 5 | Choose which data streams are active |
| `vibrate` | `0x03` | vibration type (1 B: none/short/medium/long) | 3 | Haptic feedback |
| `deep_sleep` | `0x04` | — | 2 | Force deep sleep (not used in this port) |
| `vibrate2` | `0x07` | 6× (duration:u16, strength:u8) steps | 20 | Extended/patterned vibration (not used in this port) |
| `set_sleep_mode` | `0x09` | sleep mode (1 B: normal/never_sleep) | 3 | Disable/restore auto-sleep |
| `unlock` | `0x0a` | unlock type (1 B: lock/timed/hold) | 3 | Unlock (or re-lock) the device |
| `user_action` | `0x0b` | action type (1 B) | 3 | Tell the Myo a recognized action occurred (rarely used directly) |

**Verified on real hardware**: BLE GATT "write no rsp" byte lengths
captured during testing matched these sizes exactly — 3 bytes for `unlock`,
5 bytes for `set_mode`, 3 bytes for `vibrate` — confirming the packed
struct layout and header/`payload_size` fields are being encoded correctly.

## 10. Battery

Reported via the **standard** Bluetooth Battery Service/Level characteristic
(not part of Myo's custom UUID space) as a single byte, 0-100%, both
readable and subscribable for notifications.

## 11. Sources

- Community-maintained protocol header (`myohw.h`/`myo_bluetooth.h`
  lineage), as reverse-engineered after Thalmic Labs' SDK/documentation
  went offline; this project's copy lives at
  [`firmware/myo_ble/include/myo_bluetooth.h`](../../firmware/myo_ble/include/myo_bluetooth.h).
- `project-sparthan/sparthan-myo` (Arduino library this project's BLE
  client was ported from) — see
  [firmware-implementation.md](firmware-implementation.md) §2 for the
  porting details and bug fixes found along the way.
- Direct verification against one physical Myo unit during this project's
  development (values cited throughout as "Verified on real hardware").
