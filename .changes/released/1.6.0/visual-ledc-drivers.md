---
kind: changed
pr: 435
---
Replace LED managers with LEDC-driven drivers

## Release Note
LED indicators are now driven by hardware PWM instead of raw GPIO toggling, which adds brightness control; the blink patterns themselves are unchanged. This applies to both single-color and RGB LEDs across all supported ESP32 chip variants.

- Rename PinPatternManager/RGBPatternManager to MonoLedDriver/RgbLedDriver
- Replace raw GPIO toggling with ESP32 LEDC hardware PWM for 8-bit brightness control
- Use chip-agnostic LEDC speed mode (SOC_LEDC_SUPPORT_HS_MODE) for ESP32/S2/S3/C3 compatibility
- Add cooperative task shutdown with chunked 50ms delays
- Add ledtest serial command for visual verification
