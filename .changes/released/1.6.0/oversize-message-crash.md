---
kind: security
---
Reject oversize WebSocket messages and config files before verifying them

## Release Note
Fixed a crash caused by oversized messages

A WebSocket message or stored config file of 4 KB or more tripped an assertion in the FlatBuffers verifier and rebooted the hub. Any client connected to the setup access point could trigger it. Oversized input is now rejected, and configs that large are refused when saving so a saved config always loads again.
