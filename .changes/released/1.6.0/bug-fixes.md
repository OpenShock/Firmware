---
kind: fixed
pr: 424
---
Fix race conditions between the hub's background tasks

## Release Note
Fix race conditions between the hub's background tasks
The backend connection, captive portal, E-Stop and command handler now share their state safely between tasks, with added null checks, which prevents rare crashes when several of them run at the same time.
