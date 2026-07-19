# BLE connectivity requirements

See [README.md](README.md) for how this fits into the overall system.

This doc covers the **generic BLE central-role capability**. Myo-specific
protocol details (service/characteristic UUIDs, command bytes, streaming
modes) live in [myo-armband-connectivity.md](myo-armband-connectivity.md).

## Purpose

Give the ESP32 a BLE central role capable of discovering, connecting to, and
exchanging data with one BLE peripheral — the Myo armband, in v1.

## Scope

- BLE GAP central role + GATT client role.
- One simultaneous peripheral connection (the Myo armband).

Not in scope for v1: GATT server/peripheral role (nothing pairs *to* the
ESP32, e.g. no phone app talks to it directly over BLE), multiple
simultaneous peripheral connections, BLE mesh, Bluetooth Classic.

## Assumptions / current state

- `firmware/sdkconfig` currently has `CONFIG_BT_ENABLED` **unset** — enabling
  the BT/BLE stack and picking a host stack is a prerequisite piece of work,
  not yet done. NimBLE is the likely choice (smaller flash/RAM footprint than
  Bluedroid) unless something Myo-specific rules it out — see open questions.
- Only the `esp32` (classic) board is wired up in hardware today. It has a
  single 2.4 GHz radio shared with WiFi (time-multiplexed coexistence), not
  two independent radios.

## Functional requirements

| ID | Requirement |
|----|-------------|
| BLE-1 | The device SHALL initialize the BLE stack in central (GAP central + GATT client) role at boot. |
| BLE-2 | The device SHALL scan for BLE peripherals matching a configured target (name prefix and/or service UUID) to find the Myo armband. |
| BLE-3 | The device SHALL connect to a discovered peripheral by address, perform GATT service/characteristic discovery, and read/write characteristics and subscribe to notifications. |
| BLE-4 | The device SHALL support bonding/pairing if the peripheral requires it, persisting bond info in NVS so reconnects don't require re-pairing. (Confirm in the Myo doc whether the armband actually requires this — historically it does not for basic operation.) |
| BLE-5 | The device SHALL detect disconnection and automatically resume scanning/reconnecting without manual intervention. |
| BLE-6 | The device SHALL expose its connection state (scanning / connecting / connected / disconnected) to other components, e.g. so MQTT can publish it as part of device status. |
| BLE-7 | WiFi and BLE SHALL operate concurrently without one component starving the other — coexistence must be enabled and validated, not just left at defaults. |

## Non-functional requirements

- **Footprint**: enabling BT/BLE increases binary size. The current
  `firmware/partitions.csv` has a single 1 MB `factory` app partition with no
  OTA slot — flash size is a real constraint to watch once WiFi + MQTT + BLE
  are all linked into one image, not an abstract concern.
- **Reconnect robustness**: losing and regaining BLE range (armband taken off,
  walked out of range, etc.) should recover on its own once the peripheral is
  back in range, without a device reboot.
- **Stack choice**: prefer NimBLE for footprint unless a Myo-specific
  requirement forces Bluedroid (see myo doc open questions).

## Interfaces / dependencies

- **Depends on**: `CONFIG_BT_ENABLED` and a selected BLE host stack being
  enabled in Kconfig (not yet done); WiFi/BT coexistence configuration
  (cross-ref [wifi-connectivity.md](wifi-connectivity.md)).
- **Provides**: a connected GATT client handle/API surface that the Myo
  component builds on.

## Acceptance criteria

- Device discovers and connects to the Myo armband when it's powered on and
  in range, without any manual pairing step beyond whatever the armband
  itself requires.
- A brief signal loss (walking a few meters away and back) results in
  automatic reconnection, not a stuck/disconnected state.
- Running BLE scanning/notifications and WiFi MQTT publishing at the same
  time doesn't cause one to starve the other for extended periods —
  validated empirically once both are implemented, not just assumed.

## Open questions

- NimBLE vs. Bluedroid — confirm NimBLE works for whatever GATT operations
  the Myo integration ends up needing before committing.
- Is bonding/pairing actually required by the Myo armband, or is it fully
  open for GATT connections? Affects whether BLE-4 is needed at all.
- Flash size: does adding BT push the image close to the 1 MB partition
  limit? If so, `partitions.csv` may need revisiting (currently sized for
  "single factory app, no OTA" per the top-level README).

## Out of scope

GATT server/peripheral role, multiple simultaneous BLE peripherals, BLE
mesh, Bluetooth Classic (BR/EDR).
