---
type: minor
---

Full setup wizard, REST API migration, and WiFi CRUD overhaul

- Migrate all LocalToHub WebSocket commands to HTTP REST endpoints
- Add guided setup wizard with multi-step stepper and advanced settings mode with vertical section navigation
- Portal close is now a soft signal that stays open until the device is fully online
- Add 5-minute auto-close timer when no WebSocket clients are connected

## Summary
New guided setup wizard walks you through device configuration step by step. An advanced settings mode is also available for experienced users. The captive portal now stays open until the device is fully connected, preventing premature disconnects during setup.
