#pragma once

#include <cstddef>
#include <cstdint>

// Pure (socket-free) half of the captive-portal DNS responder, split out of the
// DNSServer task so the parse/build logic can be host-tested with crafted packets.
namespace OpenShock::DNSPacket {
  constexpr size_t MAX_PACKET = 512;

  /// Turns the query in packet[0..len) into a reply, in place, answering an A/IN
  /// question with `ip` (network order, i.e. a.b.c.d) and any other type/class with
  /// an empty answer. capacity is the size of the buffer behind packet.
  ///
  /// Returns the reply length (never more than capacity), or 0 if the packet must
  /// be ignored: runt or possibly truncated datagrams, responses, non-standard
  /// opcodes, anything but exactly one question, malformed, compressed or
  /// over-long names, a missing QTYPE/QCLASS, or no room left for the reply. The
  /// buffer is only written when a reply length is returned.
  size_t BuildResponse(uint8_t* packet, size_t len, size_t capacity, const uint8_t (&ip)[4]);
}  // namespace OpenShock::DNSPacket
