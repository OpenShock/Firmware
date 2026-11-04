---
kind: added
---
Turn the status LEDs off or dim them

## Release Note
The status LEDs can now be set to one of three modes: show every state (the default), light up only for an active E-Stop or a critical error, or stay off entirely. Their brightness can also be lowered, set separately for the built-in LED and the RGB LED, and separately for critical states (E-Stop, errors) and everything else.

Change these with the `led mode` and `led brightness` serial commands, or in the captive portal's new LEDs section, which also lets you preview each state's LED pattern and try colors on the RGB LED. Only the LEDs your board has are shown.

- Brightness is a percent of the LED's full output. Defaults match previous firmware: built-in LED 100%, RGB LED 8%; boards can change them in Kconfig (`OPENSHOCK_LED_*_BRIGHTNESS`)
- Setting the RGB LED above 30% logs a warning
