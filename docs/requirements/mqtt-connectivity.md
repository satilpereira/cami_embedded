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
| MQTT-3 | The device SHALL publish Myo-derived data (EMG/pose/IMU/battery — see [myo-armband-connectivity.md](myo-armband-connectivity.md)) under a defined topic namespace, e.g. `cami/<device_id>/myo/...`. |
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
- **Schema stability**: topic names and payload structure should be treated
  as a versioned contract (e.g. a `schema_version` field in JSON payloads)
  so firmware and downstream consumers can evolve independently.

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
- Exact topic and JSON payload schema — needs to be nailed down (possibly in
  a separate schema doc) before implementation starts on MQTT-3/MQTT-5.
- Should the status topic use retained messages so new subscribers
  immediately see current device state?
- Outgoing-message policy while disconnected (MQTT-7): drop, or buffer up to
  some bound?

## Out of scope

Cloud MQTT/IoT platform integration, TLS/mTLS and certificate provisioning,
MQTT 5 features, cross-reboot message persistence.
