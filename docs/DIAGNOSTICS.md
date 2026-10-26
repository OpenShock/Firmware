# Diagnostics: crash dumps and logs

The `diagnostics` component (`components/diagnostics`) keeps what a hub knows about its own failures. It sends
nothing anywhere unless the owner turns uploads on.

## What is kept on the hub

- **Crash dumps.** ESP-IDF's panic handler writes an ELF coredump to the `coredump` partition. The next boot logs
  that one is stored. `CONFIG_APP_RETRIEVE_LEN_ELF_SHA=64` makes the dump record the crashed build's full ELF
  SHA-256, which says which release ELF decodes it.
- **Logs.** `OS_LOG*` / `OS_PANIC*` lines, and ESP-IDF's own (through `esp_log_set_vprintf`), at the log upload level
  or more severe go into a ring buffer (`CONFIG_OPENSHOCK_DIAG_LOG_BUFFER_SIZE`, 4 KiB by default). Errors and panics
  are always kept, even with log upload set to `none`. Lines logged by the upload task itself are not, so an upload
  never produces the next one. The ring lives in `__NOINIT_ATTR` memory, which a software reset leaves alone, so the
  lines that led up to a panic or watchdog reset are still there on the next boot. A power cycle clears it. An
  abnormal reset adds a `[C][boot] previous boot ended by ...` line. At info level or below, a busy hub fills the
  ring well within the minute between uploads; the upload then starts with a `[... N bytes lost ...]` line.

A dump is erased from flash only once every sink that should have it does: the SD card when SD support is built in
and configured, and the backend when coredump upload is on. With neither, it stays for `idf.py coredump-info` or
`diag erase`.

## Uploads (opt-in)

Both uploads are off by default. The settings are stored in NVS (namespace `diag`) and set from the serial console:

| Command | |
| --- | --- |
| `diag coredump [true\|false]` | get or set coredump upload |
| `diag logs [none\|error\|warn\|info\|debug\|verbose]` | get or set the least severe log level uploaded; `none` (the default) uploads no logs |
| `diag domain [<domain>\|backend]` | get or set where uploads go: the linked backend (the default), or another host, optionally with `:port` |
| `diag status` | stored dump, pending log bytes, last upload result |
| `diag upload` | upload what is pending at the next chance, skipping a retry wait |
| `diag erase` | erase the stored dump now |

Only official firmware uploads: images built by OpenShock's CI in `OpenShock/Firmware` for a release tag, the nightly
develop build or a manual dispatch (`OPENSHOCK_FW_OFFICIAL`, set by `scripts/gen_env_header.py`). A local build, a
fork's CI build or a pull request build (even one built in `OpenShock/Firmware`) uploads nothing to the linked backend,
whatever the settings say, so crashes of code that exists only on someone's machine don't land there by accident.
Such a build still uploads to an override domain (`diag domain`), since setting one is deliberate. It's a guard
against accidents, not a security boundary: anyone can set the define. The backend can tell release crashes for
certain by matching `X-OpenShock-Crash-Elf-Sha256` against the release ELFs. `diag status` reports `uploadsAllowed`.

Uploads run on a short-lived task, only while the gateway connection is up. Logs go at most once a minute.
After a failure the retry wait doubles from 1 minute up to 1 hour.

Endpoints, on the linked backend's domain, or on the override domain when one is set. The hub's `DeviceToken` goes
only to the linked backend: an override domain that isn't the backend gets the uploads without it, identified only by
the headers below. A hub that isn't linked uploads only to an override domain.

| | |
| --- | --- |
| `POST /1/device/diagnostics/coredump` | `application/octet-stream`, the raw ELF coredump |
| `POST /1/device/diagnostics/logs` | `text/plain; charset=utf-8`, log lines oldest first, `[uptime][L][tag] message` |

Both carry `X-OpenShock-Firmware`, `-Board`, `-Chip` and `-Reset-Reason` headers. The coredump also carries
`X-OpenShock-Crash-Elf-Sha256`, `-Crash-Task` and `-Panic-Reason` when the dump has them. After an OTA update the
crashed build is not the running one, so the backend must pick the ELF by `Crash-Elf-Sha256`. Any 2xx counts as
accepted. The backend side of these endpoints does not exist yet.

## SD card

Builds with `CONFIG_OPENSHOCK_DIAG_SDCARD=y` also write every log record and every coredump to a microSD card, with no
filesystem. The format, the card lifecycle and the PC reader (`tools/sdlog/sdlog.py`) are in [SDCARD.md](SDCARD.md).
The card goes through ESP-IDF's sdmmc driver: on the SDMMC host (1- or 4-bit, ESP32-S3 only, its default there) or in
SPI mode on SPI2 (every chip). The bus and pins are Kconfig symbols under the option, so they exist only in builds that
turn it on. SDMMC, 4-bit:

```
CONFIG_OPENSHOCK_DIAG_SDCARD=y
CONFIG_OPENSHOCK_DIAG_SDCARD_BUS_SDMMC=y
CONFIG_OPENSHOCK_DIAG_SDCARD_CLK_GPIO=12
CONFIG_OPENSHOCK_DIAG_SDCARD_CMD_GPIO=11
CONFIG_OPENSHOCK_DIAG_SDCARD_D0_GPIO=13
CONFIG_OPENSHOCK_DIAG_SDCARD_D1_GPIO=14
CONFIG_OPENSHOCK_DIAG_SDCARD_D2_GPIO=9
CONFIG_OPENSHOCK_DIAG_SDCARD_D3_GPIO=10
CONFIG_OPENSHOCK_DIAG_SDCARD_CD_GPIO=8
```

SPI mode:

```
CONFIG_OPENSHOCK_DIAG_SDCARD=y
CONFIG_OPENSHOCK_DIAG_SDCARD_BUS_SPI=y
CONFIG_OPENSHOCK_DIAG_SDCARD_SCK_GPIO=18
CONFIG_OPENSHOCK_DIAG_SDCARD_MOSI_GPIO=23
CONFIG_OPENSHOCK_DIAG_SDCARD_MISO_GPIO=19
CONFIG_OPENSHOCK_DIAG_SDCARD_CS_GPIO=5
CONFIG_OPENSHOCK_DIAG_SDCARD_CD_GPIO=4
```

Console: `sd status`, `sd format --confirm`, `sd eject`.

## Tests

`components/diagnostics/host_test` covers the error ring and the whole SD layout (format, classification, head
recovery, record packing, dump slots) against an in-memory card. Its "Golden card image" case writes
`tools/sdlog/testdata/golden` when `OPENSHOCK_SDLOG_GOLDEN_DIR` is set; `scripts/tests/test_sdlog.py` checks the
reader against that image.
