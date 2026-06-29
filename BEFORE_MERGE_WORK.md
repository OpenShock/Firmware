# Before-merge work — `feat/arduino-3.0`

Tracking outstanding items discovered while getting the Arduino 3.0 branch to
build. This branch is the migration vehicle toward the full ESP-IDF endgame
(what `feature/espidf-rewrite` attempted in one shot).

## Per-board build results — 12/13 pass ✅

| ✅ Succeeded (12) | ❌ Failed (1) |
|---|---|
| Wemos-D1-Mini-ESP32, Wemos-Lolin-S3, Wemos-Lolin-S3-Mini, Waveshare_esp32_s3_zero, Pishock-2023, Pishock-Lite-2021, Seeed-Xiao-ESP32C3, Seeed-Xiao-ESP32S3, DFRobot-Firebeetle2-ESP32E, OpenShock-Core-V1, OpenShock-Core-V2, NodeMCU-32S | Wemos-Lolin-S2-Mini |

The component-layout fixes are sound — all the boards that matter (incl. both
OpenShock-Core revisions) link cleanly. The `ci-build` env passes too.

**S3-Mini fixed**: added `-DARDUINO_USB_MODE=1` to its env (it has a built-in
USB-Serial-JTAG, so `Serial` becomes `HWCDCSerial` — no extra component needed,
exactly what the `esp32-s3-devkitc-1` board does).

## The 2 failures are a pre-existing USB-config bug, not the merge

Both failures are identical:

```
HardwareSerial.h:435: error: '::USBSerial' has not been declared
```

- Only these two boards set `-DARDUINO_USB_CDC_ON_BOOT=1` (platformio.ini lines
  119/130) — and that flag was already there at `df131af`, before the
  component-layout commit. The merge changes (enums prefix, protocols
  component, enum casts) don't touch serial/USB.
- With `ARDUINO_USB_CDC_ON_BOOT=1` and `ARDUINO_USB_MODE` unset, `Serial`
  resolves to `USBSerial`, which isn't declared because the native USB-CDC
  (TinyUSB) stack isn't pulled in under this `arduino,espidf` hybrid build. The
  working S3 boards avoid it by not enabling CDC-on-boot (they use UART
  `Serial0`).

`USBSerial` is declared (arduino-esp32 `USBCDC.h`) only when
`SOC_USB_OTG_SUPPORTED && CONFIG_TINYUSB_CDC_ENABLED && !ARDUINO_USB_MODE &&
ARDUINO_USB_CDC_ON_BOOT`. The TinyUSB component isn't currently pulled into this
`arduino,espidf` build (no `CONFIG_TINYUSB*` symbols in the generated configs),
so `USBSerial` is never declared.

- **S3-Mini** — FIXED via `-DARDUINO_USB_MODE=1` (HWCDC / USB-Serial-JTAG path,
  no TinyUSB needed). See platformio.ini.
- **S2-Mini** — STILL FAILS. ESP32-S2 has no USB-Serial-JTAG, so HWCDC isn't an
  option; it must use native USB CDC, which requires pulling the **TinyUSB
  component** into the build (component manager + `CONFIG_TINYUSB_CDC_ENABLED=y`
  in `sdkconfig.defaults.esp32s2`). This affects runtime USB/serial behaviour and
  wants a hardware-in-hand pass — left as a TODO.

## sdkconfig management — defaults + per-chip overrides

Decided structure (implemented):

- `sdkconfig.defaults` — shared base, **tracked** (the editable source of truth).
- `sdkconfig.defaults.<idf_target>` (e.g. `sdkconfig.defaults.esp32s2`) — per-chip
  overrides, **tracked**. ESP-IDF auto-layers these on top of `sdkconfig.defaults`
  for each entry in the defaults list (see `tools/cmake/kconfig.cmake`: it appends
  `--defaults <file>.${IDF_TARGET}` when the file exists). No custom scripting.
- `sdkconfig.<env>` — generated ("DO NOT EDIT"), now **git-ignored** (was being
  committed for 4 envs by accident; those are untracked). `dependencies.lock` is
  also generated and now ignored.

Per-board (finer than per-chip) USB *mode* flags live in each env's `build_flags`
in platformio.ini (e.g. S3-Mini's `ARDUINO_USB_MODE=1`), since they are board
wiring choices, not chip-wide sdkconfig.

## CI is red on every PR — root cause found & fixed ✅

**Symptom:** `ci-build` *and* `cpp-linter` fail on this branch (and on every PR
targeting it) ~2 min in, before any real compilation:

```
ModuleNotFoundError: No module named 'git'
  File ".../scripts/embed_env_vars.py", line 3:
    import git
```

`develop`'s `ci-build` is green — so this is **specific to the platform switch**,
not a flaky runner.

**Root cause:** `scripts/embed_env_vars.py` (a `pre:` SCons `extra_script` in
platformio.ini) does `import git` (GitPython). GitPython was never in
`requirements.txt` — it was being pulled in *transitively* by the old
`espressif32 @ 6.12.0` platform's Python env. The **pioarduino**
platform-espressif32 package this branch moves to does **not** depend on it, so
the import now fails at SCons config time and every `pio run` dies before
compiling. Both failing workflows run `pio`, so both go red from this one cause.

**Fix applied:** pinned `GitPython==3.1.46` in `requirements.txt`. This makes the
existing `import git` portable across platforms instead of relying on a
transitive dep. (Longer term, the script could be reworked to not hard-depend on
GitPython in CI — version/commit are already passed via `OPENSHOCK_FW_*` env
vars — but the pin is the minimal, behaviour-preserving fix.)

## CI is also slow — caching hardened ✅ (one more win left)

**Problem:** the three PlatformIO composite actions (`build-firmware`,
`build-compilationdb`, `build-staticfs`) all cache `~/.platformio/{platforms,
packages,.cache}` (+ `.pio/libdeps`) with
`key: pio-<os>-<hash(platformio.ini, requirements.txt)>` and **no `restore-keys`**.
Because this branch edits `platformio.ini` constantly, every edit is a *total*
cache miss → full re-download of the now-larger pioarduino platform zip + the
xtensa-gdb / openocd `platform_packages` + toolchains. That's most of the 2–4 min
each job spends before building.

**Fix applied:** added `restore-keys: pio-<os>-` to all three cache steps, so a
changed key restores the most recent prior cache and only fetches deltas. PRs can
also fall back to `develop`'s warm cache via the same prefix.

**Still worth doing (not yet done):** the `arduino,espidf` build recompiles a
large ESP-IDF tree per board with no compiler cache. Enabling **ccache**
(`IDF_CCACHE_ENABLE=1` + cache `~/.cache/ccache`, keyed per board) should cut
firmware build wall-time substantially across the matrix. Left out of this pass
to keep the change low-risk; do it as a follow-up once CI is green.

## Functional regressions left disabled in the diff (must resolve before merge)

These were commented out / removed to *get the branch compiling* and are still
inactive at the branch tip — they are behaviour changes, not just build hacks:

- **Custom DNS servers are no longer set.** `src/wifi/WiFiManager.cpp:446` — the
  `set_esp_interface_dns(..., 1.1.1.1, 8.8.8.8, 9.9.9.9)` call (and its error
  handling) is commented out (commit `5b2bdf6`). The device now relies purely on
  DHCP-provided DNS. Re-enable once the Arduino 3.0 / IDF DNS API is sorted.
- **IPv6 reporting dropped.** `WiFiManager::GetIPv6Address()` was removed and
  `GetIPAddress()` now does
  `snprintf(ipAddress, IPV6ADDR_FMT_LEN + 1, "%s", ip.toString())` — formatting an
  IPv4 into an IPv6-sized buffer (same commit, "type now has combined v4 and v6").
  Confirm the buffer sizing/format is intentional, or restore proper handling.
  (Note: the `Checksum.h` `#error` from `4f83d81` was *properly* resolved — it was
  migrated to `std::integral` concepts, not just commented out. No action needed.)

## C++ standard mismatch to reconcile

New `components/common/src/LanguageVersionCheck.cpp` hard-`#error`s anything below
**C++23** (`__cplusplus >= 202302L`), but `platformio.ini:21-22` still sets
`-std=c++2a` / `-std=gnu++2a` (C++20 = `202002L`). Builds reportedly pass, which
implies the IDF component build overrides the standard to gnu++23 — but the
platformio `build_flags` are at best stale/contradictory. Align them (bump the
flags to `c++2b`/`gnu++2b`) or confirm where the real standard is set, otherwise
that check is a latent `#error`.

## Release bookkeeping

This branch deletes `FLATBUFFERS_CHANGES.md` but adds **no `.changes/*.md`
entry**, even though the repo's release flow (`check-changes` workflow +
`OpenShock/release-tool`) expects a changeset per PR. A changelog entry for the
Arduino 3.0 migration is needed (or the `no-changelog` label) for the PR to pass
`check-changes`. Also delete this `BEFORE_MERGE_WORK.md` scratch file before merge.
