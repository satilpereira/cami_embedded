# Firmware implementation: ESP-IDF + NimBLE Myo client

Technical reference for how `cami_embedded` talks to the Myo armband
described in [myo-protocol-reference.md](myo-protocol-reference.md). Covers
the target platform, the port from an Arduino community library to native
ESP-IDF, the resulting component's design, and what has been concretely
verified against real hardware. See also
[docs/plans/myo-library-migration.md](../plans/myo-library-migration.md)
(the original step-by-step migration plan this implementation followed) and
[docs/requirements/](../requirements/) (the broader system requirements —
WiFi, MQTT, BLE, Myo — this firmware sits within).

## 1. Platform

- **Target**: ESP32 (original/"classic"), 4 MB flash, single 2.4 GHz radio
  shared between WiFi and Bluetooth (time-multiplexed coexistence, not two
  independent radios).
- **Framework**: ESP-IDF v6.0.2, native C/C++ project (`idf_component_register`
  components) — explicitly **not** using the Arduino-as-ESP-IDF-component
  route, and not the Arduino IDE/`.ino` toolchain at all.
- **libc**: Picolibc (`CONFIG_LIBC_PICOLIBC=y`), not newlib — relevant
  because newlib's "nano" formatting variant famously strips float support
  from `printf`; Picolibc doesn't have that limitation here.
- **Partitioning**: single 1 MB `factory` app partition, no OTA slot
  (`firmware/partitions.csv`). Firmware updates require a serial reflash.
  As of the state documented here, the built image uses roughly 470 KB
  (~54% of the partition free) with the full BLE/NimBLE/Myo stack linked in.
- **Bluetooth host stack**: **NimBLE**, not Bluedroid — chosen for its
  smaller flash/RAM footprint (relevant given the 1 MB, no-OTA partition).
  Enabled via Kconfig:
  ```
  CONFIG_BT_ENABLED=y
  CONFIG_BT_NIMBLE_ENABLED=y
  CONFIG_BT_BLUEDROID_ENABLED=n
  ```
  plus, specific to the ESP32-classic combo BT/BLE controller (other targets
  like ESP32-S3/C3 don't have this choice at all, since they lack classic
  BT hardware):
  ```
  CONFIG_BTDM_CTRL_MODE_BLE_ONLY=y
  ```

## 2. Component layout

```
firmware/
  app/            application component (app_main.cpp)
  board_hal/      hardware abstraction layer
  config/         per-target pin/config definitions (board.h)
  myo_ble/        <-- this component
    include/
      myo_bluetooth.h   Myo BLE protocol definitions (UUIDs, command structs)
      myo_armband.h      Armband class (public API)
    myo_armband.cpp      Armband class (implementation)
    idf_component.yml    declares the h2zero/esp-nimble-cpp dependency
```

`myo_ble` is a standalone ESP-IDF component with one external dependency,
resolved via the ESP-IDF **Component Manager** (`idf_component.yml`):

```yaml
dependencies:
  h2zero/esp-nimble-cpp: "^2.5.0"
```

This pulls `h2zero/esp-nimble-cpp` (a NimBLE C++ wrapper library, API
lineage shared with the old Arduino `BLEDevice`/`BLEClient` API this project
was ported from) from the ESP Component Registry into
`firmware/managed_components/` at build time, pinned by
`firmware/dependencies.lock`.

## 3. Porting from the Sparthan Myo Arduino library

The starting point was `project-sparthan/sparthan-myo`, an Arduino ESP32
library (`Arduino.h`, `arduino-esp32`'s Bluedroid-based `BLEDevice.h`,
Arduino `String`) previously used from `.ino` sketches. It does not compile
in this project as-is — there's no Arduino framework here at all. Rather
than adding `arduino-esp32` as an ESP-IDF component (which would work but
pull in a much larger, heavier framework than needed), the BLE client layer
was rewritten against `h2zero/esp-nimble-cpp` directly, while the actual
**protocol definitions** (`myo_bluetooth.h` — UUIDs, command byte layouts,
data structs) needed no Arduino-specific changes at all and were carried
over essentially unchanged (one line: `#include <stdint.h>` had to be
un-commented, since it used to rely on `Arduino.h` supplying that
transitively).

### 3.1 Two real bugs found and fixed during the port

**Off-by-one buffer overflow in device-info parsing.** The original
library's `get_info()` manually indexed into the raw characteristic bytes
and wrote `fw_serial_number[6]` — one byte past the end of the 6-byte
`serial_number` array declared in its own header — which also shifted
every field parsed after it by one byte, and read one byte past the end of
the 20-byte value at the tail. It was apparently never caught because the
one sketch known to have run on real hardware
(`myo_mqtt_bridge.ino`) never called `get_info()`/`get_firmware()` at all.
This port replaced the manual byte indexing with a direct typed read —
`characteristic->readValue<myohw_fw_info_t>()` — reinterpreting the raw
bytes as the already-defined packed struct, which both fixes the bug and
is less code. Verified correct against real hardware: the read serial
number, byte-reversed, matched the device's own BLE MAC address (Myo
derives its BLE address from its serial number), and the firmware version
read back as `1.5.1970` — a real, known Myo firmware version — with no
byte-swapping needed (the header's own comment claims "all values are
big-endian," which turned out to be inaccurate for this hardware; direct
little-endian struct reinterpretation on the ESP32 was correct as-is).

**Only 1 of 4 EMG characteristics had a callback wired.** Myo splits its
~200 Hz EMG stream across 4 separate BLE characteristics
(`EmgData0-3Characteristic`). The original library's `emg_notification()`
enabled BLE notifications (wrote the CCCD descriptor) on all 4, but only
*returned* one characteristic pointer, and the calling `.ino` sketches only
ever called `registerForNotify()` on that single returned pointer. The
other 3 characteristics had notifications turned on at the GATT level with
no callback attached to receive them — meaning the previously-"working"
Arduino sketches were silently only observing roughly a quarter of the EMG
bandwidth. This port's `subscribeEmg()` loops over all 4 characteristics
and subscribes the same callback to each, so the full stream is delivered.

### 3.2 API modernization (esp-nimble-cpp 1.x → 2.x plus general cleanup)

- `registerForNotify()` + manual CCCD descriptor writes → single
  `characteristic->subscribe(bool notifications, callback)` call (the
  underlying library's 2.x API removed manual CCCD handling as a public
  concern).
- Manual byte-array command construction → `writeValue<T>(const T& v)`
  with the packed `myohw_command_*_t` structs already defined in the
  protocol header.
- Manual byte-array response parsing → `readValue<T>()`, reinterpreting
  the raw response as a typed struct (see the bug fix above).
- Blocking scan-until-found loop (`while(!detected) { scan->start(10); }`)
  → `NimBLEScan::getResults(duration_ms, false)`, the 2.x replacement for
  the old blocking scan API, used in the same retry-until-found shape to
  preserve the original library's synchronous `connect()` semantics.
- `Serial.print`/`delay()` → `ESP_LOGx`/`vTaskDelay(pdMS_TO_TICKS(...))`.

## 4. `Armband` class API

Public surface of `firmware/myo_ble/include/myo_armband.h`:

| Method | Purpose |
|---|---|
| `bool connect(uint32_t scan_window_ms = 5000)` | Scans for and connects to a Myo (blocks until found; safe to call again after a disconnect) |
| `bool isConnected() const` | Current connection state |
| `bool readInfo(myohw_fw_info_t&)` | Synchronous read: serial number, unlock pose, SKU, etc. |
| `bool readFirmwareVersion(myohw_fw_version_t&)` | Synchronous read: firmware major/minor/patch/hw-rev |
| `bool unlock(myohw_unlock_type_t = myohw_unlock_hold)` | Unlock command |
| `bool setMode(emg_mode, imu_mode, classifier_mode)` | Choose active data streams |
| `bool vibrate(myohw_vibration_type_t)` | Haptic feedback |
| `bool setSleepMode(myohw_sleep_mode_t)` | Disable/restore auto-sleep-on-inactivity |
| `bool userAction(myohw_user_action_type_t = myohw_user_action_single)` | Notify Myo of a recognized user action |
| `bool subscribeBattery(notify_callback)` | Battery level notifications (standard BLE service) |
| `bool subscribeEmg(notify_callback)` | All 4 EMG characteristics, same callback |
| `bool subscribeImu(notify_callback)` | Orientation/accel/gyro notifications |
| `bool subscribeGesture(notify_callback)` | Classifier events (poses, arm sync state) — indicate, not notify |

### 4.1 Internal design: avoiding duplication across 13 protocol operations

Three private helpers do all the actual GATT work, so each public method is
just "build a UUID/struct, call the helper":

- **`NimBLEUUID myo_uuid(myohw_services)`** (anonymous-namespace free
  function) — builds a full 128-bit UUID from a `myohw_services` short
  code via the `d506<code>-a904-...` pattern (verified to hold for every
  UUID referenced in the original library). Standard Bluetooth UUIDs
  (Battery) instead use `NimBLEUUID(uint16_t)` directly, which expands
  against the *standard* base UUID — a different UUID space, so it's kept
  as a separate, explicit call rather than folded into `myo_uuid()`.
- **`NimBLERemoteCharacteristic* characteristic(const NimBLEUUID& service, const NimBLEUUID& chr)`**
  — service/characteristic lookup, `nullptr` if not connected or not found.
- **`bool subscribeCharacteristic(service, chr, notifications, callback)`**
  — looks up a characteristic and subscribes to it in notify *or* indicate
  mode (the `notifications` bool), used by all 4 `subscribe*()` public
  methods including the indicate-only gesture characteristic.
- **`bool writeCommand(const void* data, size_t len)`** — looks up
  `CommandCharacteristic` and writes raw bytes; the 5 command methods
  (`unlock`, `setMode`, `vibrate`, `setSleepMode`, `userAction`) each just
  populate their own packed struct and call this.

No UUID string or GATT-lookup-and-check sequence is repeated across the 13
public methods — this was a deliberate fix versus the original library,
which reconstructed the same full UUID strings inline in nearly every
method.

### 4.2 Reconnect behavior

`ClientCallbacks::onDisconnect` (a `NimBLEClientCallbacks` override) flips
`connected_` back to `false` on any disconnect. `connect()` is safe to call
again afterward — it reuses the existing `NimBLEClient` object (matching
the supported esp-nimble-cpp reconnect pattern) and, since it's called with
default `deleteAttributes=true`, forces a fresh GATT service/characteristic
discovery rather than risking stale cached attribute pointers from the
previous connection. **BLE subscriptions and device-side state (unlock,
mode, sleep override) do not survive a disconnect** and must be redone —
see §5 for how the application layer handles this.

## 5. Current state of `app_main.cpp`

`firmware/app/app_main.cpp` is currently a **sequential validation
harness**, not application logic — it was built up one step at a time to
prove each piece of the port against real hardware, and exercises every
`Armband` capability in one place: connect, read info, unlock, set mode,
force no-sleep, vibrate, and subscribe to battery/gesture (EMG and IMU
subscriptions are implemented and were confirmed working, but are currently
`#if 0`-disabled by default since their log volume drowns out the sparse
gesture events — flip to `#if 1` to re-enable). A `setup_myo()` function
holds this whole sequence and is re-run automatically from the main loop
whenever `myo.isConnected()` goes false, so a dropped connection recovers
without a device reboot.

**What this firmware does *not* yet do**: nothing here talks to WiFi or
MQTT, and there's no downstream consumer of the EMG/IMU/gesture data beyond
serial log lines — that integration is scoped in
[docs/requirements/](../requirements/) but not yet implemented. The LED
blink in the main loop is a leftover "hello world" heartbeat, not a
meaningful status indicator.

### 5.1 Planned data transport (decided, not yet implemented)

The next milestone is WiFi + MQTT (see
[docs/requirements/wifi-connectivity.md](../requirements/wifi-connectivity.md)
and
[docs/requirements/mqtt-connectivity.md](../requirements/mqtt-connectivity.md)),
publishing to a local Mosquitto broker for a Python script to consume
(near-term), and eventually a second, dedicated ESP32 doing signal
processing (longer-term — this board is planned to stay BLE-only).

The wire format is decided ahead of that implementation: **fixed binary
packets, one struct per MQTT topic** — one topic per Myo data type
(EMG/IMU/gesture/battery), each carrying the corresponding
`myohw_*_t` struct (§3-4) more or less as-is, rather than JSON, a
TLV-encoded stream, or a bit-packed format. This leans on MQTT's topics to
do the "what type of message is this" job that TLV would otherwise need a
type tag for, and avoids solving a bandwidth problem (bit-packing) that
doesn't exist on a local WiFi/Mosquitto link at these data rates. Full
rationale in `mqtt-connectivity.md`'s "Wire format decision" section.
Not yet decided: exact topic names and whether a timestamp/sequence number
is prepended to each packet.

## 6. Verification log (Steps 0-9)

Each step below was flashed to real hardware and confirmed via serial
monitor before moving to the next, per
[docs/plans/myo-library-migration.md](../plans/myo-library-migration.md):

| Step | What was added | Confirmed on hardware |
|---|---|---|
| 0 | NimBLE Kconfig + `myo_ble` component scaffolding | Clean build, `esp-nimble-cpp` compiles and links |
| 1 | Scan + connect | Found device at its BLE MAC, GAP connect + GATT MTU exchange, `onConnect` fired |
| 2 | `readInfo`/`readFirmwareVersion` | Serial number = byte-reversed MAC (sanity check passed); firmware `1.5.1970`, hw rev 2; unlock_pose=5 (valid enum value) — see §3.1 for why this also validated the bug fix |
| 3 | `unlock`/`setMode`/`vibrate` | Felt vibration; GATT write lengths exactly matched struct sizes (3, 5, 3 bytes) |
| 4 | `subscribeBattery` | Periodic battery % notifications observed |
| 5 | `subscribeEmg` | All 4 characteristics streaming; channel values responded to forearm flexing |
| 6 | `subscribeImu` | Quaternion/accel/gyro values responded to armband movement |
| 7 | `subscribeGesture` | Pose names (fist, wave in/out, fingers spread, double tap, rest) and arm-sync events logged |
| 8 | `setSleepMode`/`userAction` | Armband stopped auto-disconnecting from inactivity once `never_sleep` was set |
| 9 | Reconnect robustness | Full resetup sequence (unlock → mode → sleep → vibrate → resubscribe) re-runs automatically after a forced disconnect, with no ESP32 reboot |

## 7. Sources / further reading

- [myo-protocol-reference.md](myo-protocol-reference.md) — the Myo device
  and BLE protocol itself (EMG/IMU details, GATT map, command reference).
- [docs/plans/myo-library-migration.md](../plans/myo-library-migration.md)
  — the original migration plan, written before implementation, including
  the verified `h2zero/esp-nimble-cpp` API mapping table.
- [docs/requirements/](../requirements/) — WiFi/MQTT/BLE/Myo system
  requirements this firmware is one piece of.
- `h2zero/esp-nimble-cpp` (ESP Component Registry, v2.5.0 at time of
  writing) — the NimBLE C++ library this component depends on.
- `project-sparthan/sparthan-myo` — the Arduino library this component was
  ported from.
