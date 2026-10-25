---
kind: added
---
Add an opt-in `diag` serial command with drop counters, heap minimums and task stack high-water marks

Off by default and compiled out; enable `CONFIG_FREERTOS_USE_TRACE_FACILITY` to build it in.

## Release Note
New `diag` serial command for troubleshooting, in builds with `CONFIG_FREERTOS_USE_TRACE_FACILITY` enabled

- Shows how often RF, keep-alive, event, captive portal, gateway and serial paths dropped work since boot
- Shows the internal heap minimum and largest free block, and the least free stack of every task
