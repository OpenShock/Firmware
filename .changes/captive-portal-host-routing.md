---
kind: fixed
---
Route captive portal requests by Host header, and only on the portal AP

## Release Note
More reliable captive portal detection, and the portal works normally on your home network

Phones and computers joining the hub's setup network now reliably open the sign-in screen, whatever address they check. When the hub is reached over your home network, including by its hostname, the portal behaves like a normal web page and no longer redirects.

- Any request to another website over the setup network is sent to the portal, not just a fixed list of check addresses
- Unknown pages on the hub show "Not Found" instead of jumping back to the start page
- The captive portal API also answers at /captiveportal/api, and only on the setup network
- The setup network's DNS server no longer answers queries from your home network
