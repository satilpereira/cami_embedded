# cami_embedded requirements

Requirements for the four connectivity capabilities being built on top of the
current blink-LED skeleton (`firmware/app/app_main.cpp`). Each capability has
its own doc; this page just captures how they fit together and the decisions
already made so the individual docs don't repeat them.

## System data flow (v1)

```
Myo armband  --BLE (GATT)-->  ESP32                    ESP32  --WiFi-->  local network  --MQTT-->  Mosquitto broker
  (EMG/IMU/       BLE            |  BLE central role       |  MQTT client
   pose/battery)  central   -----+  publishes Myo data -----+  publishes / subscribes
                                 |  subscribes to commands   |
                                 +-- vibration cmd <---------+-- command topic
```

The ESP32 is a **bridge**: it is the sole BLE central for one Myo armband,
and the sole MQTT client publishing that armband's data to a local broker.
Commands (e.g. trigger vibration feedback) flow the other way: MQTT command
topic -> ESP32 -> Myo BLE write.

Read order / dependency chain:

1. [wifi-connectivity.md](wifi-connectivity.md) and [ble-connectivity.md](ble-connectivity.md) are the two independent transport layers.
2. [mqtt-connectivity.md](mqtt-connectivity.md) builds on WiFi.
3. [myo-armband-connectivity.md](myo-armband-connectivity.md) builds on BLE, and its data/commands flow through MQTT.

## Decisions already made

- **WiFi credentials**: hardcoded via `idf.py menuconfig` (Kconfig), not
  runtime-provisioned. Matches the existing per-board `sdkconfig.defaults.<board>`
  pattern. See wifi-connectivity.md.
- **MQTT broker**: local/self-hosted (e.g. Mosquitto on the LAN), not a cloud
  IoT platform, for v1. See mqtt-connectivity.md.
- **Doc depth**: lightweight PRD-style (purpose, requirements, acceptance
  criteria) rather than a fully implementation-ready spec. Concrete details
  that need hands-on verification against real hardware (Myo GATT UUIDs,
  exact MQTT topic strings, etc.) are flagged as open questions rather than
  guessed.

## Current hardware / project state (as of writing)

- Board in hand: **ESP32 (classic)**, 4 MB flash, single 2.4 GHz radio shared
  between WiFi and Bluetooth (time-multiplexed coexistence, not two radios).
- `firmware/sdkconfig`: `CONFIG_BT_ENABLED` is **not** currently set — enabling
  the BT/BLE stack is a prerequisite piece of work, not yet done.
- `firmware/partitions.csv`: single 1 MB `factory` app partition, **no OTA
  slot**. Firmware updates require a serial reflash. Adding WiFi + MQTT + BLE
  + TLS (if ever needed) to one 1 MB image is a real size risk worth
  tracking once implementation starts.
- Only the `esp32` board target is wired up in hardware; `esp32s3` is stubbed
  for later and out of scope for these docs.

## Open cross-cutting question

The downstream consumer of the MQTT data (what actually subscribes to
`cami/.../myo/...` and does something with it) is not yet defined. Several
requirements below (latency targets, EMG throughput/downsampling policy)
can't be pinned down precisely until that's known — they're marked as open
questions in the relevant doc rather than assumed.
