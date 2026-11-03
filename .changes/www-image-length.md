---
kind: fixed
---
Authenticate the setup portal's www image by an explicit length before parsing it

## Release Note
Setup portal files checked more strictly

The hub now checks the setup portal's files against their hash over an exact, declared length before reading anything else from them, so a damaged or partly written portal image is refused as a whole instead of being partly read.

- The www image header gains an `image_len` field; the SHA-256 covers exactly those bytes and is checked first
- docs/STORAGE.md documents the format, with an ImHex pattern
