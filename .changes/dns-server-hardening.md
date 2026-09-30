---
kind: fixed
---
Harden the captive-portal DNS responder against malformed queries

## Release Note
Fixed a flaw in the captive-portal DNS responder. A specially crafted DNS query sent to the hub's setup network could corrupt memory and crash it, or make it leak a small amount of adjacent memory back to the sender. Answering a query now checks that the reply actually fits before building it.

The responder is also stricter about what it will reply to: it ignores DNS replies (rather than answering them, which let two hubs talk past each other indefinitely), ignores anything that isn't a standard single-question lookup, and rejects malformed or over-long names. Answers now carry a one-minute cache lifetime instead of zero, so devices stop re-asking the same question during setup.

Setup-network addresses are also validated properly now, so a malformed address is rejected instead of being silently truncated into a different one. Leaving setup mode shuts the responder down cleanly, and a network error while it's running no longer risks pinning a CPU core.
