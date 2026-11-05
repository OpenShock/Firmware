---
kind: added
---
Support latching (twist-to-release) E-Stop switches

## Release Note
The E-Stop can now be a latching switch, such as a twist-to-release mushroom button, instead of a momentary push button

A latching E-Stop activates when the switch is engaged and clears as soon as it is released again, instead of needing the button held for 5 seconds. A software-triggered E-Stop is cleared by cycling the switch: engage it, then release it. The switch type is set over serial with `estop latching true` (or `false` for a momentary button); the portal does not expose E-Stop settings yet.

- Add the `latching` key (u8) to the `estop` NVS section and `latching` to its jsonconfig section; it defaults to the board's `OPENSHOCK_ESTOP_LATCHING`
- Add `OPENSHOCK_ESTOP_LATCHING` to board fragments, stamped into the board descriptor as `OPENSHOCK_BOARD_FLAG_ESTOP_LATCHING`
- Add the `estop latching` serial command; changing the switch type is refused while the E-Stop is active
