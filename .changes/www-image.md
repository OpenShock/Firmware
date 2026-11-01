---
kind: changed
---
Serve the setup portal from a flat www image

## Release Note
Setup portal assets get their own www partition

The firmware's partition table has a `www` partition (type 0x40) that holds the setup portal's assets as one flat, read-only image. The firmware maps the image once and serves it by pointer, checked against a SHA-256 before anything is served. The image is identical for every board, so an update skips re-flashing it when the one on flash already matches.

- `scripts/gen_staticfs.py` packs the frontend build into `www.bin` (`scripts/utils/wwwimage.py`), published as the release's `staticfs` artifact.
- The over-the-air update flashes the `www` partition and verifies the image there, and refuses a download that is not a www image before erasing anything.
