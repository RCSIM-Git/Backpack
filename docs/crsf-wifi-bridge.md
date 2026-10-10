# CRSF Wi-Fi trainer bridge (experimental)

This opt-in TX Backpack service carries channel input from a PC over ordinary
Wi-Fi UDP and returns CRSF telemetry. It uses the existing ExpressLRS trainer
path: PC → Backpack → MSP trainer input → TX module → EdgeTX trainer/mixes →
normal ELRS RF → receiver. Receiver firmware and PWM behavior do not change.

This is a channel-input and telemetry bridge, not arbitrary bidirectional CRSF
configuration passthrough. The initial ingress supports only RC channels and
session release. Existing MAVLink, ESP-NOW and update services remain separate.

## Compatibility and configuration

The upstream trainer path requires ExpressLRS 4, TX Backpack 1.5.2 or later and
EdgeTX 2.11 or later. This Wi-Fi service additionally requires the modified
Backpack firmware; stock 1.5.9 does not accept these datagrams.

Keep the main TX in Normal link mode. Enable Backpack head tracking; select
`HT Start Channel: EdgeTX`. In EdgeTX select `Trainer: Master/CRSF` and map
trainer sources TR1…TR16 into the desired output channels. Configure a physical
switch to choose the PC input or local controls. Choose an RF switch mode that
actually carries the required output channels.

Build the modified Backpack with `CRSF_WIFI_CONTROL_ENABLED` explicitly defined
(disabled by default), and enable `Backpack → Telemetry: WiFi` in the TX Lua
menu. Join its access point or the same LAN. The bridge uses UDP port `8888`,
separate from MAVLink ports 14550/14555. There is no web configuration switch
for this experimental service.

The explicit build environments with this feature enabled are:
- ESP32-C3 (e.g. RadioMaster Nomad): `pio run -e ESP32C3_TX_Backpack_via_CRSF_WIFI`
- ESP8285 (e.g. RadioMaster Pocket / MT12 / TX16S internal TX): `pio run -e ESP_TX_Backpack_via_CRSF_WIFI`

Ordinary backpack environments keep this feature disabled.

RCSIM's existing raw CRSF UDP transport is not directly compatible with the
session envelope below. A client adapter or matching RCSIM change is required.

## Datagram format (CRWF version 1)

All multibyte integers are little endian. Each UDP datagram is one complete
message, with no trailing bytes.

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | ASCII `CRWF` |
| 4 | 1 | Version `1` |
| 5 | 1 | Kind: `1` RC input, `2` release, `3` telemetry |
| 6 | 2 | Reserved, zero |
| 8 | 4 | Nonzero session identifier |
| 12 | 4 | Sequence number |
| 16 | variable | Payload |

RC input contains exactly one 26-byte CRSF packed-channel frame: address `EA`
or `C8`, length `24`, type `16` hexadecimal, 22-byte packed sixteen-channel
payload, and CRC8 polynomial `D5` over type and payload. Total size is 42 bytes.
Channel values use raw 11-bit CRSF units, not microseconds.

Release has no payload (16 bytes total). Telemetry carries one complete,
CRC-valid CRSF frame after the header. It does not authorize remote commands.
Telemetry sequence numbers use a separate monotonically increasing counter.

The first valid RC packet claims ownership by source IP, source UDP port and
session identifier. Telemetry returns to that endpoint; clients must retain
the same socket. Valid fresh RC packets renew a 300 ms lease. Duplicate or
out-of-order sequence numbers do not renew it. Sequence freshness uses unsigned
32-bit modular subtraction: a delta from 1 through `7fffffff` is fresh.
Release must match the owner and have a fresh sequence number.
After release, lease expiry or disable, reconnect with a new session token.
The last sixteen retired tokens are rejected, preventing recent session
replay within this bounded history. This history is not persistent or unlimited
replay protection: reboot clears it and newer retired tokens can evict older
ones. The reference client creates a new token on each run.

A session identifier prevents accidental cross-client mixing; it is not an
authentication credential. Use a trusted network. There is no claim that this
prototype provides cryptographic authentication.

Backpack must also see enabled trainer input from the TX. A client lease does
not replace physical authorization. The existing TX trainer timeout is one
second, so a Backpack lease expiry alone must not be described as a 300 ms
end-to-end failsafe. A conservative budget can reach roughly 1.3 seconds for
the lease and TX stale interval, before considering EdgeTX mixing and scheduling.
There is no sender timestamp or packet age check; delayed but increasing
sequence numbers can still be accepted. This is not a complete guarantee against
stale input under network congestion: the bounded receive loop can process
queued old controls. A physical trainer-enable transition revokes the owner,
but actual output fallback is governed by the trainer path. Confirm the actual local-control fallback
and EdgeTX mixing behavior during hardware review.

## Reference client and validation

Run `python tools/crsf_wifi_client.py` for offline checks; it sends no packets.
The client validates envelope lengths, version, reserved bits and CRSF CRC and
creates sixteen packed channels. Network transmission requires explicit
`--send IP`; it defaults to fifty channel frames per second for two seconds,
then sends a release. The optional `--channels` accepts sixteen raw CRSF values.

Neutral channels are not an assurance that an ESC will remain stopped. Hardware
bench testing requires a suitable model setup and a tested physical takeover.
No hardware test or firmware flash is implied by passing software checks.

Verify on hardware: trainer sources reach EdgeTX, configured mixes reach PWM,
telemetry returns to the same UDP socket, local takeover works, and loss of
Wi-Fi/PC/Backpack returns controls according to the configured policy. Also
verify duplicate, reordered and malformed datagrams cannot retain ownership.
