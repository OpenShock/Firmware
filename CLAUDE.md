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

# Upload to connected board
pio run -e Wemos-D1-Mini-ESP32 -t upload

# Serial monitor (115200 baud)
pio device monitor

# Static analysis
pio check -e ci-build

# Frontend (from frontend/ directory)
pnpm install
pnpm run build
pnpm run lint        # Prettier + ESLint
pnpm run check       # SvelteKit type checking
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
- **GatewayConnectionManager** / **GatewayClient** — WebSocket connection to cloud backend. JSON for text messages, FlatBuffers for binary. Rate limiting on auth failures (5 min cooldown).
- **CommandHandler** — Routes shocker commands to RF transmission. Protocols: Petrainer, CaiXianlin, etc.
- **Radio** (`src/radio/rmt/`) — Uses ESP32 RMT peripheral for precise 433 MHz signal timing.
- **CaptivePortal** — RFC-8908 compliant. Serves the SvelteKit frontend for device configuration.
- **OtaUpdateManager** — Partition-based A/B updates with validation and rollback.
- **WiFiManager** / **WiFiScanManager** — Connection management with credential rotation.
- **Serial** (`src/serial/command_handlers/`) — 17 UART commands for debugging/configuration.

### Message Flow
```
Gateway WebSocket → message_handlers/websocket/gateway/ → command dispatch
Local WebSocket   → message_handlers/websocket/local/   → config/control
Serial UART       → serial/command_handlers/             → debug/config
```

Serialization adapters in `src/serialization/`: `JsonAPI`, `JsonSerial`, `WSGateway`, `WSLocal`.

### Threading
FreeRTOS tasks with explicit core affinity. `TaskUtils::TaskCreateExpensive()` avoids the WiFi core. Thread safety via `SimpleMutex` (binary semaphore) and `ReadWriteMutex` with `ScopedReadLock`/`ScopedWriteLock` RAII guards.

### GPIO Safety
`include/Chipset.h` defines unsafe pins (strapping, UART, SPI flash) per ESP32 variant. Runtime validation prevents configuring dangerous pins. Board-specific GPIO assignments are compile-time defines (`OPENSHOCK_LED_GPIO`, `OPENSHOCK_RF_TX_GPIO`, `OPENSHOCK_ESTOP_PIN`).

## Migration Goals

**Arduino → ESP-IDF**: We are actively migrating away from the Arduino framework toward native ESP-IDF. When touching code that uses Arduino APIs, prefer replacing them with ESP-IDF equivalents where practical. Avoid introducing new Arduino dependencies.

**Per-board → Per-chip builds**: The current per-board build matrix (each board is a separate PlatformIO environment) is being phased out in favor of per-chip compiles (ESP32, ESP32-S2, ESP32-S3, ESP32-C3) with runtime or config-time pin assignment. This reduces CI/CD load and simplifies maintenance. When making changes, prefer chip-level abstractions over board-specific `#ifdef`s and avoid adding new per-board environments.

## Conventions

- C++20 with `-fno-exceptions` — use return values for error handling, not try/catch
- Logging: `OS_LOGI(TAG, "format %s", arg)` — levels: `OS_LOGV/D/I/W/E`, panics: `OS_PANIC/OS_PANIC_OTA/OS_PANIC_INSTANT`
- Macros from `Common.h`: `DISABLE_COPY(T)`, `DISABLE_MOVE(T)`, `DISABLE_DEFAULT(T)`
- Environment variables embedded at build time via `scripts/embed_env_vars.py` (see `.env`, `.env.development`, `.env.production`)
- FlatBuffers schemas live in `schemas/` submodule; generated headers go to `include/serialization/_fbs/`
- Custom board JSON definitions in `boards/`, partition tables in `chips/`
- Libraries are pinned to specific git commits in `platformio.ini` `lib_deps`
