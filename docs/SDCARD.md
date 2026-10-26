# OpenShock SD Card Log Storage: Layout and Format

Oct 26, 2026 · @Opto

> Status on this branch: see [Implementation status](#implementation-status) at the end for where the firmware
> differs from this design and what is not built yet.

## Summary

Logs and crash dumps are stored on a removable microSD card with no filesystem. The card has an MBR with two partitions. The dump partition (type `0xFA`) is a fixed 256 MiB and holds coredumps in fixed 1 MiB slots. The log partition (type `0x7F`) covers the rest of the card and holds a ring of 512-byte blocks. Each partition starts with a write-once superblock. Every block and every slot carries its own sequence number, format epoch and CRC, so each can be checked on its own. Nothing finished is ever rewritten, so pulling the card or losing power damages at most the block or slot being written at that moment.

The hub formats a card only when a person explicitly asks it to. A card it doesn't recognise is reported and left untouched. A Python script in the repo reads the card on a PC: it opens the raw partition (or an image of it), finds the newest block by binary search, and writes out text logs, JSON lines and any coredumps the hub copied from internal flash.

## Requirements and non-goals

Requirements:

- Removable media, readable on a PC with a script from the repo.
- No filesystem anywhere on the card. No FAT, exFAT or littlefs, and no FatFs or VFS in the firmware.
- Logging for weeks to years with no network and no attention.
- Power loss or card removal at any moment loses at most the block being written and the unflushed RAM queue. Nothing already on the card is lost.
- Finding the head after a mount takes a fraction of a second on any card size, with no full scan.
- The hub formats a card only on an explicit request. It never writes to a card it did not format.
- Coredumps captured in internal flash are copied to the card.
- Logging never blocks the code that logs. When the card is slow, absent or full of errors, records are dropped and the drops are counted.

Non-goals:

- Mounting the card as a drive on a PC. Raw access is accepted, and macOS users will see a "not readable" prompt they must dismiss.
- Encryption or tamper evidence.
- SDUC cards (larger than 2 TB). MBR addressing ends at 2^32 sectors.
- Live streaming. This format is for storage. Live logs go over serial or the network.
- Writing during a panic. The coredump partition in internal flash stays the capture point for crashes.

## Card layout

All addresses are 512-byte sectors. Absolute LBAs count from the start of the card. D+n and L+n count from the first sector of the dump and log partitions. All integers are little-endian.

| Region | Location | Size | Written |
| --- | --- | --- | --- |
| MBR | LBA 0 | 1 sector | at format |
| Gap | LBA 1 to 8,191 | 4 MiB minus 512 B | never (left as found) |
| Dump partition, type 0xFA | LBA 8,192 | 256 MiB (524,288 sectors) | superblocks at format, then one slot per stored dump |
| Log partition, type 0x7F | LBA 532,480 | rest of the card, rounded down to 4 MiB | superblocks at format, then appended continuously |

Inside the dump partition:

| Region | Location | Size |
| --- | --- | --- |
| Superblock, primary | D+0 | 1 sector |
| Superblock, backup | D+1 | 1 sector |
| Reserved | D+2 to D+2,047 | about 1 MiB, zeroed at format |
| Slots | D+2,048 onward | 255 slots of 2,048 sectors (1 MiB) |

Inside the log partition:

| Region | Location | Size |
| --- | --- | --- |
| Superblock, primary | L+0 | 1 sector |
| Superblock, backup | L+1 | 1 sector |
| Reserved | L+2 to L+7 | 6 sectors, zeroed at format |
| Ring | L+8 to end of partition | `ring_blocks` sectors |

The dump partition goes first because it has a fixed size. That puts both partitions at the same LBA on every card, and the log partition takes whatever capacity is left. Both partitions start on 4 MiB boundaries, which match the SD allocation unit for every capacity class up to SDXC, so sequential writes stay inside the card's erase units. `ring_blocks` is the log partition size minus 8, rounded down to a multiple of 8.

The smallest card accepted is 1 GiB. The largest is 2^32 − 1 sectors (just under 2 TiB), the most MBR can address. The gap, the ring and the slots are never erased at format. The epoch (see Ring) is what keeps stale content from being read as valid.

## MBR

The MBR is a standard one with two partition entries and no boot code.

| Offset | Size | Value |
| --- | --- | --- |
| 0x000 | 440 | zero |
| 0x1B8 | 4 | disk signature, random from `esp_random()` |
| 0x1BC | 2 | 0x0000 |
| 0x1BE | 16 | entry 1: dump partition |
| 0x1CE | 16 | entry 2: log partition |
| 0x1DE | 32 | entries 3 and 4, zero |
| 0x1FE | 2 | 0x55, 0xAA |

Each partition entry:

| Offset in entry | Size | Entry 1 (dumps) | Entry 2 (logs) |
| --- | --- | --- | --- |
| 0x0 | 1 | 0x00 | 0x00 |
| 0x1 | 3 | CHS 0xFE 0xFF 0xFF | CHS 0xFE 0xFF 0xFF |
| 0x4 | 1 | type 0xFA | type 0x7F |
| 0x5 | 3 | CHS 0xFE 0xFF 0xFF | CHS 0xFE 0xFF 0xFF |
| 0x8 | 4 | 8,192 | 532,480 |
| 0xC | 4 | 524,288 | (card sectors − 532,480), rounded down to a multiple of 8,192 |

Type 0x7F is reserved for individual and local use. Type 0xFA was historically used by the Bochs emulator, and no current OS claims it. Neither partition gets a drive letter on Windows, and Windows offers to format neither. Linux exposes both as devices with no filesystem. macOS finds nothing to mount and shows its "not readable" dialog.

On insertion the firmware puts the card into one of three classes. Only the first is ever written to.

1. Ours: all of the following hold.
   - Signature 0x55AA.
   - Entry 1 has type 0xFA at LBA 8,192 with 524,288 sectors.
   - Entry 2 has type 0x7F at LBA 532,480 and ends within the card's sector count from the CSD.
   - Entries 3 and 4 are zero.
   - Both partitions have a valid superblock, and both superblocks carry the same epoch.
2. Blank: sector 0 is all 0x00 or all 0xFF.
3. Foreign: anything else. That includes any FAT, exFAT or GPT protective (type 0xEE) card, and our MBR with a missing, invalid or mismatched superblock.

Blank and foreign cards are reported as "needs formatting" and never written.

## Superblocks

Each partition has a superblock, written once at format in identical copies at sectors 0 and 1 of the partition, and never again. Nothing the writers do depends on updating it. All mutable state lives in the ring blocks and the slot headers. Both superblocks share one layout and one card-wide epoch; only the magic and the unit fields differ.

| Offset | Size | Field | Log partition | Dump partition |
| --- | --- | --- | --- | --- |
| 0x000 | 8 | magic | ASCII `OSLOGSB` + 0x00 | ASCII `OSDMPSB` + 0x00 |
| 0x008 | 2 | version | 1 | 1 |
| 0x00A | 2 | flags | 0 | 0 |
| 0x00C | 4 | epoch | random from `esp_random()`, never 0 or 0xFFFFFFFF; same in both | same |
| 0x010 | 4 | unit\_sectors | 1 (a block) | 2,048 (a slot) |
| 0x014 | 4 | data\_start | 8 | 2,048 |
| 0x018 | 4 | unit\_count | `ring_blocks` | 255 |
| 0x01C | 4 | partition\_lba | 532,480 | 8,192 |
| 0x020 | 4 | partition\_sectors | as in the MBR | 524,288 |
| 0x024 | 4 | card\_sectors | from the CSD at format | same |
| 0x028 | 8 | format\_unix\_s | wall time of format, 0 if unknown | same |
| 0x030 | 6 | mac | base MAC of the hub that formatted | same |
| 0x036 | 2 | reserved | 0 | 0 |
| 0x038 | 32 | fw\_version | NUL-padded string | same |
| 0x058 | 32 | board | board name from the board descriptor | same |
| 0x078 | 388 | reserved | 0 | 0 |
| 0x1FC | 4 | crc32 | over 0x000 to 0x1FB | same |

A superblock is valid when four things hold: the magic matches its partition, the version is known, the CRC matches, and `partition_lba`, `partition_sectors` and `card_sectors` agree with the MBR and the card. If both copies are valid they must be byte-identical. Two valid copies that differ mean a format was interrupted, and the card is treated as foreign. If only one copy is valid, it is used. The firmware treats an unknown `version` as foreign, so no firmware ever writes to a layout it doesn't understand.

CRC-32 throughout this format is CRC-32/ISO-HDLC, the same as zlib's `crc32` and Python's `zlib.crc32`. Check vector: `"123456789"` gives 0xCBF43926. Test the firmware implementation against that vector before relying on a ROM routine, because ROM CRC helpers differ in init and final-XOR conventions.

## Block format

A ring block is one sector. It is the unit of writing, of integrity, and of loss.

| Offset | Size | Field | Value |
| --- | --- | --- | --- |
| 0x000 | 4 | magic | ASCII `OBLK` |
| 0x004 | 4 | epoch | must equal the superblock's epoch |
| 0x008 | 8 | seq | global sequence number within this epoch, starting at 0 |
| 0x010 | 4 | boot | boot counter within this epoch, starting at 0 |
| 0x014 | 2 | payload\_len | bytes of records used, 0 to 476 |
| 0x016 | 2 | flags | 0; reserved |
| 0x018 | 8 | uptime\_ms | uptime at the first record in this block |
| 0x020 | 476 | payload | records, then zero fill |
| 0x1FC | 4 | crc32 | over 0x000 to 0x1FB |

A block is valid only when all of the following hold:

1. The magic matches.
2. The epoch matches the superblock.
3. The CRC matches.
4. `payload_len` is at most 476.
5. `seq mod ring_blocks` equals the block's position in the ring.

Check 5 catches a block written to the wrong sector. It also lets the reader compute a block's lap without trusting anything outside the block.

`seq` and `boot` are 64 and 32 bits so neither wraps in the life of a card. Even at one block per millisecond, a u64 lasts beyond any card's endurance.

## Records

A block's payload holds whole records packed back to back. A record never spans two blocks. Large objects such as coredumps go to the dump partition, and a DUMP\_STORED record in the log points to them.

Record header, 4 bytes:

| Offset | Size | Field |
| --- | --- | --- |
| 0x0 | 1 | type |
| 0x1 | 1 | flags (bit 0: truncated) |
| 0x2 | 2 | len, bytes of data after this header |

Types:

| Type | Name | Data |
| --- | --- | --- |
| 0x00 | invalid | stops parsing of the block |
| 0x01 | LOG | `delta_ms` u32 (since the block's `uptime_ms`), `level` u8 (1 = error to 5 = verbose), `tag_len` u8, tag, then the message (UTF-8, no newline, no ANSI colour codes) |
| 0x02 | BOOT | `reset_reason` u8 (`esp_reset_reason_t`), `app_slot` u8, 2 reserved bytes, `mac` 6 bytes, then four u8-length-prefixed strings: fw\_version, board, variant, idf\_version |
| 0x03 | TIME\_SYNC | `delta_ms` u32, `unix_us` i64, `source` u8 (1 NTP, 2 backend, 3 RTC, 4 manual), 3 reserved bytes |
| 0x04 | DUMP\_STORED | `object_seq` u64, `slot` u32, `object_type` u16, 2 reserved bytes, `size` u32, `origin_boot` u32 |
| 0x05 | DROPPED | `records` u32 and `bytes` u32 dropped since the previous DROPPED record |
| 0x06 | MOUNT | `reason` u8 (1 = boot, 2 = card reinserted), 3 reserved bytes |
| 0x07 | UNMOUNT | none; written on a clean, user-requested removal |
| 0x08 to 0x7F | reserved | future core types |
| 0x80 to 0xFE | application | structured events, such as estop transitions or RF commands, defined in one registry in the repo |
| 0xFF | invalid | stops parsing of the block |

Rules:

- A LOG message is cut to fit the space left in the current block, or the space in an empty block if it can't fit there. When a message is cut, flag bit 0 is set.
- Readers skip unknown types using `len`. Adding a record type never changes the superblock `version`; changing the block or superblock layout does.
- The first record after every mount is MOUNT. When the mount happens at boot, BOOT comes right after it.
- DUMP\_STORED is written only after the slot it names has been committed and read back, so a log entry never points to a slot that doesn't hold that dump.

## Ring

A block's position is fixed by its sequence number: the block with sequence `s` lives at ring position `s mod N`, where `N = ring_blocks`. Its lap is `s div N`. The writer only ever writes the next position, so the ring always holds one run of blocks from the current lap (positions 0 to head) followed by blocks from the previous lap. Before the first wrap, those later positions are invalid instead.

Epoch. Every format draws a new random epoch and writes it into the superblock, and every block carries the epoch it was written under. A block whose epoch doesn't match is invalid. This is why formatting doesn't need to erase the ring: blocks from an earlier format, or a sector the card returns as junk, can never be mistaken for current data. The chance that two formats draw the same epoch is 1 in 2^32.

Write rules:

- Write position `(head + 1) mod N` with `seq = seq(head) + 1`, and nothing else.
- Never rewrite a finished block. The one exception is a block left torn by power loss or removal. It is invalid, so recovery treats it as unwritten and the writer writes it again.
- A partly filled block is written as it is, with a smaller `payload_len`. The next flush starts a new block and never appends to the old one.
- Consecutive blocks go out in one multi-sector write. A write that would cross the end of the ring is split in two.
- When the ring is full, the oldest data is overwritten.

Head recovery at mount uses binary search over lap numbers. Define `lap(p)` as `seq / N` for a valid block at position p, and −1 for an invalid one. The test "`lap(p)` equals the current lap K" is true for positions 0 to head and false after them. Blocks from the previous lap, never-written blocks and a torn block at head + 1 all fail it the same way, so a binary search finds the boundary.

1. Read position 0. If it is valid, K = lap(0). Binary search for the last position p with lap(p) = K; that position is the head.
2. If position 0 is invalid, read position N − 1. If that is valid, the head is N − 1: position 0 was torn at the start of a new lap.
3. If both are invalid, the ring is empty. The next write is seq 0, boot 0, at position 0.
4. Verify the result. The head must be valid. The block after it must not continue its sequence. A sample of 64 evenly spaced positions must all be consistent: lap K at or before the head, lap K − 1 or invalid after it.
5. If verification fails, which only happens when blocks in the middle of the ring are damaged, fall back to a full linear scan. The head is the valid block with the highest seq. Log a recovery note, and show the recovering state on the LED while the scan runs, since on a 32 GB card it takes minutes.

The normal path costs about 26 binary-search reads plus 64 probe reads, which is milliseconds. After recovery the writer continues at `seq(head) + 1`. The boot counter is `boot(head) + 1` when the mount happens at boot, or the in-RAM value when the same card is reinserted during a boot.

## Write path

The code that logs never waits on the card. Records go into a RAM queue, and one writer task turns them into blocks.

- Sources: the firmware's `OS_LOG*` macros pass level, tag and message straight to the log sink, so nothing has to be parsed. IDF component logs come in through `esp_log_set_vprintf`. The hook parses the standard `L (uptime) tag: message` prefix and strips ANSI colour codes. Logging from an ISR is not captured.
- Queue: a FreeRTOS no-split ring buffer, 16 KiB by default. Producers send with a zero timeout. When the queue is full, the record is dropped and atomic counters are incremented. The writer emits a DROPPED record with those counts before its next block.
- Writer task: low priority. It packs records into a buffer of 8 blocks (4 KiB).
- Flush triggers, whichever comes first:
  - the buffer is full;
  - 2 s have passed since the oldest unflushed record;
  - a record at error level arrives;
  - an explicit flush (unmount, or the shutdown handler registered with `esp_register_shutdown_handler`, bounded to 200 ms).
- Each flush finishes the current block and writes all finished blocks in one `sdmmc_write_sectors` call.
- Errors: on a failed write, reinit the card once and retry. A second failure marks the card failed, stops the writer, and shows the failure on the LED and in the portal. Records keep being dropped and counted until the card is reinserted.
- Timing: the SD card sits on its own peripheral (SDMMC, or SPI2/SPI3 for SPI mode), not on the flash bus. Card writes don't turn off the flash cache, so they don't disturb RMT or other ISRs the way internal flash writes do.

The flush interval decides how much is lost on a crash. The coredump covers the crash itself, and error-level records are flushed immediately. A quiet hub writes nothing, because blocks are only written when there are records to write.

## Card lifecycle

The hub never writes to a card it did not format, and it formats only when a person asks.

Insertion:

1. The card-detect GPIO changes state. It is debounced for 50 ms, and the event goes to the storage task.
2. The storage task initialises the host and the card (`sdmmc_card_init`) and reads the sector count from the CSD. Cards smaller than 1 GiB or larger than 2^32 − 1 sectors are refused.
3. It reads sector 0 and classifies the card (see MBR).
4. Ours: recover the log head (see Ring) and the dump head (see Dump partition), then mount. Write MOUNT, and BOOT too when this is the mount at boot. Start the writer, then copy any pending coredump.
5. Blank or foreign: the LED shows "needs formatting", and the portal shows the card's capacity and class. Nothing is written.

Format runs only from an explicit action: a portal button whose confirmation shows the capacity and says the card's contents will be destroyed, or the serial command `sd format --confirm`. It never runs on insertion. The steps, each sector read back after writing:

1. Draw the epoch.
2. Zero the reserved sectors D+2 to D+2,047 and L+2 to L+7.
3. Write the dump superblock backup (D+1), then the primary (D+0).
4. Write the log superblock backup (L+1), then the primary (L+0).
5. Write the MBR last.
6. Classify the card again. It must come out as ours, and then it is mounted.

That is about 2,060 sector writes, finished well within a second. An interrupted format leaves either no valid MBR or superblock copies that disagree, so the card classifies as foreign and the person formats it again. A half-formatted card is never taken for a valid one.

Safe removal: a portal button, the serial command `sd eject`, or a long press of the button. The hub stops accepting records into the card path, flushes, writes UNMOUNT, waits for the write to finish, shuts down the host, and shows "safe to remove" on the LED.

Surprise removal: when card detect deasserts, the writer stops immediately and the host is shut down. Queued records stay in RAM; if the queue fills, records are dropped and counted. A dump copy in progress is abandoned. The coredump is still in internal flash and is retried on the next mount.

Reinsertion goes through the full insertion path again, because it may be a different card. The hub writes MOUNT with reason 2. The boot counter carries on if the card's epoch matches the card that was removed. Otherwise it is that card's head boot + 1.

Card detect is required. A board without a card-detect pin is not supported, because removal could not be told apart from a dead card.

## Dump partition

The dump partition holds 255 fixed 1 MiB slots. Each slot is a header sector followed by up to 2,047 sectors (1,047,552 bytes) of data. One object fills one slot; an object larger than a slot is refused. Today's coredump partition is 64 KiB, so a slot has 16 times that as headroom.

Slot header:

| Offset | Size | Field | Value |
| --- | --- | --- | --- |
| 0x000 | 4 | magic | ASCII `ODMP` |
| 0x004 | 4 | epoch | must equal the superblock's epoch |
| 0x008 | 8 | seq | object sequence within this epoch, starting at 0 |
| 0x010 | 4 | stored\_boot | log boot counter of the boot that stored it |
| 0x014 | 4 | origin\_boot | log boot counter of the boot that crashed (stored\_boot − 1 for a coredump) |
| 0x018 | 2 | object\_type | 1 = coredump ELF |
| 0x01A | 2 | flags | 0 |
| 0x01C | 4 | size | bytes of data |
| 0x020 | 4 | data\_crc32 | over the `size` data bytes |
| 0x024 | 1 | reset\_reason | `esp_reset_reason()` at the storing boot |
| 0x025 | 3 | reserved | 0 |
| 0x028 | 8 | stored\_uptime\_ms | uptime when stored |
| 0x030 | 32 | app\_elf\_sha256 | crashed app's ELF SHA-256, from `esp_core_dump_get_summary` |
| 0x050 | 32 | stored\_fw\_version | firmware that stored it |
| 0x070 | 32 | board | board name from the board descriptor |
| 0x090 | 4 | exc\_pc | faulting PC from the summary |
| 0x094 | 16 | exc\_task | task name from the summary |
| 0x0A4 | 344 | reserved | 0 |
| 0x1FC | 4 | crc32 | over 0x000 to 0x1FB |

The object with sequence `s` lives in slot `s mod 255`, so the oldest dump is overwritten once 255 are stored. At mount the firmware reads all 255 headers, which is 255 single-sector reads and takes tens of milliseconds. A slot counts only if its magic, epoch and header CRC match and `seq mod 255` equals its index. The head is the valid slot with the highest seq; if no slot is valid, the next seq is 0. The firmware doesn't check data CRCs at mount; the reader does.

Storing a coredump runs after the mount at boot, before WiFi starts. It also runs at any later mount while a dump is still waiting in internal flash.

1. `esp_core_dump_image_check()` reports a valid dump, and `esp_core_dump_image_get()` gives its address and size.
2. Dedupe: if the head slot already has the same `app_elf_sha256`, `size` and data CRC, skip to step 7. This covers a crash between commit and the log record.
3. Target slot `(head_seq + 1) mod 255`. Write a zeroed sector over its header, which invalidates the old object in that slot.
4. Copy the data from the coredump partition in 4 KiB reads (`esp_partition_read`) to slot sectors 1 onward, using multi-sector writes and computing the CRC as it goes.
5. Write the header with `data_crc32`. This is the commit.
6. Read back the header and the data, and check both CRCs.
7. Append DUMP\_STORED to the log and flush it.
8. Erase the dump from internal flash (`esp_core_dump_image_erase()`), but only after every sink has it. If backend upload is configured, wait until the upload has succeeded too.

Power loss or removal during steps 3 to 6 leaves the slot with an invalid header or a data CRC mismatch. Readers skip it, the dump is still in flash, and the next mount retries into the same slot, because the head hasn't moved.

## Timestamps

Current boards have no RTC, so every time on the card is uptime first, with wall-clock time added when the hub learns it.

- Each block carries `boot` and `uptime_ms`, and each LOG record adds `delta_ms`, so every record has an exact (boot, uptime) position.
- TIME\_SYNC is written whenever the hub learns wall time (an SNTP sync, or time from the backend). While synced, it is also written every 6 hours, to bound RTC drift.
- The reader converts each record in a boot using the nearest TIME\_SYNC in the same boot. Uptime is continuous within a boot, so records before the first sync are back-filled from it.
- A boot with no TIME\_SYNC gets relative time only. It is printed as `b<boot>+<uptime>` and marked unsynced. Times are never guessed across boots, because the downtime between them is unknown.
- A future board with an RTC writes TIME\_SYNC with source 3 at every boot.

## PC reader

`tools/sdlog/sdlog.py` reads cards. It is Python 3, standard library only, and lives in the firmware repo, not on the card. It parses with the same rules the firmware uses, and it is tested against golden card images that a host test of the firmware writer produces.

Commands:

- `sdlog.py list`: removable disks, and whether each one looks like an OpenShock card. Needs admin.
- `sdlog.py image <device> <dir>`: copies the MBR, both superblocks, the valid dump slots and the used part of the log ring into image files. Before the ring has wrapped, "used" means blocks 0 to head only, so a fresh 128 GB card images in seconds. Needs admin; this is the only privileged step.
- `sdlog.py dump <device-or-image-dir> <outdir>`: parses and writes the outputs below. Runs unprivileged on an image.

Raw device paths:

| OS | Path | Notes |
| --- | --- | --- |
| Linux | `/dev/sdX` or `/dev/mmcblkN` | run with sudo; nothing is auto-mounted |
| macOS | `/dev/rdiskN` | run with sudo; click Ignore on the "not readable" dialog, never Initialize |
| Windows | `\\.\PhysicalDriveN` | run from an administrator terminal; no drive letter is assigned |

Logs: the reader classifies the card and recovers the head with the firmware's algorithm. The oldest block is at head + 1 if that position holds a valid block from the previous lap; otherwise it is position 0. From there it walks to the head, checks every block, and reports seq gaps (torn or damaged blocks) separately from blocks that belong to another epoch.

Dumps: each valid slot has its data CRC checked and is written out as `coredumps/b<origin_boot>_<seq>.elf`. The reader matches `app_elf_sha256` against the per-variant release ELFs (`OpenShock_<variant>_<version>.elf`). The dump then decodes with `esp-coredump info_corefile --core <dump.elf> --core-format elf <app.elf>`.

Outputs:

- `log.txt`: one line per record, with wall time (or `b<boot>+<uptime>` when unsynced), level, tag and message.
- `log.jsonl`: the same records with every field, including application record types.
- `coredumps/*.elf`: one file per valid dump.
- `summary.txt`: card and format info; every boot with its reset reason and firmware version; the time range, synced and unsynced; dropped-record totals; invalid blocks; unclean removals (MOUNT reason 2 with no UNMOUNT before it); and a dump list with task and PC.

## Hardware and capacity

The firmware uses the IDF SD drivers (`sdmmc_host` or `sdspi_host`, `sdmmc_card_init`, `sdmmc_read_sectors`, `sdmmc_write_sectors`) and nothing above them. Neither `esp_vfs_fat` nor FatFs is linked.

| Chip | Boards | Bus | Notes |
| --- | --- | --- | --- |
| ESP32-S3 | Core V1, Core V2, Xiao S3, Lolin S3, Lolin S3 Mini, S3 Zero | SDMMC, 4-bit or 1-bit | routed through the GPIO matrix, so any free pins work |
| ESP32 | Firebeetle 2, NodeMCU-32S, PiShock 2023, PiShock Lite, D1 Mini | SPI mode | SDMMC is fixed to GPIO 2, 4, 12, 13, 14 and 15. 2, 12 and 15 are strapping pins, and GPIO 12 sets the flash voltage on some modules. |
| ESP32-S2 | Lolin S2 Mini | SPI mode | no SDMMC host |
| ESP32-C3 | Xiao C3 | SPI mode | no SDMMC host; few free pins |

Electrical requirements: 10 kΩ pull-ups on CMD and DAT0 to DAT3, a card-detect pin, and decoupling at the socket for write current peaks of 100 to 200 mA. Industrial pSLC cards are recommended for unattended logging. Consumer cards work but wear out sooner under constant small writes. Throughput is far beyond what logging needs: about 1 to 2 MB/s in SPI mode at 20 MHz, and 10 MB/s or more on 4-bit SDMMC at 40 MHz.

Approximate log retention. The log partition is the card minus 260 MiB, and 93 % of each block is payload. The rates count record bytes.

| Card | Info level, about 5 MB/day | Verbose, about 200 MB/day |
| --- | --- | --- |
| 8 GB | about 3.9 years | about 36 days |
| 32 GB | about 16 years | about 5 months |
| 128 GB | far beyond card endurance | about 1.6 years |

The 2 s flush can leave a block partly filled. A hub that logs slowly but without pause therefore uses up to about 20 MB/day of blocks, whatever its record rate. Dumps are independent of the log partition: the last 255 are always kept.

## Open decisions

- [ ] When the log ring is full, overwrite the oldest data (proposed) or stop?
- [ ] Dump partition at 256 MiB with 255 slots of 1 MiB, or smaller slots to keep more dumps?
- [ ] Is a 2 s flush interval with an immediate flush on errors the right trade between loss and wasted blocks?
- [ ] Default card log level: info (proposed)?
- [ ] Which boards get a slot, and do the S3 boards use SDMMC or SPI mode for a single code path?
- [ ] Coredump erase policy when backend upload is configured but the hub stays offline for weeks.
- [ ] Add an RTC on the next board revision?
- [ ] Who owns the registry of application record types (0x80 to 0xFE)?
- [ ] Is a long press free for eject on every board, or is it portal and serial only?

## Implementation status

What `feat/crash-log-reporting` builds (`components/diagnostics/src/sdcard`, `tools/sdlog`), and where it departs from
the design above:

- **Card I/O** follows the design: ESP-IDF's `sdmmc` protocol layer (`sdmmc_card_init`, `sdmmc_read_sectors`,
  `sdmmc_write_sectors`) on either host (`SdHostCard.cpp`), chosen at build time. SDMMC (1- or 4-bit) is offered
  only on chips whose SD host goes through the GPIO matrix (ESP32-S3) and is the default there; every other chip
  uses SPI mode on SPI2. Card detect stays in `SdStorage` (debounced), not in the driver.
- **Dump copy reads** use `esp_flash_read` on the address `esp_core_dump_image_get()` returns, in 512-byte reads,
  rather than 4 KiB `esp_partition_read`s; data still goes to the card in 8-sector multi-block writes.
- **Configuration.** Bus, pins, card-detect polarity, clock limit and queue size are Kconfig options under
  `CONFIG_OPENSHOCK_DIAG_SDCARD` (off by default). Card detect is required at build time.
- **Not built yet:** the LED states (needs formatting, recovering, safe to remove, failed) and everything in the
  portal (status, format and eject buttons); eject by long press. Only the serial commands exist: `sd status`,
  `sd format --confirm`, `sd eject`.
- **TIME_SYNC** has an API (`SdCard::RecordTimeSync`) but no caller: the firmware has no wall-clock source today. Once
  one sync arrives, the writer repeats it every 6 hours and after every mount.
- **BOOT `variant`** carries the chip name (`OPENSHOCK_FW_CHIP`); the build variant id lives only in the build scripts.
- **Mount at a later insertion** writes BOOT too when the card hasn't had one this boot (MOUNT reason 2), so every boot
  number on a card has its BOOT record.
- **Level mapping.** `OS_LOGN` is stored as info (3), a panic (`OS_PANIC*`, printed with the letter `C`) as error (1).
  Tags are cut to 32 bytes.
- **`sdlog.py list`** enumerates removable disks through sysfs (Linux), `diskutil` (macOS) and PowerShell
  `Win32_DiskDrive` (Windows); only the Linux path has been run.
