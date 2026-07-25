# MQTT connectivity requirements

See [README.md](README.md) for how this fits into the overall system.

## Purpose

Publish Myo-armband-derived data and device status to a local MQTT broker
over WiFi, and receive commands (e.g. trigger armband vibration) from that
broker.

## Scope

- MQTT client (`esp-mqtt`) connecting to a **local/self-hosted broker**
  (e.g. Mosquitto on the LAN), not a cloud IoT platform, for v1.
- QoS 0/1 publish and subscribe.

Not in scope for v1: TLS/mTLS, cloud broker integration, MQTT 5-specific
features, message persistence/store-and-forward across reboots.

## Wire format decision

Payloads are **fixed binary packets, one struct per topic** — not JSON, not
TLV, not a bit-packed format. Each Myo data type (EMG, IMU, gesture,
battery) gets its own topic (see MQTT-3), and each topic carries a fixed,
densely-packed struct specific to that data type (the existing
`myohw_emg_data_t`/`myohw_imu_data_t`/`myohw_classifier_event_t` structs
from [myo-armband-connectivity.md](myo-armband-connectivity.md) are the
natural starting point, since the data already arrives off the Myo in
exactly that shape). Rationale:

- **Fixed binary over TLV**: TLV earns its keep when one stream carries a
  *mix* of message types and needs to stay self-describing as it evolves —
  MQTT already gives you that for free via topics. Splitting by topic makes
  a type tag redundant; TLV would only make sense if everything were
  multiplexed onto a single topic/stream (e.g. anticipating a future
  non-MQTT transport like UART or ESP-NOW between two boards, where
  topic-multiplexing isn't free — not the current plan, see
  [README.md](README.md)).
- **Fixed binary over bit-packing**: EMG channels are already `int8_t` (the
  natural minimum width), and a local Mosquitto broker over WiFi has far
  more headroom than a ~200 Hz x 8-channel stream needs (~1.6 KB/s) —
  bit-packing would add real complexity for a bandwidth problem that
  doesn't exist here.
- **Fixed binary over JSON**: zero serialization cost on the ESP32 (raw
  struct bytes out), trivial `struct.unpack`-style parsing on any consumer
  (the near-term Python script, and later the second ESP32 — see
  [README.md](README.md)), no text-encoding overhead on a stream that's
  already bandwidth-conscious.

Not yet decided: exact topic names, whether each packet gets a
timestamp/sequence number prepended (useful for the Python-side analysis
and for detecting dropped notifications), and formal versioning of the
struct layouts as they evolve (see Non-functional requirements below).

## Assumptions

- Broker host/port is reachable on the local network the device is on
  (see [wifi-connectivity.md](wifi-connectivity.md)); connection is only
  attempted once WiFi is up (WIFI-6).
- Broker auth (username/password) may or may not be required depending on
  how the local broker is configured — treat as configurable, not assumed
  either way.
- No cloud account, device cert provisioning, or internet egress is required
  for MQTT in v1, per the "local/self-hosted broker" decision.

## Functional requirements

| ID | Requirement |
|----|-------------|
| MQTT-1 | The device SHALL connect to a configured MQTT broker (host + port) once WiFi is connected. |
| MQTT-2 | The device SHALL use a stable, unique client ID (e.g. derived from its MAC address) to avoid session collisions on the broker. |
| MQTT-3 | The device SHALL publish each Myo-derived data type (EMG/pose/IMU/battery — see [myo-armband-connectivity.md](myo-armband-connectivity.md)) as a fixed binary struct on its own topic under a defined namespace, e.g. `cami/<device_id>/myo/emg`, `.../imu`, `.../gesture`, `.../battery` (see Wire format decision above). |
| MQTT-4 | The device SHALL publish its own status (online/offline, and WiFi/BLE/Myo link state) to a status topic, using MQTT Last Will and Testament so it's marked offline automatically on an ungraceful disconnect. |
| MQTT-5 | The device SHALL subscribe to a command topic (e.g. `cami/<device_id>/cmd/...`) to receive commands, at minimum "trigger armband vibration." |
| MQTT-6 | The device SHALL reconnect to the broker automatically on connection loss, independent of (but coordinated with) WiFi-level reconnect logic. |
| MQTT-7 | The device SHALL have a defined, non-crashing behavior for outgoing messages while disconnected from the broker (drop with a log, or a small bounded buffer — exact policy TBD, see open questions). |

## Non-functional requirements

- **Throughput**: publish rate/volume must stay within what the broker and
  local network can handle. Raw Myo EMG streaming (see myo doc, MYO-3) is
  the main risk here — this doc doesn't set a number because the on-device
  vs. off-device processing decision isn't made yet.
- **Reconnect backoff**: reconnect attempts should back off rather than
  hammering the broker in a tight loop.
- **Schema stability**: topic names and struct layouts should be treated as
  a versioned contract so firmware and downstream consumers (Python script
  now, second ESP32 later) can evolve independently — exact versioning
  mechanism (e.g. a version byte per struct) not yet decided, see Wire
  format decision above.

## Interfaces / dependencies

- **Depends on**: [wifi-connectivity.md](wifi-connectivity.md) for network
  reachability.
- **Depends on**: [myo-armband-connectivity.md](myo-armband-connectivity.md) /
  [ble-connectivity.md](ble-connectivity.md) as the source of the data being
  published and the target of the vibration command.
- **Provides**: the only data egress/ingress path off the device in v1 — no
  other network protocol is planned.

## Acceptance criteria

- Device's status topic shows it "online" within a reasonable time of the
  broker becoming reachable, and flips to "offline" (via LWT) if the device
  loses power or network without a clean disconnect.
- Publishing a vibration command on the command topic produces a felt
  vibration on the armband within a short, noticeable delay.
- Restarting the broker process does not require restarting the device for
  publishing to resume.

## Open questions

- TLS for MQTT even on the LAN — worth it for a personal project, or
  deferred entirely?
- Broker address: static config value, or LAN discovery (mDNS)?
- Exact topic names and per-struct versioning (see Wire format decision) —
  format style (fixed binary, struct-per-topic) is decided; exact names and
  whether to prepend a timestamp/sequence number are not yet.
- Should the status topic use retained messages so new subscribers
  immediately see current device state?
- Outgoing-message policy while disconnected (MQTT-7): drop, or buffer up to
  some bound?

## Out of scope

Cloud MQTT/IoT platform integration, TLS/mTLS and certificate provisioning,
MQTT 5 features, cross-reboot message persistence.
