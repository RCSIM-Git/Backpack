# Add experimental opt-in CRSF Wi-Fi trainer input and telemetry

PC applications cannot currently send packed CRSF channel input to a TX
Backpack over ordinary Wi-Fi UDP. This prototype adds a separate, build-time
opt-in UDP service that converts validated channel frames into the existing
MSP trainer-input message and returns CRSF telemetry to the active client.

The main ELRS module stays in Normal mode. Channels flow through its existing
EdgeTX trainer route: configure ExpressLRS 4 with `HT Start Channel: EdgeTX`,
and EdgeTX 2.11 or later with `Master/CRSF` trainer mode and appropriate mixes.
No receiver firmware change, custom RF protocol or direct PWM override is added.

## Behavior

- New ESP32-C3 environment `ESP32C3_TX_Backpack_via_CRSF_WIFI` enables UDP port 8888;
  the standard environment leaves the service disabled.
- CRWF version 1 wraps one complete CRC-valid CRSF frame with a session and
  sequence number. Ingress is limited to packed RC channels and release.
- One source IP/port/session owns a 300 ms renewable lease. Invalid, duplicate,
  reordered and foreign-client packets cannot renew it. Recent retired tokens
  are rejected using a bounded sixteen-token history.
- Trainer enable from the main TX gates forwarding. Telemetry returns to the
  same client socket. Existing telemetry services remain available.
- Includes protocol documentation, native checks and a stdlib reference client
  whose default invocation performs offline checks without sending packets.

## Validation

The standard `ESP32C3_TX_Backpack_via_UART` and opt-in
`ESP32C3_TX_Backpack_via_CRSF_WIFI` PlatformIO builds passed after review fixes.
The Python reference client's offline checks passed. The production C++ session
parser harness passed unit testing for decoding, CRC/length rejection, endpoint
ownership, duplicate sequences, wraparound, release, expiry, and retired-token rejection.

**Hardware bench validation:**
- Flashed and tested on physical RadioMaster Nomad TX Backpack (ESP32-C3).
- Verified with RadioMaster Pocket / MT12 running EdgeTX 2.11+ (`Master/CRSF` trainer mode) and RadioMaster ER5C V2 receiver (PWM servo output).
- Verified full end-to-end control path over Wi-Fi UDP port 8888 using `CRWF` v1 datagrams.
- Local physical switch takeover in EdgeTX confirmed — toggling the switch instantly yields control back to local radio inputs.
- Required a radio reboot after enabling Lua settings (`Telemetry: WiFi`, `HT Start Channel: EdgeTX`) for the main TX to bring up the Backpack in Wi-Fi telemetry service mode.

## Technical considerations and limitations

- **Failsafe budget:** The existing EdgeTX trainer stale timeout is approximately 1 second; the 300 ms Backpack lease terminates forwarding, but the full pipeline budget before EdgeTX mixer fallback can take up to ~1.3 seconds.
- **Network security:** Session tokens and bounded retired history prevent accidental cross-client collisions and reordered packets, but are not cryptographic authentication. The service is intended for trusted networks and remains opt-in / disabled by default.
- **Ingress scope:** The service strictly accepts packed RC channel frames (type 0x16) and session release datagrams; it does not accept arbitrary CRSF configuration commands.
- Protocol review and maintainer feedback on the wire format are welcome.
