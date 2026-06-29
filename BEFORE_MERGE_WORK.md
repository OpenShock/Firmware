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
