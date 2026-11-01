---
kind: removed
pr: 552
---
Remove the FlatBuffers form of the config and the rawconfig serial command

## Release Note
The rawconfig serial command is gone

Use `jsonconfig` to read or set the whole config over serial.

- The HubConfig schema, `Config::Wire` and every section's `ToFlatbuffers` are removed
- The portal's Ready message carries only what it shows: the RF TX pin, the E-Stop state, the saved SSIDs and the OTA settings
