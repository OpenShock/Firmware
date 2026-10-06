---
kind: changed
pr: 522
---
Captive portal UI/UX pass: Enter-to-submit, confirmations, synced settings and a desktop layout

## Release Note
The setup portal is easier to use on both phones and computers

Forms now submit with Enter, WiFi dialogs stay open when saving fails, and forgetting a network or unlinking your account asks for confirmation first. OTA update settings and toggles now always show what the hub actually saved. On a larger screen the portal uses the extra space, with a sidebar in Advanced Setup.

- Fix OTA settings not reflecting saved values, which made toggles impossible to turn back without reloading
- Toggles only move once the hub accepts the change, and revert if saving fails
- Submit dialogs and inline fields with Enter; WiFi dialogs get a Cancel button and close only on success
- Confirm before forgetting a WiFi network or unlinking the account
- Report failed OTA requests and test commands sent while disconnected
- Advanced Setup reuses the guided steps, so the EStop toggle is now also in the guided hardware step
- Pair code hint now points to Hubs > Pair on the OpenShock website
- Desktop layout: sidebar navigation in Advanced Setup, wider guided wizard, landing page cards
- Mobile shell no longer over-scrolls by the height of the browser's URL bar
- Replace raw palette colours with theme tokens and fix low-contrast status text
