---
kind: chore
---
Compile once per build variant in CI and stamp each board from it

## Release Note
CI now compiles the firmware once per build variant instead of once per board, and stamps each board's descriptor into a copy of its variant's image. A variant is every board one compile can serve: the same chip and the same compiled-in Kconfig, with pins and flash size left out because they are settled after linking. The variants are derived from the boards a build asks for, never declared, so today's 12 boards are 5 compiles, and a board that changes a Kconfig setting moves into a variant of its own.

- Group boards into variants in `scripts/utils/boards.py`, and add `scripts/build.py --variant <id>` to build one; board builds are unchanged
- Add `scripts/package_boards.py`, which stamps, verifies and lays out each board's images from its variant's build, replacing the `merge-partitions` action with `package-firmware`
- Restructure `ci-build.yml` into gate, plan, stage and staticfs, one build job per variant, then package and publish; `get-vars.yml` is inlined into the plan job and `checkpoint-build` keeps the name the develop ruleset requires
- Cache ESP-IDF with ccache bundled into the install and ccache per variant, both saved only from develop, which keeps the run inside the repository's cache limit
- Merge images with esptool's bootloader offsets and `--flash-size keep`, so a merged image holds exactly the images published beside it
- Consolidate the build and CI script helpers and their tests, and drop helpers nothing calls

## Notices
- info: Boards with more than 4 MB of flash now ship with a bootloader whose header says 4 MB, the size the shared partition table is laid out for. They log that the chip is larger than the image header says at boot and behave as before; no board's partition table uses flash past 4 MB.
