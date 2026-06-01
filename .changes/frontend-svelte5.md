---
type: minor
---

Rewrite frontend to Svelte 5 + Vite

- Replace SvelteKit with plain Svelte 5 + Vite for the captive portal frontend
- Migrate to Svelte 5 runes ($state, $derived, $effect)
- Single-file HTML output via vite-plugin-singlefile for LittleFS

## Summary
The captive portal frontend has been completely rewritten using Svelte 5 with its new runes-based reactivity system, replacing the previous SvelteKit setup. The UI is now built as a single HTML file for efficient storage on the device.

## Notices
- info: The frontend was fully rewritten, but since it is served directly from the device's filesystem, no user action is required. The update is applied automatically with the firmware flash.
