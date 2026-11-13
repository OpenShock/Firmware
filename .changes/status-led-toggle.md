---
kind: added
---
Turn the status LEDs off or dim them

## Release Note
The status LEDs can now be set to one of three modes: show every state (the default), light up only for an active E-Stop or a critical error, or stay off entirely. Their brightness can also be lowered, set separately for the built-in LED and the RGB LED, and separately for critical states (E-Stop, errors) and everything else.

Change these with the `led mode` and `led brightness` serial commands, or in the captive portal's new LEDs section, which also lets you preview each state's LED pattern and try colors on the RGB LED. Only the LEDs your board has are shown. The portal shows what the LEDs are showing right now. Brightness you move but don't save is shown on the LEDs and goes back to the saved value after 10 seconds without changes. A color preview ends 2 seconds after you stop moving the picker.

- Brightness is stored as 16 bits (0-65535) of the LED's full output; the portal shows it as a percent. Defaults match previous firmware exactly: built-in LED 65535 (100%), RGB LED 5140 (20/255, 7.8%); boards can change them in Kconfig (`OPENSHOCK_LED_*_BRIGHTNESS`)
- Setting the RGB LED above 30% logs a warning
- On boards with both LEDs, the built-in LED now also shows an E-Stop or a critical error, not only "connected"
