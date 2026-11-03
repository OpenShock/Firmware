# Serial protocol v2 (TODO)

> **Status: design only, nothing implemented.** This replaces the `$`-prefixed command / `$SYS$|...` response
> console in `components/serial_console`. Tick items off in the [checklist](#implementation-checklist) as they land.

## Why

The current console was built for a human at a terminal; tools are bolted on with the `$` prefix. For a tool:

- **No end of response.** A command prints zero, one or many lines (`sysinfo` ~9 `Response|` lines, `ledtest` a
  `Response|` and a `Success|` 24 s apart). `help`, `version`, `restart` and `factoryreset` print unframed text. Lines
  can also be dropped silently when the serial write lock times out (500 ms).
- **No correlation.** Responses carry no request id.
- **Inconsistent keys.** `rftxpin` answers `RmtPin|`, `estop` answers `EStopEnabled|`, `echo` answers `SerialEcho|`,
  `sysinfo` nests two levels (`RTOSInfo|Free Heap|...`).
- **Unescaped `|`.** SSIDs, JSON and echoed user input go in raw.
- **Free-text errors.** Tools have to match English sentences.
- **Help text on misuse.** A wrong argument count prints `printCommandHelp()`, unframed, even to a `$` caller.
  `isAutomated` reaches every handler but only controls echo.
- **Get/set by argument presence.** An empty value can't be set; multi-argument commands are impossible.
- **No handshake, discovery or events.** No protocol version, no "ready" line, no machine-readable command list, no
  pushed state changes.

## Design

One method registry, two thin front-ends over it: JSON-RPC 2.0 for tools and a text shell for humans.

### Transport

The port always carries text nobody can frame (ROM bootloader, panics, early-boot logs), so frames must be
recognisable on sight, with no mode to get out of sync across a reset.

- An input line starting with `{` is an RPC request; anything else goes to the shell.
- Every protocol output line starts with a fixed sentinel (`@`, or `\x1E` if `@` turns out to clash with log output —
  decide before implementing). Tools keep sentinel lines and treat the rest as log text.
- One frame is one `Serial::Write` call, so it never interleaves with a log line. Protocol frames must not be dropped by
  the write-lock timeout the way log lines may be: block or retry.
- No handshake to start. Every boot emits `event.ready`, so a tool knows the device restarted and that requests still
  in flight will never be answered.

### Wire format

Plain [JSON-RPC 2.0](https://www.jsonrpc.org/specification), one object per line, no batching.

```
→ {"jsonrpc":"2.0","id":1,"method":"config.set","params":{"statusLed":false}}
← @{"jsonrpc":"2.0","id":1,"result":{"changed":["statusLed"],"restartRequired":false}}

→ {"jsonrpc":"2.0","id":2,"method":"estop.configure","params":{"pin":99}}
← @{"jsonrpc":"2.0","id":2,"error":{"code":-32602,"message":"Invalid params","data":{"reason":"invalid_pin","field":"pin"}}}

← @{"jsonrpc":"2.0","method":"event.estop","params":{"active":true,"latched":true}}
← @{"jsonrpc":"2.0","method":"event.ready","params":{"proto":"2.0","fw":"2.0.0","board":"...","resetReason":"sw","bootId":"8f3a..."}}
```

- Every request with an `id` gets exactly one response.
- Errors use the standard JSON-RPC codes plus `data.reason`, a stable snake_case string (`estop_active`,
  `invalid_pin`, `save_failed`, `not_found`, ...). `message` is for humans and may change freely. `data.field` names
  the offending parameter where there is one.
- Progress and state changes are notifications. Long operations emit `event.progress` with `{"id": <request id>, ...}`
  and then the final response.

### Methods

| Method | Replaces |
|---|---|
| `rpc.discover` | `help` — an [OpenRPC](https://spec.open-rpc.org/) document: methods + JSON Schema for params and results |
| `sys.info` | `sysinfo`, `version` — one structured object |
| `sys.restart`, `sys.factoryReset` | `restart`, `factoryreset` — respond first, then act; reset requires `{"confirm":true}` |
| `config.get` `{keys?}` | getters of `statusled`, `keepalive`, `echo`, `hostname`, `domain`, `rftxpin`, `jsonconfig` |
| `config.set` `{partial object}` | the matching setters — patch semantics, validate every field before saving any, report `restartRequired` |
| `config.schema` | the hand-written constraint text in `addArgument` |
| `wifi.networks.list` / `wifi.networks.set` | `networks` |
| `wifi.status`, `wifi.scan` | the WiFi part of `sysinfo` |
| `rf.transmit` | `rftransmit` |
| `estop.status`, `estop.configure` | `estop` |
| `gpio.valid` | `validgpios` |
| `led.test` | `ledtest` — progress events, then the response |

Rules:

- **Secrets are write-only.** `config.get` returns `"authToken":{"set":true}`, never the token.
- **E-Stop semantics don't change.** `estop.configure` and E-Stop fields in `config.set` go through `EStopManager`
  exactly as the current handlers do, including refusing changes while the E-Stop is active.

### Human shell

A front-end that tokenises friendly syntax into the same method calls and pretty-prints the results:

```
> config set statusLed=false keepAlive=true
> config get
> wifi networks
> sys info
> help config.set        # generated from the same schema as rpc.discover
```

Echo, ANSI, paste detection and tab completion (now possible: method and parameter names are in the registry) live
only here and never touch protocol output.

### Firmware structure

```cpp
struct Method {
  std::string_view name;          // "config.set"
  std::string_view paramsSchema;  // JSON Schema literal, served by rpc.discover
  std::string_view resultSchema;
  uint8_t flags;                  // Restarts | Destructive | LongRunning
  Rpc::Status (*handler)(JSON::JsonView params, JSON::Writer& result, Rpc::Context& ctx);
};
inline constexpr Method kMethods[] = { ... };  // static table, nothing built at startup
```

- Handlers never print. They return `Status{reason, message, field}` and fill the result writer. `Rpc::Context` lets a
  handler emit progress events or defer an action until after the response is written (restart, factory reset).
- `RpcFrontend`: parse the line, dispatch, write one frame. `ShellFrontend`: tokenise, map to method + params,
  pretty-print. `SERPR_*`, `isAutomated` and the help-on-misuse paths go away.
- A small event publisher pushes E-Stop, WiFi and OTA state changes as notifications.
- The registry doesn't know about the transport, so the local websocket / captive portal can expose the same methods
  later.

### Versioning

- `event.ready` and `rpc.discover` carry `proto: "major.minor"`.
- New methods and fields are additive and bump minor; removing or changing anything bumps major.
- Tools check capabilities against `rpc.discover`, not firmware versions.

### Rejected

- **FlatBuffers / COBS binary framing.** Reuses the schemas codegen, but shows up as garbage in a plain serial monitor
  that humans also use, is harder to debug with `screen`/`pyserial`, and bandwidth doesn't matter here.
- **A machine mode entered by a handshake.** Goes out of sync after every reset or panic; a per-line sentinel is
  stateless.
- **One command per setting.** The root of the inconsistent keys and argument-presence get/set; typed
  `config.get`/`config.set` fixes both and removes most handlers.

## Migration

Keep a legacy front-end for one release: `$`-prefixed lines map to the new methods and render the old `$SYS$|...`
format, so existing tools keep working. Delete it in the next minor release. Update the frontend / flash tool to the new
protocol before the adapter goes.

## Implementation checklist

### Decisions
- [ ] Pick the output sentinel (`@` vs `\x1E`)
- [ ] Final error `reason` list
- [ ] Final `config.get`/`config.set` key names (one per config section owner, matching `docs/STORAGE.md`)

### Core
- [ ] `Rpc::Status`, `Rpc::Context`, `Method` and the static `kMethods` registry
- [ ] Serial: never-drop write path for protocol frames
- [ ] `RpcFrontend`: line parse, JSON-RPC validation (`-32700`, `-32600`, `-32601`, `-32602`), dispatch, one frame out
- [ ] Event publisher + `event.ready` on boot (proto, fw, board, reset reason, boot id)
- [ ] `event.progress` plumbing for long-running methods
- [ ] Deferred post-response actions (restart / factory reset)

### Methods
- [ ] `rpc.discover` (OpenRPC from the registry)
- [ ] `sys.info`, `sys.restart`, `sys.factoryReset`
- [ ] `config.get`, `config.set` (validate-all-then-save, `restartRequired`), `config.schema`
- [ ] Write-only secrets (`authToken`)
- [ ] `wifi.networks.list`, `wifi.networks.set`, `wifi.status`, `wifi.scan`
- [ ] `rf.transmit`
- [ ] `estop.status`, `estop.configure` (no change to `EStopManager` semantics)
- [ ] `gpio.valid`
- [ ] `led.test`
- [ ] Events: `event.estop`, `event.wifi`, `event.ota`

### Shell
- [ ] `ShellFrontend` tokeniser (`key=value`, quoting)
- [ ] Pretty-printers for results and errors
- [ ] `help` generated from the registry
- [ ] Tab completion
- [ ] Move echo / ANSI / paste detection out of the RX loop into the shell

### Migration & cleanup
- [ ] Legacy `$` front-end emitting `$SYS$|...`
- [ ] Frontend / flash tool switched to JSON-RPC
- [ ] Remove `SERPR_*`, `isAutomated`, `CommandGroup`/`CommandEntry`, the per-setting handlers
- [ ] Remove the legacy front-end
- [ ] `.changes/` entry
- [ ] Host-side test script (pyserial) covering every method and the error paths
