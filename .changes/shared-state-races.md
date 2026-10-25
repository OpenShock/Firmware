---
kind: fixed
---
Fix races, lock misuse and blocking calls in shared state across the firmware

## Release Note
Stability fixes: fewer hangs and stalls

The hub no longer freezes up during flash writes or when the network connection stalls. Commands, including Stop, are also handled more reliably under load.

- Stop jumps ahead of queued commands and cancels the older ones for that shocker, instead of being dropped when the queue is full
- An active E-Stop stays latched across internal restarts, and a software E-Stop takes effect immediately
- The E-Stop sampler and RF transmitter run above the network tasks; TLS work stays on the other core
- Failing RF transmits back off instead of spinning, and a stop frame only counts once it was actually sent
- No task is ever force-deleted mid-operation; a task that won't stop is waited out and the device restarts after 10 s
- The E-Stop and RF loops are watched by the task watchdog (logged, not a reboot)
- Config, WiFi and gateway locks are no longer held across flash writes or driver calls, which could stall every WiFi and IP event
- Logging no longer stalls when no USB host is reading the console
- The gateway connection recovers from missed WiFi events, and handles unlink, token changes and client replacement without stale state
- Captive portal WebSocket messages are queued and retried instead of silently dropped
- OTA no longer reports an abandoned update after reboot, and erases flash incrementally
- Logging from about 35 source files (config, WiFi, gateway, OTA, serial) was compiled out and now prints
