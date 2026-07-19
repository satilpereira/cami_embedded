# Myo armband connectivity requirements

See [README.md](README.md) for how this fits into the overall system.

## Background

The Myo is a discontinued (Thalmic Labs) gesture armband with 8-channel EMG
and a 9-axis IMU. The official SDK/desktop software ("Myo Connect") is
deprecated and not relevant here — this is a from-scratch integration
talking directly to the armband's BLE GATT interface. The protocol is not
officially documented, but has been reverse-engineered by the community
(various open-source references exist for the service/characteristic UUIDs
and command bytes). **Every protocol detail in this doc needs to be verified
against the actual armband in hand before/during implementation** — treat
this as a requirements doc, not a protocol spec.

## Purpose

Connect to a Myo armband over BLE and extract EMG, IMU, pose, and battery
data for publishing via MQTT; accept a vibration command from MQTT and
relay it to the armband.

## Scope

- One Myo armband, one ESP32.
- EMG and/or onboard-classifier pose data, IMU orientation, battery level,
  vibration feedback.

Not in scope for v1: two-armband setups, Myo Connect/Windows SDK
compatibility, on-device ML/training (classification is either the
armband's built-in classifier or happens downstream, not on the ESP32).

## Assumptions

- One armband, charged, not paired to anything else via any OS-level Myo
  software — this integration connects directly over raw BLE GATT.
- Depends on [ble-connectivity.md](ble-connectivity.md) for the underlying
  central-role connection and reconnect behavior.
- Depends on [mqtt-connectivity.md](mqtt-connectivity.md) for how extracted
  data leaves the device and how the vibration command arrives.

## Functional requirements

| ID | Requirement |
|----|-------------|
| MYO-1 | The device SHALL identify and connect to a Myo armband via its BLE advertisement (known service UUID and/or device name prefix). |
| MYO-2 | The device SHALL put the armband into an active streaming mode via its control characteristic — Myo ships locked/sleeping by default and requires an explicit command before it streams EMG/IMU data. |
| MYO-3 | The device SHALL subscribe to EMG notifications and forward samples toward MQTT — with an explicit decision (see open questions) on whether raw 8-channel EMG streams as-is or gets downsampled/summarized on-device first. |
| MYO-4 | The device SHALL subscribe to IMU notifications (orientation quaternion, accelerometer, gyroscope). |
| MYO-5 | The device SHALL subscribe to the armband's onboard classifier/pose events (e.g. fist, wave in, wave out, fingers spread, double tap, rest) as an alternative or complement to raw EMG. |
| MYO-6 | The device SHALL read the armband's battery level periodically and include it in the status data published over MQTT. |
| MYO-7 | The device SHALL send a vibration command (short/medium/long pulse) to the armband when triggered by an incoming MQTT command (cross-ref mqtt-connectivity.md MQTT-5). |
| MYO-8 | On BLE reconnection after a drop, the device SHALL redo the unlock/streaming-mode setup and re-subscribe to notifications automatically (cross-ref ble-connectivity.md BLE-5) rather than staying silently un-streaming. |
| MYO-9 | The device SHALL suppress the armband's built-in inactivity auto-sleep while continuous streaming is required, via the appropriate control command. |

## Non-functional requirements

- **EMG throughput**: raw EMG is 8 channels at roughly 200 Hz — high volume
  relative to a WiFi+MQTT hop off a microcontroller. This is flagged as the
  single biggest open design question in this doc (see below) rather than
  assumed away.
- **Latency**: gesture/EMG-to-MQTT-publish latency should be low enough for
  whatever consumes it to act on in near-real-time, but no numeric target is
  set here — it depends on the downstream use case, which isn't defined yet
  (see [README.md](README.md) open cross-cutting question).
- **Armband battery life**: don't keep the armband awake/streaming (MYO-9)
  any more than the actual use case requires — it's a fixed, non-swappable
  battery.

## Interfaces / dependencies

- **Depends on**: [ble-connectivity.md](ble-connectivity.md) for the GATT
  client and connection lifecycle.
- **Provides**: structured EMG/IMU/pose/battery data to the MQTT publisher
  (mqtt-connectivity.md MQTT-3).
- **Consumes**: the vibration command from the MQTT subscriber
  (mqtt-connectivity.md MQTT-5).

## Acceptance criteria

- After a fresh BLE connection, the armband is unlocked and streaming within
  a few seconds without any manual action on the armband itself.
- While worn and in range, pose/EMG events (whichever mode is chosen)
  reliably reach the MQTT broker.
- A vibration command sent via MQTT produces a felt vibration on the
  armband within a short, noticeable delay.
- Battery level shows up in the device's published status and updates
  periodically.

## Open questions / risks

- **Raw EMG vs. onboard classifier**: streaming raw EMG is bandwidth-heavy
  and pushes processing downstream; relying on the onboard classifier is
  cheap but limited to Myo's fixed gesture set. This decision blocks
  MYO-3/MYO-5 implementation and the MQTT throughput NFR in
  mqtt-connectivity.md — needs to be made before implementation starts.
- **Exact GATT UUIDs / command bytes**: need to be confirmed against the
  specific armband firmware in hand; reverse-engineered references vary
  slightly across firmware versions.
- **Downstream consumer**: what actually acts on this data is still
  undefined (see README.md) — blocks setting real latency/throughput
  targets here.
- Does the armband require pairing/bonding, or is it open for GATT
  connections? Determines whether ble-connectivity.md BLE-4 is exercised.

## Out of scope

Multiple armbands, Myo Connect/Windows SDK compatibility, on-device
gesture-training/ML pipeline.
