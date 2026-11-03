---
kind: added
---
Set the log level at runtime over serial

## Release Note
The serial console can now quiet the hub's logs with `loglevel <level>`, until the next restart

- `loglevel` shows the current level and the highest one the build includes; `loglevel none|error|warn|info|debug|verbose` sets it
- Only levels the build already includes can be turned on: nothing is added to the image, and a release build cannot be made more verbose than it was built
- The filter reads the level from the log line's format string, so no call site gains an argument
