// DNSPacket::BuildResponse is the captive-portal responder's whole parse/build
// step: it turns a received query into a reply in place or tells the task to drop
// it. These tests feed it crafted packets in buffers sized exactly to the given
// capacity (so a sanitizer build catches any write past the end), and pad the
// bytes past the packet with garbage the parser must never look at.
#include "unity.h"

#include "DNSPacket.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

using namespace OpenShock;

namespace {
  constexpr uint8_t kIp[4] = {10, 10, 10, 1};

  constexpr size_t kHeaderSize = 12;
  constexpr size_t kAnswerSize = 16;

  constexpr uint16_t kFlagQR = 0x8000;
  constexpr uint16_t kFlagAA = 0x0400;
  constexpr uint16_t kFlagTC = 0x0200;
  constexpr uint16_t kFlagRD = 0x0100;
  constexpr uint16_t kFlagRA = 0x0080;
  constexpr uint16_t kFlagAD = 0x0020;
  constexpr uint16_t kFlagCD = 0x0010;

  constexpr uint16_t kTypeA    = 1;
  constexpr uint16_t kTypeAAAA = 28;
  constexpr uint16_t kTypeANY  = 255;
  constexpr uint16_t kClassIN  = 1;
  constexpr uint16_t kClassCH  = 3;

  void PutU16(std::vector<uint8_t>& out, uint16_t v)
  {
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
  }

  uint16_t GetU16(const uint8_t* p)
  {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
  }

  // Encodes a dotted name ("a.bc") as length-prefixed labels plus the terminator.
  std::vector<uint8_t> EncodeName(const std::string& dotted)
  {
    std::vector<uint8_t> out;
    size_t start = 0;
    while (start < dotted.size()) {
      size_t dot = dotted.find('.', start);
      if (dot == std::string::npos) {
        dot = dotted.size();
      }
      out.push_back(static_cast<uint8_t>(dot - start));
      out.insert(out.end(), dotted.begin() + start, dotted.begin() + dot);
      start = dot + 1;
    }
    out.push_back(0);
    return out;
  }

  std::vector<uint8_t> Header(uint16_t id, uint16_t flags, uint16_t qdcount, uint16_t arcount = 0)
  {
    std::vector<uint8_t> out;
    PutU16(out, id);
    PutU16(out, flags);
    PutU16(out, qdcount);
    PutU16(out, 0);  // ANCOUNT
    PutU16(out, 0);  // NSCOUNT
    PutU16(out, arcount);
    return out;
  }

  std::vector<uint8_t> Query(const std::vector<uint8_t>& name, uint16_t qtype = kTypeA, uint16_t qclass = kClassIN, uint16_t flags = kFlagRD, uint16_t qdcount = 1, uint16_t id = 0xBEEF)
  {
    std::vector<uint8_t> out = Header(id, flags, qdcount);
    out.insert(out.end(), name.begin(), name.end());
    PutU16(out, qtype);
    PutU16(out, qclass);
    return out;
  }

  std::vector<uint8_t> Query(const char* dotted, uint16_t qtype = kTypeA, uint16_t qclass = kClassIN, uint16_t flags = kFlagRD, uint16_t qdcount = 1, uint16_t id = 0xBEEF)
  {
    return Query(EncodeName(dotted), qtype, qclass, flags, qdcount, id);
  }

  // A heap buffer of exactly `capacity` bytes holding `pkt`, with the rest filled
  // with `pad` so a parser that reads past the packet sees non-zero junk.
  std::vector<uint8_t> Buffer(const std::vector<uint8_t>& pkt, size_t capacity, uint8_t pad = 0xA5)
  {
    std::vector<uint8_t> buf(capacity, pad);
    std::copy_n(pkt.begin(), pkt.size() < capacity ? pkt.size() : capacity, buf.begin());
    return buf;
  }

  size_t Run(std::vector<uint8_t>& buf, size_t len)
  {
    return DNSPacket::BuildResponse(buf.data(), len, buf.size(), kIp);
  }

  // Asserts the packet is dropped and the buffer is left untouched.
  void AssertIgnored(const std::vector<uint8_t>& pkt, size_t capacity = DNSPacket::MAX_PACKET)
  {
    std::vector<uint8_t> buf    = Buffer(pkt, capacity);
    std::vector<uint8_t> before = buf;
    TEST_ASSERT_EQUAL_UINT(0, Run(buf, pkt.size()));
    TEST_ASSERT_EQUAL_MEMORY(before.data(), buf.data(), buf.size());
  }

  // A name of `total` encoded bytes (labels plus terminator) built from labels
  // of at most 63 bytes, the longest a label may be.
  std::vector<uint8_t> NameOfLength(size_t total)
  {
    std::vector<uint8_t> out;
    size_t remaining = total - 1;  // the terminator
    while (remaining > 0) {
      size_t label = remaining - 1 > 63 ? 63 : remaining - 1;
      if (remaining - (label + 1) == 1) {
        label--;  // a lone byte left over could not hold a label, so share it out
      }
      out.push_back(static_cast<uint8_t>(label));
      out.insert(out.end(), label, 'a');
      remaining -= label + 1;
    }
    out.push_back(0);
    return out;
  }
}  // namespace

TEST_CASE("A query is answered with the AP address", "[dns_server][answer]")
{
  std::vector<uint8_t> pkt = Query("connectivitycheck.gstatic.com");
  std::vector<uint8_t> buf = Buffer(pkt, DNSPacket::MAX_PACKET);

  size_t respLen = Run(buf, pkt.size());
  TEST_ASSERT_EQUAL_UINT(pkt.size() + kAnswerSize, respLen);

  // Header: ID echoed, QR|AA set, RD carried over, counts rewritten.
  TEST_ASSERT_EQUAL_HEX16(0xBEEF, GetU16(&buf[0]));
  TEST_ASSERT_EQUAL_HEX16(kFlagQR | kFlagAA | kFlagRD, GetU16(&buf[2]));
  TEST_ASSERT_EQUAL_UINT16(1, GetU16(&buf[4]));   // QDCOUNT
  TEST_ASSERT_EQUAL_UINT16(1, GetU16(&buf[6]));   // ANCOUNT
  TEST_ASSERT_EQUAL_UINT16(0, GetU16(&buf[8]));   // NSCOUNT
  TEST_ASSERT_EQUAL_UINT16(0, GetU16(&buf[10]));  // ARCOUNT

  // Question copied unchanged.
  TEST_ASSERT_EQUAL_MEMORY(&pkt[kHeaderSize], &buf[kHeaderSize], pkt.size() - kHeaderSize);

  // Answer: pointer to the question, A/IN, 60 s TTL, 4-byte RDATA of the AP address.
  const uint8_t expected[kAnswerSize] = {0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x04, kIp[0], kIp[1], kIp[2], kIp[3]};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, &buf[pkt.size()], kAnswerSize);
}

TEST_CASE("Reply flags carry only RD from the query", "[dns_server][answer]")
{
  std::vector<uint8_t> pkt = Query("example.com", kTypeA, kClassIN, 0);
  std::vector<uint8_t> buf = Buffer(pkt, DNSPacket::MAX_PACKET);
  TEST_ASSERT_EQUAL_UINT(pkt.size() + kAnswerSize, Run(buf, pkt.size()));
  TEST_ASSERT_EQUAL_HEX16(kFlagQR | kFlagAA, GetU16(&buf[2]));

  // TC/RA/AD/CD and a non-zero RCODE in the query must not leak into the reply.
  pkt = Query("example.com", kTypeA, kClassIN, kFlagTC | kFlagRD | kFlagRA | kFlagAD | kFlagCD | 0x000F);
  buf = Buffer(pkt, DNSPacket::MAX_PACKET);
  TEST_ASSERT_EQUAL_UINT(pkt.size() + kAnswerSize, Run(buf, pkt.size()));
  TEST_ASSERT_EQUAL_HEX16(kFlagQR | kFlagAA | kFlagRD, GetU16(&buf[2]));
}

TEST_CASE("Root name query is answered", "[dns_server][answer]")
{
  std::vector<uint8_t> pkt = Query(std::vector<uint8_t> {0});
  std::vector<uint8_t> buf = Buffer(pkt, DNSPacket::MAX_PACKET);
  TEST_ASSERT_EQUAL_UINT(kHeaderSize + 1 + 4 + kAnswerSize, Run(buf, pkt.size()));
  TEST_ASSERT_EQUAL_UINT16(1, GetU16(&buf[6]));
}

TEST_CASE("Non-A or non-IN questions get an empty answer", "[dns_server][answer]")
{
  const uint16_t cases[][2] = {
    {kTypeAAAA, kClassIN},
    { kTypeANY, kClassIN},
    {   kTypeA, kClassCH},
  };

  for (const auto& c : cases) {
    std::vector<uint8_t> pkt = Query("example.com", c[0], c[1]);
    std::vector<uint8_t> buf = Buffer(pkt, DNSPacket::MAX_PACKET);

    TEST_ASSERT_EQUAL_UINT(pkt.size(), Run(buf, pkt.size()));
    TEST_ASSERT_EQUAL_HEX16(kFlagQR | kFlagAA | kFlagRD, GetU16(&buf[2]));
    TEST_ASSERT_EQUAL_UINT16(0, GetU16(&buf[6]));  // ANCOUNT
    TEST_ASSERT_EQUAL_MEMORY(&pkt[kHeaderSize], &buf[kHeaderSize], pkt.size() - kHeaderSize);
  }
}

TEST_CASE("Records after the question are dropped from the reply", "[dns_server][answer]")
{
  // An EDNS0 OPT record in the additional section, as most stub resolvers send.
  std::vector<uint8_t> pkt = Query("example.com");
  pkt[11]                  = 1;  // ARCOUNT
  size_t questionEnd       = pkt.size();
  const uint8_t opt[]      = {0x00, 0x00, 0x29, 0x04, 0xD0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  pkt.insert(pkt.end(), std::begin(opt), std::end(opt));

  std::vector<uint8_t> buf = Buffer(pkt, DNSPacket::MAX_PACKET);
  TEST_ASSERT_EQUAL_UINT(questionEnd + kAnswerSize, Run(buf, pkt.size()));
  TEST_ASSERT_EQUAL_UINT16(0, GetU16(&buf[10]));  // ARCOUNT
  TEST_ASSERT_EQUAL_HEX8(0xC0, buf[questionEnd]);
  TEST_ASSERT_EQUAL_HEX8(0x0C, buf[questionEnd + 1]);
}

TEST_CASE("Maximum-length name is accepted, one byte more is not", "[dns_server][name]")
{
  // 255 encoded bytes, terminator included, is the longest a name may be.
  std::vector<uint8_t> longest = NameOfLength(255);
  TEST_ASSERT_EQUAL_UINT(255, longest.size());
  std::vector<uint8_t> pkt = Query(longest);
  std::vector<uint8_t> buf = Buffer(pkt, DNSPacket::MAX_PACKET);
  TEST_ASSERT_EQUAL_UINT(pkt.size() + kAnswerSize, Run(buf, pkt.size()));

  std::vector<uint8_t> tooLong = NameOfLength(256);
  TEST_ASSERT_EQUAL_UINT(256, tooLong.size());
  AssertIgnored(Query(tooLong));
}

TEST_CASE("63-byte labels are accepted", "[dns_server][name]")
{
  std::vector<uint8_t> name {63};
  name.insert(name.end(), 63, 'x');
  name.push_back(0);
  std::vector<uint8_t> pkt = Query(name);
  std::vector<uint8_t> buf = Buffer(pkt, DNSPacket::MAX_PACKET);
  TEST_ASSERT_EQUAL_UINT(pkt.size() + kAnswerSize, Run(buf, pkt.size()));
}

TEST_CASE("Runt and truncated packets are ignored", "[dns_server][reject]")
{
  std::vector<uint8_t> pkt = Query("example.com");

  // Zero-length datagram and every truncated header.
  for (size_t len = 0; len < kHeaderSize; len++) {
    AssertIgnored(std::vector<uint8_t>(pkt.begin(), pkt.begin() + len));
  }

  // Header only, no question at all.
  AssertIgnored(std::vector<uint8_t>(pkt.begin(), pkt.begin() + kHeaderSize));

  // Cut anywhere in the name or the QTYPE/QCLASS that follows it.
  for (size_t len = kHeaderSize + 1; len < pkt.size(); len++) {
    AssertIgnored(std::vector<uint8_t>(pkt.begin(), pkt.begin() + len));
  }
}

TEST_CASE("A datagram that fills the buffer is ignored", "[dns_server][reject]")
{
  // recvfrom truncates silently, so a full buffer means a question we never fully saw.
  std::vector<uint8_t> pkt = Query("example.com");
  AssertIgnored(pkt, pkt.size());

  // One spare byte is enough to take it at face value (an empty answer fits).
  pkt                      = Query("example.com", kTypeAAAA);
  std::vector<uint8_t> buf = Buffer(pkt, pkt.size() + 1);
  TEST_ASSERT_EQUAL_UINT(pkt.size(), Run(buf, pkt.size()));
}

TEST_CASE("A reply that would not fit is not built", "[dns_server][reject]")
{
  std::vector<uint8_t> pkt = Query("example.com");

  for (size_t capacity = pkt.size() + 1; capacity < pkt.size() + kAnswerSize; capacity++) {
    AssertIgnored(pkt, capacity);
  }

  std::vector<uint8_t> buf = Buffer(pkt, pkt.size() + kAnswerSize);
  TEST_ASSERT_EQUAL_UINT(buf.size(), Run(buf, pkt.size()));
}

TEST_CASE("Responses are ignored", "[dns_server][reject]")
{
  AssertIgnored(Query("example.com", kTypeA, kClassIN, kFlagQR));
  AssertIgnored(Query("example.com", kTypeA, kClassIN, kFlagQR | kFlagAA | kFlagRD));
}

TEST_CASE("Non-standard opcodes are ignored", "[dns_server][reject]")
{
  for (uint16_t opcode = 1; opcode < 16; opcode++) {
    AssertIgnored(Query("example.com", kTypeA, kClassIN, static_cast<uint16_t>(kFlagRD | (opcode << 11))));
  }
}

TEST_CASE("Anything but exactly one question is ignored", "[dns_server][reject]")
{
  AssertIgnored(Query("example.com", kTypeA, kClassIN, kFlagRD, 0));
  AssertIgnored(Query("example.com", kTypeA, kClassIN, kFlagRD, 2));
  AssertIgnored(Query("example.com", kTypeA, kClassIN, kFlagRD, 0x0100));
  AssertIgnored(Query("example.com", kTypeA, kClassIN, kFlagRD, 0xFFFF));

  // Two well-formed questions back to back are still two questions.
  std::vector<uint8_t> pkt    = Query("a.example", kTypeA, kClassIN, kFlagRD, 2);
  std::vector<uint8_t> second = Query("b.example");
  pkt.insert(pkt.end(), second.begin() + kHeaderSize, second.end());
  AssertIgnored(pkt);
}

TEST_CASE("Compression pointers and reserved label types are rejected", "[dns_server][name]")
{
  // Pointer as the whole name (back at the header).
  AssertIgnored(Query(std::vector<uint8_t> {0xC0, 0x0C}));
  // Pointer after an ordinary label.
  AssertIgnored(Query(std::vector<uint8_t> {3, 'w', 'w', 'w', 0xC0, 0x0C}));
  // Pointer to itself.
  AssertIgnored(Query(std::vector<uint8_t> {0xC0, 0x0C, 0x00}));
  // Reserved 0b01 and 0b10 label types.
  AssertIgnored(Query(std::vector<uint8_t> {0x40, 'a', 0}));
  AssertIgnored(Query(std::vector<uint8_t> {0x80, 'a', 0}));
  AssertIgnored(Query(std::vector<uint8_t> {1, 'a', 0x41, 0}));
}

TEST_CASE("Labels running past the packet are rejected", "[dns_server][name]")
{
  // Label claims 20 bytes but the packet ends after 3; the padding beyond it
  // in the buffer must not be read as the rest of the label.
  std::vector<uint8_t> pkt = Header(0x1234, kFlagRD, 1);
  const uint8_t name[]     = {20, 'a', 'b', 'c'};
  pkt.insert(pkt.end(), std::begin(name), std::end(name));
  AssertIgnored(pkt);

  // Label ends exactly at the end of the packet, with no room for a terminator.
  pkt                   = Header(0x1234, kFlagRD, 1);
  const uint8_t exact[] = {3, 'a', 'b', 'c'};
  pkt.insert(pkt.end(), std::begin(exact), std::end(exact));
  AssertIgnored(pkt);
}

TEST_CASE("A name with no terminator is rejected", "[dns_server][name]")
{
  // Labels fill the rest of the packet; the buffer past it is zero, which must
  // not be taken as the terminator.
  std::vector<uint8_t> pkt = Header(0x1234, kFlagRD, 1);
  const uint8_t name[]     = {3, 'w', 'w', 'w', 7, 'e', 'x', 'a', 'm', 'p', 'l', 'e'};
  pkt.insert(pkt.end(), std::begin(name), std::end(name));

  std::vector<uint8_t> buf    = Buffer(pkt, DNSPacket::MAX_PACKET, 0x00);
  std::vector<uint8_t> before = buf;
  TEST_ASSERT_EQUAL_UINT(0, Run(buf, pkt.size()));
  TEST_ASSERT_EQUAL_MEMORY(before.data(), buf.data(), buf.size());
}

TEST_CASE("A name with no room for QTYPE/QCLASS is rejected", "[dns_server][name]")
{
  // QTYPE/QCLASS bytes present in the buffer but past the packet length.
  std::vector<uint8_t> pkt = Query("example.com");
  for (size_t cut = 1; cut <= 4; cut++) {
    std::vector<uint8_t> buf    = Buffer(pkt, DNSPacket::MAX_PACKET);
    std::vector<uint8_t> before = buf;
    TEST_ASSERT_EQUAL_UINT(0, Run(buf, pkt.size() - cut));
    TEST_ASSERT_EQUAL_MEMORY(before.data(), buf.data(), buf.size());
  }
}

namespace {
  // Parses a reply independently and checks it against the query it came from.
  void AssertWellFormedReply(const std::vector<uint8_t>& query, const std::vector<uint8_t>& buf, size_t respLen)
  {
    TEST_ASSERT_LESS_OR_EQUAL_UINT(buf.size(), respLen);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(kHeaderSize + 1 + 4, respLen);

    uint16_t flags = GetU16(&buf[2]);
    TEST_ASSERT_EQUAL_HEX16(kFlagQR | kFlagAA | (GetU16(&query[2]) & kFlagRD), flags);
    TEST_ASSERT_EQUAL_HEX16(GetU16(&query[0]), GetU16(&buf[0]));
    TEST_ASSERT_EQUAL_UINT16(1, GetU16(&buf[4]));

    // Find the question end in the query, the same way a strict resolver would.
    size_t off = kHeaderSize;
    while (query[off] != 0) {
      off += query[off] + 1u;
    }
    size_t questionEnd = off + 1 + 4;
    TEST_ASSERT_LESS_OR_EQUAL_UINT(query.size(), questionEnd);
    TEST_ASSERT_LESS_OR_EQUAL_UINT(255, off + 1 - kHeaderSize);
    TEST_ASSERT_EQUAL_MEMORY(&query[kHeaderSize], &buf[kHeaderSize], questionEnd - kHeaderSize);

    bool answered = GetU16(&query[off + 1]) == kTypeA && GetU16(&query[off + 3]) == kClassIN;
    TEST_ASSERT_EQUAL_UINT16(answered ? 1 : 0, GetU16(&buf[6]));
    TEST_ASSERT_EQUAL_UINT(questionEnd + (answered ? kAnswerSize : 0), respLen);
    if (answered) {
      TEST_ASSERT_EQUAL_HEX8(kIp[3], buf[respLen - 1]);
    }
  }

  // One fuzz iteration: run `pkt` in a buffer of `capacity` twice, with two
  // different paddings past the packet, and require identical, bounded results.
  // Returns the reply length.
  size_t FuzzOne(const std::vector<uint8_t>& pkt, size_t capacity)
  {
    std::vector<uint8_t> a = Buffer(pkt, capacity, 0x00);
    std::vector<uint8_t> b = Buffer(pkt, capacity, 0xFF);

    size_t lenA = Run(a, pkt.size());
    size_t lenB = Run(b, pkt.size());

    TEST_ASSERT_LESS_OR_EQUAL_UINT(capacity, lenA);
    TEST_ASSERT_EQUAL_UINT(lenA, lenB);  // never depends on bytes past the packet

    if (lenA == 0) {
      // Dropped packets leave the buffer as it was.
      if (!pkt.empty()) {
        TEST_ASSERT_EQUAL_MEMORY(pkt.data(), a.data(), pkt.size());
      }
      return 0;
    }

    TEST_ASSERT_EQUAL_MEMORY(a.data(), b.data(), lenA);
    AssertWellFormedReply(pkt, a, lenA);
    return lenA;
  }
}  // namespace

TEST_CASE("Fuzz: mutated queries never produce an out-of-bounds or malformed reply", "[dns_server][fuzz]")
{
  std::mt19937 rng(0x0D115EED);  // fixed seed, so a failure reproduces

  const std::vector<uint8_t> seeds[] = {
    Query("connectivitycheck.gstatic.com"),
    Query("captive.apple.com", kTypeAAAA),
    Query(std::vector<uint8_t> {0}),
    Query(NameOfLength(255)),
    Query("a.b.c.d.e.f.g.h", kTypeA, kClassCH),
  };

  int replies = 0;

  for (int iter = 0; iter < 200000; iter++) {
    std::vector<uint8_t> pkt = seeds[rng() % (sizeof(seeds) / sizeof(seeds[0]))];

    int mutations = 1 + static_cast<int>(rng() % 4);
    for (int m = 0; m < mutations; m++) {
      switch (rng() % 6) {
        case 0:  // flip a bit
          if (!pkt.empty()) {
            pkt[rng() % pkt.size()] ^= static_cast<uint8_t>(1u << (rng() % 8));
          }
          break;
        case 1:  // overwrite a byte
          if (!pkt.empty()) {
            pkt[rng() % pkt.size()] = static_cast<uint8_t>(rng());
          }
          break;
        case 2:  // truncate
          pkt.resize(rng() % (pkt.size() + 1));
          break;
        case 3:  // append junk
          for (size_t n = rng() % 32; n > 0; n--) {
            pkt.push_back(static_cast<uint8_t>(rng()));
          }
          break;
        case 4:  // set a label length byte in the question to something interesting
          if (pkt.size() > kHeaderSize) {
            static const uint8_t interesting[]                    = {0x00, 0x01, 0x3F, 0x40, 0x7F, 0x80, 0xBF, 0xC0, 0xFF};
            pkt[kHeaderSize + rng() % (pkt.size() - kHeaderSize)] = interesting[rng() % sizeof(interesting)];
          }
          break;
        case 5:  // make it a valid-looking single standard query again
          if (pkt.size() >= kHeaderSize) {
            pkt[2] &= 0x07;
            pkt[4] = 0;
            pkt[5] = 1;
          }
          break;
      }
    }

    // Capacity from "just over the packet" up to the real buffer, so the
    // reply-fits check is exercised at every margin.
    size_t minCapacity = pkt.size() + 1;
    size_t capacity    = minCapacity + rng() % (kAnswerSize + 8);
    if (rng() % 2 == 0 && minCapacity <= DNSPacket::MAX_PACKET) {
      capacity = DNSPacket::MAX_PACKET;
    }

    if (FuzzOne(pkt, capacity) != 0) {
      replies++;
    }
  }

  // The mutations must leave enough queries intact to exercise the build path.
  TEST_ASSERT_GREATER_THAN_INT(10000, replies);
}

TEST_CASE("Fuzz: random bytes never produce an out-of-bounds reply", "[dns_server][fuzz]")
{
  std::mt19937 rng(0xC0FFEE);

  for (int iter = 0; iter < 100000; iter++) {
    std::vector<uint8_t> pkt(rng() % 300);
    for (uint8_t& byte : pkt) {
      byte = static_cast<uint8_t>(rng());
    }
    // Bias towards the interesting path: mostly a standard single-question query.
    if (pkt.size() >= kHeaderSize && rng() % 4 != 0) {
      pkt[2] &= 0x07;
      pkt[4] = 0;
      pkt[5] = 1;
    }

    FuzzOne(pkt, pkt.size() + 1 + rng() % 64);
  }
}
