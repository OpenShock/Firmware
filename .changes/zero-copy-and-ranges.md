---
kind: fixed
---
Cut string copies and allocations across the firmware, and fix the issues found along the way

## Release Note
Setup portal and WiFi fixes, less memory churn

A WiFi password too long for the setup portal is now rejected, instead of the network being saved as an open one. Network names from a WiFi scan are always read correctly, and the hub does less copying and allocating while it parses, builds and logs text.

- The setup portal reads query and form parameters in place, without a 256-byte limit
- Account link codes must be letters and digits before they are put into a request URL
- Network names copied from scan results are always terminated
- Saved WiFi credentials are looked up by id instead of being copied, and the portal's ready message no longer copies every saved password to list the network names
- JSON fields that are only parsed or compared are read in place, without a copy
- Firmware versions are printed and compared without building strings
- RF transmitter and LED task names fit FreeRTOS's 15-character limit
