---
kind: added
---
Smoother RGB LED dimming

## Release Note
The RGB status LED now dims and blends colors in much finer steps. Before, low brightness settings left only a few distinct levels per color, so dimming jumped visibly and nearby colors looked the same; now they change smoothly.

- The LED takes 8 bits per color; the driver keeps 16 and rapidly alternates between the two nearest levels (temporal dithering), so the average matches the requested value
- On by default; `CONFIG_OPENSHOCK_RGB_LED_DITHERING` turns it off
