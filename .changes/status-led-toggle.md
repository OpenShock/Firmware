---
kind: added
---
Turn the status LEDs off or dim them

## Release Note
The status LEDs can now be turned off, limited to critical states, or dimmed, for hubs in a bedroom.

`led mode off` keeps the LEDs dark, `led mode critical` lights them only for an active E-Stop or a critical error, and `led mode all` shows every state again. `led brightness <mono|rgb> <critical|normal> <percent>` sets the brightness of the built-in LED or the RGB LED, separately for critical and normal patterns, as a percent of the LED's full output.

The captive portal has a new LEDs section with the same settings, brightness sliders that light the LED live while dragging, buttons that preview each state's pattern, and a color wheel to test the RGB LED.

- Add the `led` config section (NVS namespace `led`, keys `mode`, `monoCrit`, `monoNorm`, `rgbCrit`, `rgbNorm` (u8); `led.mode`, `led.monoCriticalBrightness`, `led.monoNormalBrightness`, `led.rgbCriticalBrightness` and `led.rgbNormalBrightness` in jsonconfig)
- The defaults keep today's look: every state shown, the built-in LED at 100%, the RGB LED at 8% (20/255). The default brightnesses are Kconfig options (`OPENSHOCK_LED_*_BRIGHTNESS`)
- Captive portal API: `GET`/`PUT /api/led` reads and saves the settings, `POST /api/led/preview?pattern=` or `?color=RRGGBB[&led=][&brightness=]` previews for 10 seconds without saving, `DELETE /api/led/preview` ends a preview
- An RGB brightness above 30% logs a warning
- The LED test (`ledtest`) shows every state pattern whatever the mode, at the configured brightness
