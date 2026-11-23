---
kind: changed
---
Enforce a minimum duration on the serial rftransmit command and ignore the duration of a stop

## Release Note
Serial rftransmit command checks the duration differently

The `rftransmit` serial command now refuses a `durationMs` shorter than 300 ms, and no longer needs a `durationMs` at all when the type is `stop`.

- The 300 ms length of a stop is now the `OPENSHOCK_RF_MIN_COMMAND_DURATION_MS` Kconfig option, and the serial command uses the same value as its minimum
- For a stop, `durationMs` may be missing or invalid and is not read

## Notices
- warning: **The serial `rftransmit` command rejects durations under 300 ms.** Scripts that send shorter commands now get an error instead of a transmission. Send at least 300 ms.
