# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

OpenShock ESP32 firmware — controls shock collars via reverse-engineered 433 MHz sub-1 GHz protocols. Dual-component project: C++20 embedded firmware + SvelteKit web frontend (served from LittleFS).

## Build Commands

```bash
# Install Python build dependencies
pip install -r requirements.txt

# Build firmware for a specific board
pio run -e Wemos-D1-Mini-ESP32

# Build all board variants
pio run

# Upload firmware to connected board
pio run -e Wemos-D1-Mini-ESP32 -t upload

# Upload LittleFS filesystem (frontend) to connected board
pio run -e Wemos-D1-Mini-ESP32 -t uploadfs

# Serial monitor (115200 baud)
pio device monitor

# Static analysis
pio check -e ci-build

# Regenerate FlatBuffers headers from schemas/ submodule
python scripts/generate_schemas.py

# Frontend (from frontend/ directory)
pnpm install
pnpm run build          # Production build (single-file HTML)
pnpm run lint           # Prettier + ESLint
pnpm run check          # Svelte type checking (svelte-check)
```

Board environments are defined in `platformio.ini`. Common ones: `Wemos-D1-Mini-ESP32`, `Wemos-Lolin-S3`, `OpenShock-Core-V2`, `Seeed-Xiao-ESP32S3`, `ci-build` (for analysis).

## Code Style

**C++**: clang-format (`.clang-format`) — 2-space indent, 256 column limit, C++20, LF line endings. Functions use Allman braces; control statements use K&R (multi-line only). Pointers/references left-aligned (`int* p`).

**Frontend**: Prettier (`.prettierrc`) — 2-space indent, semicolons, trailing commas.

**Python**: Black with `--line-length 120 --skip-string-normalization`.

## Architecture

### Initialization Chain (`src/main.cpp`)
```
setup() → Config::Init() → Events::Init() → OtaUpdateManager::Init()
        → trySetup():
            VisualStateManager → EStopManager → SerialInputHandler
            → CommandHandler → WiFiManager → GatewayConnectionManager
            → CaptivePortal
loop() → spawns FreeRTOS task "main_app" → deletes Arduino loop task
main_app() → continuous GatewayConnectionManager::Update()
```

On OTA boot: same init but rolls back on failure. On normal boot: restarts after 5s on failure.

### Subsystem Pattern
Each major subsystem is a static namespace under `OpenShock::` with `Init()` / `Update()` functions. No class instances — singletons via file-static variables. Example: `OpenShock::WiFiManager::Init()`, `OpenShock::Config::GetRFConfig()`.

Source files define `const char* const TAG = "ModuleName";` at the top for the logging system.

### Key Subsystems
- **Config** (`src/config/`, `include/config/`) — Thread-safe persistent storage on LittleFS. Uses `ReadWriteMutex` with RAII locks. Dual format: JSON (REST API) and FlatBuffers (binary storage). Sub-configs: WiFi, Backend, RF, OTA, Serial, CaptivePortal, EStop.
- **GatewayConnectionManager** / **GatewayClient** — WebSocket connection to cloud backend. JSON for text messages, FlatBuffers for binary. Rate limiting on auth failures (5 min cooldown). Broadcasts `AccountLinkStatusEvent` when auth token is validated.
- **CommandHandler** — Routes shocker commands to RF transmission. Protocols: Petrainer, CaiXianlin, etc.
- **Radio** (`src/radio/rmt/`) — Uses ESP32 RMT peripheral for precise 433 MHz signal timing.
- **CaptivePortal** — RFC-8908 compliant. Serves the SvelteKit frontend for device configuration. REST API endpoints for WiFi, account, GPIO, and OTA configuration. WebSocket for real-time events (scan results, network changes, connection status).
- **OtaUpdateManager** — Partition-based A/B updates with validation and rollback.
- **WiFiManager** / **WiFiScanManager** — Connection management with SSID-based connecting (no BSSID required). Supports hidden networks. Auth mode pinning prevents evil twin attacks. Optional BSSID pinning for advanced users.
- **Serial** (`src/serial/command_handlers/`) — 17 UART commands for debugging/configuration.
- **TaskUtils** (`src/util/TaskUtils.cpp`) — FreeRTOS task creation helpers with core affinity. `StopTask()` for cooperative task shutdown with bounded timeout and force-kill fallback.

### Captive Portal Architecture
The captive portal has two interfaces:
- **REST API** (`/api/*`) — HTTP endpoints for configuration actions (WiFi save/forget/connect, account link/unlink, GPIO pin changes, OTA settings). Responses use HTTP status codes for success/failure with JSON error bodies `{"error":"..."}`. Success with no data returns 200 with empty body.
- **WebSocket** (port 81) — FlatBuffers binary protocol for real-time events (network discovery, scan status, connection/disconnection, account link status) and shocker commands (`ShockerCommandList`).

Portal lifecycle:
- Opens when device is not fully configured (no WiFi credentials or no auth token)
- Stays open during setup regardless of WiFi/gateway connection state
- Closes via `/api/portal/close` (soft signal — waits for gateway connection) or `ForceClose()` (immediate)
- Auto-closes AP after 5 minutes with no WebSocket clients when gateway is connected
- 30-second startup grace period for already-configured devices

### Message Flow
```
Gateway WebSocket → message_handlers/websocket/gateway/ → command dispatch
Local WebSocket   → message_handlers/websocket/local/   → config/control
Serial UART       → serial/command_handlers/             → debug/config
REST HTTP         → captiveportal/CaptivePortalInstance   → config/control
```

Shocker command processing is shared between gateway and local WebSocket handlers via `message_handlers/ShockerCommandList.cpp`.

Serialization adapters in `src/serialization/`: `JsonAPI`, `JsonSerial`, `WSGateway`, `WSLocal`.

### Frontend Structure (`frontend/`)
Svelte 5 SPA built as a single-file HTML (vite-plugin-singlefile) and served from LittleFS.

```
src/
  App.svelte              — Root: routing between Landing, Guided, Advanced, Success views
  lib/
    views/                — Page-level components (Landing, Guided, Advanced, Success)
    components/           — Reusable UI components
      steps/              — Wizard step components (HardwareStep, WiFiStep, TestStep, AccountStep)
      sections/           — Advanced mode section components
      ui/                 — shadcn-svelte primitives (button, input, dialog, stepper, etc.)
      Layout/             — Header, Footer
    stores/               — Svelte 5 reactive state (HubStateStore, ViewModeStore, ColorScheme)
    MessageHandlers/      — WebSocket binary message handlers
    api.ts                — REST API client functions
    _fbs/                 — Generated FlatBuffers TypeScript bindings
    mappers/              — Config data mapping from FlatBuffers to TS types
```

State management uses Svelte 5 `$state`/`$derived` runes. Note: `Map` mutations require reassignment for reactivity (create a new Map).

### Threading
FreeRTOS tasks with explicit core affinity. `TaskUtils::TaskCreateExpensive()` avoids the WiFi core. Thread safety via `SimpleMutex` (binary semaphore) and `ReadWriteMutex` with `ScopedReadLock`/`ScopedWriteLock` RAII guards.

### GPIO Safety
`include/Chipset.h` defines unsafe pins (strapping, UART, SPI flash) per ESP32 variant. Runtime validation prevents configuring dangerous pins. Board-specific GPIO assignments are compile-time defines (`OPENSHOCK_LED_GPIO`, `OPENSHOCK_RF_TX_GPIO`, `OPENSHOCK_ESTOP_PIN`).

## FlatBuffers Schemas

Schemas live in the `schemas/` submodule (tracks `github.com/OpenShock/flatbuffers-schemas`). The sibling repo at `../flatbuffers-schemas/` is for making schema changes — edit there, copy to the submodule, then regenerate:

```bash
cp ../flatbuffers-schemas/HubConfig.fbs schemas/
python scripts/generate_schemas.py
```

Generated C++ headers go to `include/serialization/_fbs/`, TypeScript bindings to `frontend/src/lib/_fbs/`.

Key schema files:
- `HubConfig.fbs` — Persistent configuration (WiFi credentials with auth mode/BSSID pinning, RF, EStop, OTA)
- `HubToLocalMessage.fbs` — Events sent from firmware to frontend (ReadyMessage, WiFi events, AccountLinkStatusEvent)
- `LocalToHubMessage.fbs` — Commands from frontend to firmware (ShockerCommandList)
- `GatewayToHubMessage.fbs` — Commands from cloud backend to firmware
- `Common/ShockerCommand.fbs` — Shared shocker command types used by both local and gateway

## Migration Goals

**Arduino → ESP-IDF**: We are actively migrating away from the Arduino framework toward native ESP-IDF. When touching code that uses Arduino APIs, prefer replacing them with ESP-IDF equivalents where practical. Avoid introducing new Arduino dependencies.

**Per-board → Per-chip builds**: The current per-board build matrix (each board is a separate PlatformIO environment) is being phased out in favor of per-chip compiles (ESP32, ESP32-S2, ESP32-S3, ESP32-C3) with runtime or config-time pin assignment. This reduces CI/CD load and simplifies maintenance. When making changes, prefer chip-level abstractions over board-specific `#ifdef`s and avoid adding new per-board environments.

**WS → REST**: Local WebSocket commands are being migrated to HTTP REST endpoints on the captive portal. The WebSocket channel is reserved for real-time events and shocker commands (binary FlatBuffers). New configuration endpoints should be REST, not WS.

## Conventions

- C++20 with `-fno-exceptions` — use return values for error handling, not try/catch
- Logging: `OS_LOGI(TAG, "format %s", arg)` — levels: `OS_LOGV/D/I/W/E`, panics: `OS_PANIC/OS_PANIC_OTA/OS_PANIC_INSTANT`
- Macros from `Common.h`: `DISABLE_COPY(T)`, `DISABLE_MOVE(T)`, `DISABLE_DEFAULT(T)`
- HTTP content types: use `HTTP::ContentType::JSON`, `TextPlain`, `TextHTML` from `include/http/ContentTypes.h`
- REST error responses: `{"error":"ErrorCode"}` with appropriate HTTP status. Success with no data: 200 empty body. Use `static const char*` constants for repeated JSON error strings.
- Dynamic JSON in REST handlers: use `cJSON` (ESP-IDF built-in), not Arduino `String` concatenation
- Environment variables embedded at build time via `scripts/embed_env_vars.py` (see `.env`, `.env.development`, `.env.production`)
- FlatBuffers schemas live in `schemas/` submodule; generated headers go to `include/serialization/_fbs/`
- Custom board JSON definitions in `boards/`, partition tables in `chips/`
- Libraries are pinned to specific git commits in `platformio.ini` `lib_deps`
- Frontend: Svelte 5 runes (`$state`, `$derived`, `$effect`). Avoid `<script module>` unless exporting types. Use `Component<any>` from `svelte` for dynamic component types (not lucide's `Icon` type).
