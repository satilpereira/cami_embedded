# WiFi connectivity requirements

See [README.md](README.md) for how this fits into the overall system.

## Purpose

Give the ESP32 a station-mode WiFi connection to the local network, so it has
IP connectivity for the MQTT client ([mqtt-connectivity.md](mqtt-connectivity.md)).

## Scope

- Station (STA) mode only, WPA2-PSK, single configured access point.
- DHCP for IP assignment.

Not in scope for v1: SoftAP/provisioning mode, static IP, enterprise auth
(WPA2-Enterprise), mesh, multiple/fallback SSIDs.

## Assumptions

- Credentials (SSID + password) are configured at build time via
  `idf.py menuconfig` (Kconfig), not hardcoded in source, and not committed to
  git in plaintext. This mirrors the existing per-board
  `firmware/sdkconfig.defaults.<board>` pattern for non-secret settings.
- Only the `esp32` board target needs this today (per the project README).
- The radio is shared with BLE (see [ble-connectivity.md](ble-connectivity.md)) —
  WiFi requirements here assume BT/WiFi coexistence is enabled, not two
  independent radios.

## Functional requirements

| ID | Requirement |
|----|-------------|
| WIFI-1 | On boot, the device SHALL attempt to connect to the configured SSID using WPA2-PSK credentials. |
| WIFI-2 | SSID and password SHALL be configurable via Kconfig (`idf.py menuconfig`), stored outside of tracked source files. |
| WIFI-3 | On connection failure, the device SHALL retry with backoff (capped, not a tight infinite-retry loop) rather than giving up permanently. |
| WIFI-4 | The device SHALL log connection state transitions (connecting / connected + IP / disconnected / reconnecting) at INFO level. |
| WIFI-5 | On disconnect (AP reboot, RF loss, etc.), the device SHALL reconnect automatically without requiring a power cycle. |
| WIFI-6 | The device SHALL expose its current connection state to other components (e.g. via an event group or callback) so the MQTT component can gate connection attempts on "WiFi up." |
| WIFI-7 | The device SHALL obtain an IP address via DHCP. |

## Non-functional requirements

- **Reconnect time**: no hard target yet, but reconnection after a transient
  AP outage should be automatic and complete within a reasonable time (low
  tens of seconds) without manual intervention.
- **Secrets hygiene**: WiFi credentials must never land in a committed file.
  If Kconfig alone isn't enough to keep them out of `sdkconfig` diffs, this
  needs a documented workaround (e.g. a gitignored `sdkconfig.local` or
  build-time env var) before implementation.
- **Coexistence**: WiFi and BLE run on the same radio on ESP32 classic. This
  doc doesn't set a specific throughput/latency budget, but implementation
  should validate that simultaneous MQTT publish traffic and BLE Myo
  notification handling don't starve each other (cross-ref
  [ble-connectivity.md](ble-connectivity.md) BLE-7).

## Interfaces / dependencies

- **Depends on**: NVS partition (already present in `firmware/partitions.csv`)
  for WiFi driver state/calibration data.
- **Provides**: a "network up" signal consumed by the MQTT component.

## Acceptance criteria

- Device connects to the configured AP within a reasonable time after boot
  under normal conditions (AP in range, correct credentials).
- Power-cycling the AP does not require power-cycling the device — it
  reconnects on its own once the AP is back.
- `grep`-ing the git history/tracked files for the WiFi password turns up
  nothing.

## Open questions

- Static IP support — needed, or is DHCP sufficient indefinitely?
- Fallback to a second SSID (e.g. phone hotspot) if the primary AP is down?
- If the hardcoded-credentials approach becomes limiting (e.g. device needs
  to move between networks without a reflash), runtime provisioning
  (BLE or SoftAP) is the documented fallback design, not built for v1.

## Out of scope

SoftAP provisioning, WiFi mesh, WPA2-Enterprise, OTA firmware updates over
WiFi (there is currently no OTA partition in `partitions.csv`).
