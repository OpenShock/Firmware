# mock-portal

A Go HTTP server that simulates the OpenShock ESP32 captive portal for frontend development and testing. It implements the same HTTP API and WebSocket protocol as the real firmware, with an interactive CLI for injecting network events and faults — without requiring real hardware, DNS, DHCP, or file hosting.

## Contents

- [Running](#running)
- [Flags](#flags)
- [Endpoints](#endpoints)
- [WebSocket Protocol](#websocket-protocol)
- [Interactive CLI](#interactive-cli)
- [Chaos Mode](#chaos-mode)
- [Config Persistence](#config-persistence)
- [Regenerating FlatBuffers Bindings](#regenerating-flatbuffers-bindings)
- [Dependencies](#dependencies)

---

## Running

```bash
cd tools/mock-portal
go run . [flags]
```

Or build a binary first:

```bash
go build -o mock-portal .
./mock-portal [flags]
```

## Flags

| Flag | Default | Description |
|---|---|---|
| `--addr string` | `:8080` | Listen address |
| `--config string` | `mock-config.bin` | FlatBuffers `HubConfig` state file — loaded on startup, saved on every mutation |
| `--chaos` | false | Start with chaos mode enabled |

## Endpoints

| Method | Path | Description |
|---|---|---|
| `GET` | `/captive-portal/api` | RFC 8908 captive portal JSON response |
| `GET` | `/ws` | WebSocket upgrade; subprotocol `flatbuffers` |

## WebSocket Protocol

All messages are FlatBuffers binary, matching the firmware's `LocalToHub` / `HubToLocal` message schemas. JSON is not used.

### On connect

The server immediately sends two messages to every new client:

1. A `ReadyMessage` containing full device state — firmware version, boot type, OTA config, RF config, EStop config, and account link status.
2. A `WifiNetworkEvent(Discovered)` listing all currently known networks.

### Supported incoming commands (`LocalToHub`)

**WiFi**
- `WifiScanCommand`
- `WifiNetworkSaveCommand`
- `WifiNetworkForgetCommand`
- `WifiNetworkConnectCommand`
- `WifiNetworkDisconnectCommand`

**OTA**
- `OtaUpdateSetIsEnabledCommand`
- `OtaUpdateSetDomainCommand`
- `OtaUpdateSetCheckIntervalCommand`
- `OtaUpdateSetAllowBackendManagementCommand`
- `OtaUpdateSetRequireManualApprovalCommand`
- `OtaUpdateSetUpdateChannelCommand`
- `OtaUpdateCheckForUpdatesCommand`
- `OtaUpdateStartUpdateCommand`
- `OtaUpdateHandleUpdateRequestCommand`

**RF**
- `SetRfTxPinCommand`

**EStop**
- `SetEstopEnabledCommand`
- `SetEstopPinCommand`

**Account**
- `AccountLinkCommand`
- `AccountUnlinkCommand`

---

## Interactive CLI

The server accepts commands on stdin at runtime. Type `help` for a full list.

### Network management

```
add-networks [n]              Add n random networks (default 10); broadcasts Discovered
set-networks <n>              Replace ALL networks with n fresh random ones
remove-network <ssid>         Remove network by SSID; broadcasts Lost
list-networks                 Print a tabular list of all current networks
clear-networks                Remove all networks; broadcasts Lost for each
network add <ssid> [opts]     Add a specific network
                                opts: rssi=<dBm>  channel=<n>  auth=<mode>  saved  hidden
network update <ssid> [opts]  Update network properties in-place; broadcasts Updated
network rssi <ssid> <dBm>     Set RSSI; broadcasts Updated
network auth <ssid> <mode>    Set auth mode; broadcasts Updated
network save <ssid>           Mark network as having a saved credential; broadcasts Saved
network forget <ssid>         Remove saved credential; broadcasts Removed
mesh <ssid> [count]           Add count APs sharing an SSID with spread channels/RSSI (default 3)
mesh-roam <ssid>              Shuffle RSSI among mesh nodes to simulate a roam event
```

### WiFi events

```
wifi-lost                     Simulate IP loss + Disconnected event
wifi-got <ssid>               Simulate IP assignment + Connected event
scan                          Trigger a full scan cycle broadcast to all clients
```

### Fault injection

```
error <message>               Broadcast an ErrorMessage to all clients
disconnect                    Close all active WebSocket connections
spike [n]                     Flood n rapid WifiNetworkEvents (default 100)
corrupt [n]                   Send n malformed binary frames (default 1)
scan-fail [status]            Make the next scan return an error status
                                status: Error | TimedOut | Aborted (default Error)
chaos [on|off]                Toggle automatic random fault injection
```

### Other

```
status                        Dump a summary of current server state
clients                       List all connected WebSocket clients
help                          Show all commands
```

### Random network generation

On startup, the server generates 40 random networks using [gofakeit](https://github.com/brianvoe/gofakeit). SSIDs cover a wide range of edge cases useful for frontend testing:

- Normal names, emoji, and Unicode
- XSS payloads and SQL injection strings
- Path traversal sequences and null bytes
- Very long names and whitespace-only strings
- All auth modes: Open, WEP, WPA, WPA2, WPA3, Enterprise, and hidden networks
- Channels spread across 2.4 GHz (1–13) and 5 GHz (36–165)

---

## Chaos Mode

Enable with `--chaos` at startup or with the `chaos on` CLI command. When active, a random fault fires every 2–10 seconds. Possible actions:

1. Disconnect all clients
2. Broadcast a random error message
3. Traffic spike (20–100 events)
4. Send corrupt frames (1–3)
5. Arm a scan failure for the next scan
6. Add 3–13 new random networks
7. Remove a random network
8. Flutter RSSI on a random network
9. Simulate an IP flap (lost then regained)
10. Burst scan status transitions

Chaos mode is useful for stress-testing client-side state machines and UI resilience.

---

## Config Persistence

Device state is stored in memory and serialized as a FlatBuffers `HubConfig` binary to the path given by `--config` (default `mock-config.bin`). The file is written on every mutation and loaded on startup if it exists.

To reset all state to defaults, delete the file:

```bash
rm mock-config.bin
```

---

## Regenerating FlatBuffers Bindings

The Go bindings under `fbs/` are generated from the root-level `schemas/` submodule. Run from the repository root:

```bash
python3 scripts/generate_schemas.py
```

This regenerates `fbs/**/*.go` alongside the TypeScript and C++ bindings used by the rest of the firmware. Requires `scripts/flatc` to be executable.

---

## Dependencies

| Package | Purpose |
|---|---|
| `github.com/google/flatbuffers/go` | FlatBuffers runtime |
| `github.com/gorilla/websocket` | WebSocket server |
| `github.com/brianvoe/gofakeit/v6` | Random data generation |
