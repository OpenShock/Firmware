---
kind: fixed
---
Report a validated update as finished even if the hub restarted before telling the gateway

## Release Note
Updates no longer show as rolled back when they worked

A hub only reported a finished update on its first boot on the new firmware. If it restarted before reaching the gateway, as the storage migration does, the next boot reported a normal boot and the website showed the update as rolled back, though the hub was running the new firmware. The hub now reports the update as finished on its first boot that reaches the gateway.
