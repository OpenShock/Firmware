#include "DNSPacket.h"

const char* const TAG = "DNSPacket";

#include "Logging.h"

#include <cstring>

namespace {
  constexpr size_t DNS_HEADER_SIZE  = 12;
  constexpr size_t DNS_MAX_NAME_LEN = 255;
  constexpr size_t DNS_ANSWER_SIZE  = 16;
  constexpr uint32_t DNS_ANSWER_TTL = 60;  // seconds; a zero TTL makes clients re-query on every probe

  // Header flag bits, as a big-endian uint16 read from bytes [2..3].
  constexpr uint16_t DNS_FLAG_QR     = 0x8000;  // set on responses
  constexpr uint16_t DNS_FLAG_AA     = 0x0400;  // authoritative answer
  constexpr uint16_t DNS_FLAG_RD     = 0x0100;  // recursion desired
  constexpr uint16_t DNS_OPCODE_MASK = 0x7800;

  constexpr uint16_t DNS_TYPE_A   = 1;
  constexpr uint16_t DNS_CLASS_IN = 1;

  // The answer's owner name is a compression pointer to the question, which
  // always begins immediately after the header.
  static_assert(DNS_HEADER_SIZE == 0x0C, "the answer's 0xC00C pointer assumes the question starts at offset 12");

  // Walks the QNAME at `data` and returns its encoded length including the
  // terminating zero byte, or 0 if the name is malformed, compressed, or
  // longer than a name may legally be.
  //
  // Compression pointers are rejected rather than accepted: in a question
  // there is nothing valid for one to point back at, and echoing it would
  // leave the answer's own 0xC00C pointer aimed at a dangling chain.
  size_t ParseQName(const uint8_t* data, size_t avail)
  {
    size_t offset = 0;

    while (offset < avail) {
      uint8_t label = data[offset];

      if (label == 0) {
        return offset + 1;  // consume the terminator
      }

      // Only ordinary labels are accepted: 0b11 marks a compression pointer
      // and 0b01/0b10 are reserved, neither of which is a length.
      if ((label & 0xC0) != 0) {
        return 0;
      }

      if (offset + 1 + label > avail) {
        return 0;  // label overruns the packet
      }

      offset += static_cast<size_t>(label) + 1;

      // Leave room for the terminator, so the encoded name including it stays
      // within DNS_MAX_NAME_LEN.
      if (offset >= DNS_MAX_NAME_LEN) {
        return 0;
      }
    }

    return 0;  // ran off the end without a terminator
  }
}  // namespace

size_t OpenShock::DNSPacket::BuildResponse(uint8_t* packet, size_t len, size_t capacity, const uint8_t (&ip)[4])
{
  if (len < DNS_HEADER_SIZE) {
    return 0;  // runt packet
  }

  // A datagram that fills the buffer was almost certainly truncated by
  // recvfrom, and we would be answering a question we never fully saw. No
  // legitimate query comes close: 12 + 255 + 4 is the most one can be.
  if (len >= capacity) {
    return 0;
  }

  uint16_t flags = static_cast<uint16_t>((packet[2] << 8) | packet[3]);

  // Ignore responses: answering one lets two of these servers keep each
  // other busy indefinitely, and makes us usable as a reflector.
  if ((flags & DNS_FLAG_QR) != 0) {
    return 0;
  }

  // Only standard queries. Any other opcode would otherwise get a NOERROR
  // reply, carrying its own opcode back, for a request we never handled.
  if ((flags & DNS_OPCODE_MASK) != 0) {
    return 0;
  }

  // A captive portal only ever needs to answer a single question.
  uint16_t qdcount = static_cast<uint16_t>((packet[4] << 8) | packet[5]);
  if (qdcount != 1) {
    return 0;
  }

  size_t qnameLen = ParseQName(packet + DNS_HEADER_SIZE, len - DNS_HEADER_SIZE);
  if (qnameLen == 0) {
    return 0;  // malformed, compressed or oversized name
  }

  // QTYPE(2) and QCLASS(2) follow the name and must both be present.
  if (len - DNS_HEADER_SIZE - qnameLen < 4) {
    return 0;
  }
  size_t questionEnd = DNS_HEADER_SIZE + qnameLen + 4;

  const uint8_t* qfixed = packet + DNS_HEADER_SIZE + qnameLen;
  uint16_t qtype        = static_cast<uint16_t>((qfixed[0] << 8) | qfixed[1]);
  uint16_t qclass       = static_cast<uint16_t>((qfixed[2] << 8) | qfixed[3]);

  bool answering = qtype == DNS_TYPE_A && qclass == DNS_CLASS_IN;

  // A question that fills the buffer leaves no room for the answer; without
  // this the memcpy below would run past the end of packet.
  size_t respLen = questionEnd + (answering ? DNS_ANSWER_SIZE : 0);
  if (respLen > capacity) {
    OS_LOGW(TAG, "Query leaves no room for a response (%zu bytes), ignoring", respLen);
    return 0;
  }

  // Reply in place: the transaction ID and question are already correct.
  uint16_t respFlags = DNS_FLAG_QR | DNS_FLAG_AA | (flags & DNS_FLAG_RD);
  packet[2]          = static_cast<uint8_t>(respFlags >> 8);
  packet[3]          = static_cast<uint8_t>(respFlags);
  packet[6]          = 0x00;  // ANCOUNT hi
  packet[7]          = answering ? 0x01 : 0x00;
  packet[8]          = 0x00;  // NSCOUNT
  packet[9]          = 0x00;
  packet[10]         = 0x00;  // ARCOUNT
  packet[11]         = 0x00;

  if (answering) {
    // clang-format off
    const uint8_t answer[DNS_ANSWER_SIZE] = {
      0xC0, 0x0C,                                  // name pointer to offset 12 (the question)
      0x00, 0x01,                                  // TYPE  A
      0x00, 0x01,                                  // CLASS IN
      static_cast<uint8_t>(DNS_ANSWER_TTL >> 24),  // TTL
      static_cast<uint8_t>(DNS_ANSWER_TTL >> 16),
      static_cast<uint8_t>(DNS_ANSWER_TTL >> 8),
      static_cast<uint8_t>(DNS_ANSWER_TTL),
      0x00, 0x04,                                  // RDLENGTH 4
      ip[0], ip[1], ip[2], ip[3],                  // RDATA: the fixed response IP
    };
    // clang-format on
    memcpy(packet + questionEnd, answer, sizeof(answer));
  }

  return respLen;
}
