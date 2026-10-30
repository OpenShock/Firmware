---
kind: fixed
---
Split the captive portal instance into focused units and tighten static file serving

## Release Note
The setup portal's web server follows HTTP more closely

It now honours the browser's Accept-Encoding header instead of always sending compressed files, answers HEAD requests, repeats cache headers on 304 replies, and returns proper error codes for malformed or unservable paths instead of redirecting or dropping the connection.

- Content negotiation: gzip-capable clients get the pre-compressed asset, a plain copy is served when the image has one, and a gzip-only asset requested by a client that excludes gzip gets 406 instead of a redirect back to itself
- Directory paths, and files that fail to open, no longer close the connection without a reply
- A ".." in the request path is refused with 400
- The static image is accepted with either index.html.gz or index.html
- Internally, the captive portal instance is split into WebSocket, static file, API and HTTP helper units, and the StaticFs facade is folded into LfsPartition
