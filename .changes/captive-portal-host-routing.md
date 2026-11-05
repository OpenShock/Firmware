---
kind: fixed
---
Route captive portal requests by Host header, and serve the portal only on its own network

## Release Note
More reliable captive portal detection

Phones and computers joining the hub's setup network now reliably open the sign-in screen, whatever address they check. The setup portal and its DNS server are now only reachable from the setup network, not from your home network.

- Any request to another website over the setup network is sent to the portal, not just a fixed list of check addresses
- Unknown pages on the hub show "Not Found" instead of jumping back to the start page
- The captive portal API also answers at /captiveportal/api

## Notices
- note: **The setup portal no longer answers on your home network.** Opening the hub's home network address in a browser now fails. Join the hub's setup network to reach the portal.
