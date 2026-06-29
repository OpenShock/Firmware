---
kind: changed
---
Report specific account linking failure reasons instead of a generic internal error

## Release Note
More descriptive account linking errors

Account linking now reports the specific reason it failed (request failed, timed out, server error, invalid response, or failed to save the token) in both the captive portal and the device logs, instead of a generic "Internal error".
