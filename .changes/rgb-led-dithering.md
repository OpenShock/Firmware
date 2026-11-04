---
kind: added
---
Smoother RGB LED dimming

## Release Note
The RGB status LED now dims and mixes colors in much finer steps. The LED itself takes 8 bits per color; the hub keeps 16 and alternates between the two nearest steps faster than the eye can follow, so low brightness settings no longer jump between coarse levels.

- Temporal dithering in the RGB LED driver: 16 bits per channel (color times brightness), shown as a first-order sigma-delta between adjacent 8-bit steps
- Frames only go out while a channel sits between two steps, every `CONFIG_OPENSHOCK_RGB_LED_DITHER_INTERVAL_MS` (default 1 ms); a whole 8-bit color is sent once as before
- `CONFIG_OPENSHOCK_RGB_LED_DITHERING` (default on) turns it off, rounding to the nearest 8-bit step instead
- The RGB driver takes 16-bit patterns (`RgbLedState16`) and brightness (`SetBrightness16`); 8-bit patterns and `SetBrightness` keep working
