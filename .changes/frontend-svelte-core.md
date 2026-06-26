---
kind: chore
---
Consume the shared @openshock/svelte-core library in the captive portal

- Replace the vendored shadcn UI, components, theme, and utilities with `@openshock/svelte-core` (^0.2.2), imported by package name through its JS barrels
- Import the library's `theme.css` (shared design tokens + self-registering Tailwind source) instead of an inlined theme copy
- Use the library's self-contained `LightSwitch`/`Toaster` and color-scheme state, removing the local `LightSwitch` and `ColorSchemeStore`
