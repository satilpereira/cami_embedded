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
- **MQTT wire format**: fixed binary packets, one struct per topic (one
  topic per Myo data type: EMG/IMU/gesture/battery) — not JSON, not TLV,
  not bit-packed. See mqtt-connectivity.md's "Wire format decision" for the
  full rationale.
- **Doc depth**: lightweight PRD-style (purpose, requirements, acceptance
  criteria) rather than a fully implementation-ready spec. Concrete details
  that need hands-on verification against real hardware (Myo GATT UUIDs,
  exact MQTT topic strings, etc.) are flagged as open questions rather than
  guessed.

## Current hardware / project state (updated)

- Board in hand: **ESP32 (classic)**, 4 MB flash, single 2.4 GHz radio shared
  between WiFi and Bluetooth (time-multiplexed coexistence, not two radios).
  Only 1 MB of the 4 MB is actually claimed by `firmware/partitions.csv`
  (see below) — the rest is currently unpartitioned/unused.
- BLE connectivity and the Myo armband integration (this doc's `ble-` and
  `myo-armband-` requirements) are **implemented and verified against real
  hardware** — NimBLE host stack (not Bluedroid), all 9 steps of
  [docs/plans/myo-library-migration.md](../plans/myo-library-migration.md)
  complete. Full technical writeup:
  [docs/technical/firmware-implementation.md](../technical/firmware-implementation.md)
  and [docs/technical/myo-protocol-reference.md](../technical/myo-protocol-reference.md).
  WiFi and MQTT (this doc's other two requirements) are **not yet
  implemented** — `firmware/app/app_main.cpp` is currently a BLE-only
  validation harness with no network code.
- `firmware/partitions.csv`: single 1 MB `factory` app partition, **no OTA
  slot**. Firmware updates require a serial reflash. With the full BLE/NimBLE
  stack linked in, the image uses ~54% of that 1 MB — comfortable for now,
  but still worth watching once WiFi + MQTT (+ TLS, if ever added) stack on
  top. Resizing to use more of the physical 4 MB is straightforward if
  needed later.
- Only the `esp32` board target is wired up in hardware; `esp32s3` is stubbed
  for later and out of scope for these docs.

## Open cross-cutting question (partially resolved)

Originally: "the downstream consumer of the MQTT data is not yet defined."
That's now partially answered:

- **Near-term**: a Python script subscribing to MQTT directly, for
  ad-hoc/manual analysis of the Myo data stream. No fixed schema or
  latency target implied by this — it's an exploratory consumer.
- **Longer-term (planned, not yet built)**: a **second ESP32** dedicated to
  signal processing, splitting the system into two boards — this one stays
  BLE-only (Myo central role, per `ble-` and `myo-armband-connectivity.md`),
  and the second board does the processing/interpretation work, presumably
  consuming the same data this board would publish to MQTT rather than
  doing EMG/IMU processing on this board itself.

Still open: the exact division of responsibility between the two boards
(e.g. does the Myo-side board publish raw EMG/IMU, or something
pre-processed?), the MQTT topic/payload schema, and real latency/throughput
targets — none of that is pinned down yet, so the requirements below still
treat EMG throughput and downsampling policy as open questions rather than
assuming an answer.
