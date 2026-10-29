---
kind: added
pr: 469
---
Reconnect when the gateway stops sending keepalive pings

## Release Note
Hub reconnects when its connection silently dies

The gateway pings the hub regularly, but the hub never checked that the pings kept coming. When the connection broke without being closed, for example after a router or WiFi hiccup, the hub kept treating it as connected: it showed as offline and ignored commands until it was restarted. The hub now reconnects when no ping has arrived for 90 seconds, asking the API for a gateway again.
