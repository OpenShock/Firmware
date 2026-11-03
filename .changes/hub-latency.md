---
kind: changed
---
Cut network latency between the hub, the gateway and the setup portal

## Release Note
Faster response to commands

Commands from the app reach your shockers sooner. While you are using the hub its WiFi radio stays awake instead of waiting for the access point's next beacon, reconnections are quicker, and the connection buffers are larger.

- WiFi power save is switched off on the first command and back on after an idle period (`CONFIG_OPENSHOCK_WIFI_POWERSAVE_IDLE_MS`, default 1 hour)
- RF commands are picked up between frames instead of between rounds
- TLS sessions are resumed when reconnecting to the gateway and HTTP hosts
- Gateway reconnects back off from 2 s instead of a flat 20 s
- Larger HTTP client, gateway websocket and TCP send/receive buffers (16 KB window); Nagle is off on portal sockets
- Read-only littlefs mounts are read through a flash mapping
