---
kind: changed
---
Serve the setup portal from a flat www image where the partition table has a www partition

## Release Note
Setup portal assets get their own www partition

The firmware's partition table replaces the 1.x littlefs `static0` partition with a `www` partition (type 0x40, same offset and size) that holds the setup portal's assets as one flat, read-only image. The firmware maps the image once and serves it by pointer, checked against a SHA-256 before anything is served. The image is identical for every board, so an update skips re-flashing it when the one on flash already matches.

Hubs in the field keep their 1.x partition table for now: this release installs over the air like any other, keeps serving the portal from the littlefs `static0`, and keeps updating it the way 1.x firmware does. The in-place move onto the `www` partition ships with the release that runs the storage migrations.
- `partitions/ota_4mb.csv` turns `static0` into `www`; the 1.x table is kept as `partitions/legacy/ota_4mb_v1.csv`.
- `scripts/gen_staticfs.py` packs the frontend build into `www.bin` (`scripts/utils/wwwimage.py`) and, with `--legacy-output`, into the 1.x littlefs `staticfs.bin`. A 1.x-numbered release publishes the littlefs image as its `staticfs` artifact, since 1.x firmware's own update code installs it by mounting it; from 2.0.0 the artifact is `www.bin`.
- The over-the-air update flashes whichever of `www` or `static0` the running table has and verifies it in that partition's format.
- `components/www` adds the in-place migration (`Www::MigrateToWwwPartition`) that retypes `static0` as `www` and downloads the image over it; nothing calls it yet.
