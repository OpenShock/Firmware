---
kind: safety
---
Keep an active E-Stop latched across reboots

## Release Note
An active E-Stop now survives a reboot or power cut

If the E-Stop is active when the hub restarts or loses power, it comes back up with the E-Stop still active, and nothing is sent to your shockers until you clear it with the button.

- The latch is the `latched` key (u8) in the `estop` NVS namespace; it is state, not config, so it is not in `jsonconfig`
- The E-Stop task writes it after every activation and every clear, once the change is already in effect; it never waits for another config writer, it retries on its next sample
- It is read in `EStopManager::Init`, before anything can transmit; a missing key reads as not latched, a read error as latched
- A latch on a hub whose E-Stop is disabled is dropped, since no button could clear it; a factory reset clears it
- `IsEStopped` reads the software trigger before the active flag, so a trigger cleared by the task in between is never missed
