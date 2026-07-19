# Migration plan: Sparthan Myo library → native ESP-IDF

Companion to [ble-connectivity.md](../requirements/ble-connectivity.md) and
[myo-armband-connectivity.md](../requirements/myo-armband-connectivity.md).
This doc is the *how*; those are the *what/why*.

## Source material reviewed

- `_tmp/sparthanmyo/` (also present, identical, at `~/Arduino/libraries/Sparthan_Myo`)
  — the Arduino ESP32 library previously used from `.ino` sketches.
- `~/Arduino/myo_mqtt_bridge/myo_mqtt_bridge.ino` — a previously working sketch
  built on this library (connect, unlock, set EMG mode, force no-sleep,
  subscribe to EMG + battery, publish over MQTT via a FreeRTOS queue). This is
  the closest thing to a proven reference for call order and timing.

## Verdict: not usable as-is, protocol layer is fine

The library is Arduino-flavored end to end:

- `myo.h` includes `Arduino.h` and `<BLEDevice.h>` (the `arduino-esp32` core's
  Bluedroid-based BLE wrapper) and returns Arduino `String` from reads.
- `myo.cpp` uses `Serial.print`/`delay()` for logging/timing and the
  `BLEDevice`/`BLEClient`/`BLERemoteCharacteristic`/`BLEAdvertisedDeviceCallbacks`
  class family, none of which exist outside the Arduino framework.

None of that compiles in `firmware/` as it stands today (pure ESP-IDF,
`idf_component_register`, no Arduino component). Per the "raw embedding"
decision, we are **not** pulling in `arduino-esp32` as an IDF component —
we're porting to native ESP-IDF instead.

The good news: `src/includes/myo_bluetooth.h` (the actual Myo BLE protocol —
service/characteristic UUIDs, command byte layouts, EMG/IMU/pose structs) is
plain, portable C with **no Arduino dependency** (it doesn't even use
`Arduino.h`'s `uint8_t`/`uint16_t` — it relies on something upstream having
already pulled in `<stdint.h>`, since its own include is commented out). This
file can move over almost unchanged — see Step 0.

## Target: `h2zero/esp-nimble-cpp`

Verified live against the actual repo (not from memory) since this library's
API had breaking 1.x→2.x changes:

- Available on the ESP-IDF Component Registry as `h2zero/esp-nimble-cpp`,
  current version **2.5.0** (released 2026-04-01).
- Ships an official **native ESP-IDF example** (`examples/NimBLE_Client`,
  plain `main.cpp` + `CMakeLists.txt`, no Arduino) that was pulled and read in
  full to confirm the exact current method signatures used below.
- It's the same API family the old `BLEDevice`/`BLEClient`/
  `BLERemoteCharacteristic` classes in the Sparthan library descend from
  (both trace back to nkolban's original ESP32 BLE library) — class shapes
  are recognizably similar, just renamed `BLE*` → `NimBLE*` with some
  method changes.
- Matches the NimBLE recommendation already made in
  [ble-connectivity.md](../requirements/ble-connectivity.md) (smaller
  footprint than Bluedroid, relevant given the single 1 MB factory partition).

## Step 0 — Prerequisites (do first, once)

1. **Enable NimBLE in Kconfig.** Add to `firmware/sdkconfig.defaults` (or a
   new `firmware/sdkconfig.defaults.esp32` block — the ESP32-classic-specific
   `BTDM_CTRL_MODE_*` options only exist for chips with a combo BT controller,
   so this may need to be board-specific per the existing
   `sdkconfig.defaults.<board>` convention):
   ```
   CONFIG_BT_ENABLED=y
   CONFIG_BTDM_CTRL_MODE_BLE_ONLY=y
   CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY=n
   CONFIG_BTDM_CTRL_MODE_BTDM=n
   CONFIG_BT_BLUEDROID_ENABLED=n
   CONFIG_BT_NIMBLE_ENABLED=y
   ```
   (Pulled directly from esp-nimble-cpp's own example `sdkconfig.defaults`.)
2. **Add the component dependency.** New `firmware/myo_ble/idf_component.yml`
   (component name TBD in Step 1) with:
   ```yaml
   dependencies:
     h2zero/esp-nimble-cpp: "^2.5.0"
   ```
   `idf.py build` will fetch it into `firmware/managed_components/` on next
   build — nothing to vendor by hand.
3. **Carry over `myo_bluetooth.h` almost unchanged.** Copy
   `_tmp/sparthanmyo/src/includes/myo_bluetooth.h` in as-is, with one fix:
   uncomment `#include <stdint.h>` at the top, since there's no `Arduino.h`
   upstream to supply it implicitly anymore. Everything else in that file
   (UUIDs, command enums, packed structs) needs no changes — it was never
   Arduino-dependent.
4. **Fix a real bug while we're in there.** `myo.cpp`'s `get_info()` writes
   `fw_serial_number[6]` into a 6-byte array (`uint8_t fw_serial_number[6]`
   in `myo.h`, valid indices 0–5) — a one-byte out-of-bounds write, which
   also shifts every field parsed after it by one byte and reads one byte
   past the 20-byte `myohw_fw_info_t` struct at the end. It was likely never
   caught because `myo_mqtt_bridge.ino` (the one sketch we know worked on
   real hardware) never calls `get_info()` or `get_firmware()` — only
   `connect/unlock/set_myo_mode/set_sleep_mode/vibration/emg_notification/
   battery_notification`. The port fixes this for free (see Step 2).

## API mapping (old Arduino → new NimBLE C++, verified)

| Old (Sparthan/Arduino BLE) | New (esp-nimble-cpp 2.5.0) | Notes |
|---|---|---|
| `BLEDevice::init("")` | `NimBLEDevice::init("")` | same |
| `BLEScan`, `setAdvertisedDeviceCallbacks` | `NimBLEScan`, `NimBLEScanCallbacks` | callback class renamed |
| `BLEAdvertisedDeviceCallbacks::onResult(BLEAdvertisedDevice)` | `NimBLEScanCallbacks::onResult(const NimBLEAdvertisedDevice*)` | now a pointer |
| `pBLEScan->start(10)` (loop until found) | `pScan->getResults(duration_ms, false)` (blocking, returns `NimBLEScanResults`) | time is now **milliseconds**; closest 1:1 replacement for the original's blocking-loop style |
| `BLEClient`, `BLEClientCallbacks` | `NimBLEClient`, `NimBLEClientCallbacks` | `onDisconnect(NimBLEClient*, int reason)` |
| `pClient->connect(address)` | `pClient->connect(advDevice)` (bool return) | takes the advertised-device pointer, not just an address, in the current example |
| `getService(uuid)->getCharacteristic(uuid)` | same shape | `NimBLERemoteService*` / `NimBLERemoteCharacteristic*`, unchanged usage |
| `->readValue()` → Arduino `String` | `->readValue()` → `NimBLEAttValue` (has `.data()`, `.c_str()`, `.size()`) | **also** has a template form: `chr->readValue<T>()` returns `T` directly via `reinterpret_cast` — use this for `myohw_fw_info_t` / `myohw_fw_version_t` instead of manual byte indexing (fixes the Step 0.4 bug) |
| `->writeValue(uint8_t buf[], len)` | same, **plus** `->writeValue(const T& v)` template for POD types | can write a `myohw_command_vibrate_t` struct directly instead of hand-building a byte array |
| manual `getDescriptor(0x2902)->writeValue(...)` **then** `->registerForNotify(cb)` | single `->subscribe(true /*notify*/, cb)` or `->subscribe(false /*indicate*/, cb)` | `registerForNotify` was removed in 2.x; `subscribe` does the CCCD write internally — the original library's two-step dance collapses into one call |
| notify callback: `void cb(BLERemoteCharacteristic*, uint8_t* pData, size_t length, bool isNotify)` | `void cb(NimBLERemoteCharacteristic*, uint8_t* pData, size_t length, bool isNotify)` | **identical shape**, only the pointer type name changes — `myo_mqtt_bridge.ino`'s `emgCallback`/`batteryCallback` bodies port with a find-and-replace on the parameter type, nothing inside them needs to change |
| `Serial.println(...)` | `ESP_LOGI(TAG, ...)` | |
| `delay(ms)` | `vTaskDelay(pdMS_TO_TICKS(ms))` | |

## Where the ported code lives

New component `firmware/myo_ble/` (sibling to `app`, `board_hal`, `config`,
matching the existing per-concern component layout), added to
`EXTRA_COMPONENT_DIRS` in `firmware/CMakeLists.txt`. Public surface kept
close to the original `armband` class so call sites read the same as the
proven `.ino` code (`myo.connect()`, `myo.unlock(...)`, `myo.set_myo_mode(...)`,
etc.) — this is a port, not a redesign.

One architectural note for the "maybe a phone connects later" idea you
mentioned: that would be the ESP32 acting in the GATT **server/peripheral**
role, which is a separate concern from this central-role Myo client (see
[ble-connectivity.md](../requirements/ble-connectivity.md) scope — peripheral
role is explicitly out of scope for v1). Keeping `NimBLEDevice::init()` and
any shared central-role scan/connect helpers separate from Myo-specific GATT
code (rather than baking Myo assumptions into a single monolithic BLE init)
costs nothing now and avoids a rewrite if/when that's picked up later. Not
building it now, per your "but for now" — just not painting it into a corner.

## Migration steps

Each step is independently testable by flashing and reading serial output
(`make flash-monitor`), consistent with how `tests/` already validates the
project (pytest-embedded reading serial output from real hardware).

### Step 1 — Connect

**Goal:** replicate `armband::connect()` — scan for the Myo's advertised
service UUID (`d5060001-a904-deb9-4748-2c7f4a124842`), connect, and set
`connected`/`detected` state, using `getResults()` + `NimBLEClient` per the
mapping table above.
**Verify:** serial log shows the device found, address, and "Connected".

### Step 2 — Device info & firmware version

**Goal:** port `get_info()`/`get_firmware()`, but via
`readValue<myohw_fw_info_t>()` / `readValue<myohw_fw_version_t>()` instead of
manual byte indexing — this is the fix for the Step 0.4 bug, not a
carried-over risk.
**Why do this early:** it's a simple synchronous read/response round trip
with no subscriptions involved — the cheapest possible smoke test that the
port's connect/service-discovery path actually works before adding
notification complexity.
**Verify:** serial log shows a plausible serial number, firmware version,
and unlock pose — sanity-checked against whatever the Arduino sketch printed
previously, if that was ever captured.

### Step 3 — Unlock + set mode

**Goal:** port `unlock()` and `set_myo_mode()`. Required before any
streaming works — the Myo starts locked/idle. Mirror the proven sequence
from `myo_mqtt_bridge.ino`: `unlock(myohw_unlock_hold)` →
`set_myo_mode(emg_mode, imu_mode, classifier_mode)`, with the same
"critical" delays between commands (the original comments suggest the Myo
needs time to process each command before the next).
**Verify:** no errors; armband should give the short vibration if you also
port `vibration()` here as a physical confirmation (it's a one-line command,
cheap to include).

### Step 4 — Battery stats

**Goal:** port `battery_notification()` using `subscribe(true, cb)` against
the standard Battery Service (`0x180f`/`0x2a19` — no Myo-specific UUID
needed here, these are standard BLE).
**Verify:** serial log shows periodic battery level updates.

### Step 5 — Raw sensors: EMG

**Goal:** port `emg_notification()` (4 characteristics,
`d5060105`/`0205`/`0305`/`0405`, each `subscribe(true, cb)`).
**Verify:** serial log streams 8-channel int8 samples while the armband is
worn and flexed; rate should be in the ballpark of `MYOHW_EMG_DEFAULT_STREAMING_RATE`
(200 Hz) — this is where MYO-3's raw-vs-classifier bandwidth question from
myo-armband-connectivity.md becomes concrete, not hypothetical.

### Step 6 — Raw sensors: IMU

**Goal:** port `imu_notification()` (single characteristic `d5060402`).
**Verify:** serial log shows orientation quaternion + accel + gyro values
that respond sensibly to moving the armband.

### Step 7 — Gesture/classifier events

**Goal:** port `gesture_notification()` — note this one is **indicate**, not
notify (`ClassifierEventCharacteristic` is indicate-only per
`myo_bluetooth.h`), so it's `subscribe(false, cb)`, not `subscribe(true, cb)`.
Requires classifier mode enabled in Step 3's `set_myo_mode()` call.
**Verify:** serial log prints pose names (fist, wave in/out, etc.) matching
`myo_print.ino`'s reference behavior when performing the gestures.

### Step 8 — Remaining commands

**Goal:** port `vibration()` (if not already done in Step 3),
`set_sleep_mode()` (force never-sleep, per the proven sketch), and
`user_action()`. Good candidates for `writeValue<T>(struct)` using the
packed command structs directly instead of hand-built byte arrays.
**Verify:** vibration felt on command; armband doesn't auto-sleep during an
extended idle period once `set_sleep_mode(myohw_sleep_mode_never_sleep)` is
active.

### Step 9 — Reconnect robustness

**Goal:** wire up `NimBLEClientCallbacks::onDisconnect` to flip the
connected flag and restart scanning, then have the app-level loop redo
unlock → set mode → re-subscribe on reconnect — this is exactly what
`myo_mqtt_bridge.ino`'s `loop()` already does by polling `!myo.connected`;
same shape, ported callback types.
**Verify:** power-cycle or walk the armband out of range and back; streaming
resumes without a device reboot (this is BLE-5 / MYO-8 from the requirements
docs, now concretely testable).

## Non-goals for this plan

WiFi/MQTT wiring (covered by their own requirement docs, not started), and
any BLE peripheral/server role for a future phone app — noted above as an
architectural consideration only, not part of this port.

## Suggested order of work

Steps 0–3 are the minimum to prove the port works at all (connect, read
info, unlock/mode). Steps 4–8 can happen in any order after that depending
on what you want to see data from first. Step 9 (reconnect) is worth doing
before calling this "done," since every downstream use of this component
depends on it recovering from drops on its own.
