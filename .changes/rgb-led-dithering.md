---
kind: added
---
Smoother RGB LED dimming

## Release Note
The RGB status LED now dims and blends colors in finer steps. Before, low brightness settings left only a few distinct levels per color, so dimming jumped visibly and nearby colors looked the same; now there are four times as many.

- The LED takes 8 bits per color; the driver shows 10 by repeating 4 frames that alternate between the two nearest levels, looped by the RMT hardware so it costs no CPU (ESP32 and ESP32-S3; the ESP32-C3 lacks the RMT memory and stays at 8 bits)
- On by default; `CONFIG_OPENSHOCK_RGB_LED_DITHERING` turns it off
