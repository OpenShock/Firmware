---
kind: fixed
---
Close the gateway websocket cleanly when the hub restarts

## Release Note
No more false connection errors on restart

The hub now closes its server connection properly before it restarts, so restarts no longer end with "Received error from API" and TLS read errors in the log when nothing was wrong.

- A shutdown handler, registered after `esp_wifi_init()` so it runs before WiFi stops, closes the gateway websocket; it covers every `esp_restart()` caller
- A restart requested by the gateway is deferred from the websocket task to the main task, which can close the client, and is now logged
