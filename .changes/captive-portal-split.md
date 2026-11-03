---
kind: fixed
---
Tighten HTTP handling in the setup portal's file server

## Release Note
Setup portal file serving tightened up

The portal's web server now respects the browser's compression preferences, answers HEAD requests, and replies with a proper error instead of redirecting or dropping the connection when it cannot serve a path. The captive portal code was also split into smaller units.
