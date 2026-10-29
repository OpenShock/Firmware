// The parser and the invariant checker against every table in partitions/ and a corpus of broken ones: the
// design's "a test feeds the checker every table ever shipped plus deliberately broken ones".
#include "unity.h"

#include "Md5.h"
#include "partition_layout/PartitionTable.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace OpenShock;
namespace PT = OpenShock::PartitionTable;

namespace {
  constexpr uint32_t kMiB = 1024 * 1024;

  std::vector<uint8_t> ReadFixture(const char* name)
  {
    std::string path = std::string(PT_FIXTURE_DIR) + "/" + name;
    FILE* f          = fopen(path.c_str(), "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path.c_str());
    std::vector<uint8_t> data(PT::kMaxLen);
    std::size_t n = fread(data.data(), 1, data.size(), f);
    fclose(f);
    TEST_ASSERT_EQUAL_size_t(PT::kMaxLen, n);
    return data;
  }

  std::vector<uint8_t> V1()
  {
    return ReadFixture("ota_4mb_v1.bin");
  }
  std::vector<uint8_t> V2_4MB()
  {
    return ReadFixture("ota_4mb.bin");
  }

  PT::Table ParseOk(const std::vector<uint8_t>& bytes)
  {
    auto table = PT::Parse(bytes);
    TEST_ASSERT_TRUE_MESSAGE(table.has_value(), table ? "" : PT::ParseErrorName(table.error()));
    return *table;
  }

  void ExpectParseError(const std::vector<uint8_t>& bytes, PT::ParseError expected)
  {
    auto table = PT::Parse(bytes);
    TEST_ASSERT_FALSE_MESSAGE(table.has_value(), "parsed a broken table");
    TEST_ASSERT_EQUAL_STRING(PT::ParseErrorName(expected), PT::ParseErrorName(table.error()));
  }

  // Re-seals a table after an entry was edited, so the test exercises the invariant and not the MD5.
  void Reseal(std::vector<uint8_t>& bytes)
  {
    std::size_t pos = 0;
    while (pos + PT::kEntrySize <= bytes.size() && (bytes[pos] | (bytes[pos + 1] << 8)) == PT::kMagic) {
      pos += PT::kEntrySize;
    }
    TEST_ASSERT_EQUAL_UINT16(PT::kMd5Magic, bytes[pos] | (bytes[pos + 1] << 8));
    uint8_t digest[16];
    PT::Internal::Md5(bytes.data(), pos, digest);
    memcpy(bytes.data() + pos + 16, digest, 16);
  }

  std::size_t EntryPos(const std::vector<uint8_t>& bytes, const char* label)
  {
    for (std::size_t pos = 0; pos + PT::kEntrySize <= bytes.size(); pos += PT::kEntrySize) {
      if ((bytes[pos] | (bytes[pos + 1] << 8)) != PT::kMagic) {
        break;
      }
      if (strncmp(reinterpret_cast<const char*>(bytes.data() + pos + 12), label, PT::kLabelSize) == 0) {
        return pos;
      }
    }
    TEST_FAIL_MESSAGE(label);
    return 0;
  }

  void Put32(std::vector<uint8_t>& bytes, std::size_t at, uint32_t value)
  {
    for (int i = 0; i < 4; ++i) bytes[at + i] = static_cast<uint8_t>(value >> (8 * i));
  }
  void SetOffset(std::vector<uint8_t>& bytes, const char* label, uint32_t offset)
  {
    Put32(bytes, EntryPos(bytes, label) + 4, offset);
    Reseal(bytes);
  }
  void SetSize(std::vector<uint8_t>& bytes, const char* label, uint32_t size)
  {
    Put32(bytes, EntryPos(bytes, label) + 8, size);
    Reseal(bytes);
  }
  void SetSubtype(std::vector<uint8_t>& bytes, const char* label, uint8_t subtype)
  {
    bytes[EntryPos(bytes, label) + 3] = subtype;
    Reseal(bytes);
  }
  void SetLabel(std::vector<uint8_t>& bytes, const char* label, const char* replacement)
  {
    std::size_t pos = EntryPos(bytes, label);
    memset(bytes.data() + pos + 12, 0, PT::kLabelSize);
    memcpy(bytes.data() + pos + 12, replacement, strlen(replacement));
    Reseal(bytes);
  }

  // A 1.9.9 image of 1.2 MB running from app0 on a 4 MB chip whose bootloader says 4 MB, with the v1 table live.
  struct Scenario {
    PT::Table live;
    PT::Context context;
    explicit Scenario(uint32_t flashMiB = 4, uint32_t bootloaderMiB = 4, uint32_t runningOffset = PT::kApp0Offset)
      : live(ParseOk(V1()))
      , context {flashMiB * kMiB, bootloaderMiB * kMiB, runningOffset, 1200 * 1024, &live}
    {
    }
  };

  void ExpectViolation(const std::vector<uint8_t>& staged, const PT::Context& context, PT::Violation expected)
  {
    auto result = PT::Validate(ParseOk(staged), context);
    TEST_ASSERT_FALSE_MESSAGE(result.has_value(), "a bad table validated");
    TEST_ASSERT_EQUAL_STRING(PT::ViolationName(expected), PT::ViolationName(result.error()));
  }

  void ExpectValid(const std::vector<uint8_t>& staged, const PT::Context& context)
  {
    auto result = PT::Validate(ParseOk(staged), context);
    TEST_ASSERT_TRUE_MESSAGE(result.has_value(), result ? "" : PT::ViolationName(result.error()));
  }
}  // namespace

TEST_CASE("MD5 matches the RFC 1321 test suite", "[partition_table][md5]")
{
  uint8_t digest[16];
  PT::Internal::Md5(reinterpret_cast<const uint8_t*>(""), 0, digest);
  const uint8_t empty[16] = {0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04, 0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(empty, digest, 16);

  const char* msg = "The quick brown fox jumps over the lazy dog";
  PT::Internal::Md5(reinterpret_cast<const uint8_t*>(msg), strlen(msg), digest);
  const uint8_t fox[16] = {0x9e, 0x10, 0x7d, 0x9d, 0x37, 0x2b, 0xb6, 0x82, 0x6b, 0xd8, 0x1d, 0x35, 0x42, 0xa4, 0x19, 0xd6};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(fox, digest, 16);

  std::string eighty(80, 'a');  // crosses a block, length lands in the next padding block
  PT::Internal::Md5(reinterpret_cast<const uint8_t*>(eighty.data()), eighty.size(), digest);
  const uint8_t a80[16] = {0xb1, 0x5a, 0xf9, 0xcd, 0xab, 0xba, 0xea, 0x05, 0x16, 0x86, 0x6a, 0x33, 0xd8, 0xfd, 0x0f, 0x98};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(a80, digest, 16);
}

TEST_CASE("Every table the repository ships parses with its MD5", "[partition_table]")
{
  PT::Table v1 = ParseOk(V1());
  TEST_ASSERT_EQUAL_size_t(7, v1.count);
  TEST_ASSERT_EQUAL_STRING("nvs", v1.entries[0].label);
  TEST_ASSERT_EQUAL_HEX32(0x9000, v1.entries[0].offset);
  TEST_ASSERT_EQUAL_HEX32(0x5000, v1.entries[0].size);
  TEST_ASSERT_EQUAL_UINT8(PT::kTypeData, v1.entries[0].type);
  TEST_ASSERT_EQUAL_UINT8(PT::kSubtypeDataNvs, v1.entries[0].subtype);
  TEST_ASSERT_NOT_NULL(v1.Find(PT::kTypeData, PT::kSubtypeDataSpiffs, "config"));
  TEST_ASSERT_EQUAL_HEX32(0x1A0000, v1.FindAppAt(0x10000)->size);
  TEST_ASSERT_EQUAL_HEX32(0x400000, v1.End());

  PT::Table v2 = ParseOk(V2_4MB());
  TEST_ASSERT_EQUAL_size_t(7, v2.count);
  TEST_ASSERT_EQUAL_HEX32(0x1A0000, v2.FindAppAt(0x10000)->size);
  TEST_ASSERT_EQUAL_HEX32(0x350000, v2.Find("cfg")->offset);
  TEST_ASSERT_EQUAL_UINT8(PT::kSubtypeDataNvs, v2.Find("cfg")->subtype);
  TEST_ASSERT_EQUAL_UINT8(PT::kTypeWww, v2.Find("www")->type);
  TEST_ASSERT_EQUAL_HEX32(0x353000, v2.Find("www")->offset);
  TEST_ASSERT_EQUAL_HEX32(0x400000, v2.End());
  TEST_ASSERT_NULL(v2.Find("static0"));
  TEST_ASSERT_NULL(v2.Find(PT::kTypeData, PT::kSubtypeDataNvs, "www"));  // the subtype is checked too
}

TEST_CASE("Layouts are told apart", "[partition_table]")
{
  TEST_ASSERT_EQUAL(PT::Layout::V1, PT::Detect(ParseOk(V1())));
  TEST_ASSERT_EQUAL(PT::Layout::V2, PT::Detect(ParseOk(V2_4MB())));

  auto bytes = V2_4MB();
  SetLabel(bytes, "www", "static0");  // neither layout
  TEST_ASSERT_EQUAL(PT::Layout::Unknown, PT::Detect(ParseOk(bytes)));
}

TEST_CASE("A table without an MD5 entry, or with a wrong one, is refused", "[partition_table][corrupt]")
{
  auto bytes = V2_4MB();
  bytes[16] ^= 0x01;  // a byte inside the first entry: the MD5 no longer matches
  ExpectParseError(bytes, PT::ParseError::Md5Mismatch);

  bytes              = V2_4MB();
  std::size_t md5Pos = 7 * PT::kEntrySize;
  memset(bytes.data() + md5Pos, 0xFF, PT::kEntrySize);  // the MD5 entry erased
  ExpectParseError(bytes, PT::ParseError::NoMd5);

  bytes = V2_4MB();
  bytes[md5Pos + 16] ^= 0x01;  // the digest itself
  ExpectParseError(bytes, PT::ParseError::Md5Mismatch);

  bytes                              = V2_4MB();
  bytes[md5Pos + PT::kEntrySize + 5] = 0x00;  // garbage after the MD5 entry
  ExpectParseError(bytes, PT::ParseError::Trailing);

  bytes = V2_4MB();
  memcpy(bytes.data() + md5Pos + PT::kEntrySize, bytes.data(), PT::kEntrySize);  // an entry after the MD5 entry
  ExpectParseError(bytes, PT::ParseError::Trailing);

  std::vector<uint8_t> erased(PT::kMaxLen, 0xFF);
  ExpectParseError(erased, PT::ParseError::NoEntries);
  ExpectParseError(std::vector<uint8_t>(10, 0xFF), PT::ParseError::TooShort);
  const auto whole = V2_4MB();
  ExpectParseError(std::vector<uint8_t>(whole.begin(), whole.begin() + 3 * PT::kEntrySize), PT::ParseError::NoMd5);
}

TEST_CASE("The shipped 2.x table passes every invariant from a 1.x hub", "[partition_table][validate]")
{
  ExpectValid(V2_4MB(), Scenario(4, 4).context);
  ExpectValid(V2_4MB(), Scenario(8, 8).context);  // one table for every flash size
  ExpectValid(V2_4MB(), Scenario(16, 16).context);
  ExpectValid(V2_4MB(), Scenario(16, 4).context);  // a bootloader stamped smaller than the chip still covers it
  // Nothing moves, so running from app1 is as good as running from app0
  ExpectValid(V2_4MB(), Scenario(4, 4, 0x1B0000).context);
}

TEST_CASE("The table is refused on a smaller chip or behind a smaller bootloader header", "[partition_table][validate]")
{
  ExpectViolation(V2_4MB(), Scenario(2, 2).context, PT::Violation::PastFlash);
  // The bootloader rejects every entry past the size its header declares: a brick
  ExpectViolation(V2_4MB(), Scenario(4, 2).context, PT::Violation::BootloaderHeaderTooSmall);
  ExpectViolation(V2_4MB(), Scenario(4, 0).context, PT::Violation::BootloaderHeaderUnknown);
}

TEST_CASE("The running image must fit its slot", "[partition_table][validate]")
{
  Scenario huge(4, 4);
  huge.context.runningImageSize = 0x1A0000 + 1;
  ExpectViolation(V2_4MB(), huge.context, PT::Violation::NoRunningSlot);
}

TEST_CASE("A table that moves, adds, removes or retypes anything else is refused", "[partition_table][validate]")
{
  auto bytes = V2_4MB();
  SetSize(bytes, "cfg", 0x2000);  // a gap, no overlap: still a valid table, but cfg shrank
  ExpectViolation(bytes, Scenario().context, PT::Violation::GeometryChanged);

  bytes = V2_4MB();
  SetOffset(bytes, "www", 0x354000);
  SetSize(bytes, "www", 0x9C000);
  ExpectViolation(bytes, Scenario().context, PT::Violation::GeometryChanged);

  bytes = V2_4MB();
  SetSize(bytes, "coredump", 0x8000);  // shrunk
  ExpectViolation(bytes, Scenario().context, PT::Violation::GeometryChanged);

  bytes = V2_4MB();
  SetLabel(bytes, "coredump", "crash");
  ExpectViolation(bytes, Scenario().context, PT::Violation::EntryRetyped);

  bytes = V2_4MB();
  SetSubtype(bytes, "coredump", PT::kSubtypeDataNvs);
  ExpectViolation(bytes, Scenario().context, PT::Violation::EntryRetyped);

  // cfg and www swapped: each exists with its type, every place is the old one, but cfg would take over static0
  bytes = V2_4MB();
  SetOffset(bytes, "cfg", 0x353000);
  SetSize(bytes, "cfg", 0x9D000);
  SetOffset(bytes, "www", 0x350000);
  SetSize(bytes, "www", 0x3000);
  ExpectViolation(bytes, Scenario().context, PT::Violation::EntryRetyped);
}

TEST_CASE("The live table must be readable and the 1.x layout", "[partition_table][validate]")
{
  Scenario scenario;
  scenario.context.live = nullptr;
  ExpectViolation(V2_4MB(), scenario.context, PT::Violation::LiveUnreadable);

  PT::Table alreadyV2   = ParseOk(V2_4MB());
  scenario.context.live = &alreadyV2;
  ExpectViolation(V2_4MB(), scenario.context, PT::Violation::LiveNotMigratable);

  auto odd = V1();
  SetLabel(odd, "static0", "assets");
  PT::Table unknown     = ParseOk(odd);
  scenario.context.live = &unknown;
  ExpectViolation(V2_4MB(), scenario.context, PT::Violation::LiveNotMigratable);
}

TEST_CASE("A moved nvs or otadata is refused", "[partition_table][validate]")
{
  auto bytes = V2_4MB();
  SetOffset(bytes, "nvs", 0x8000);  // onto the table itself: nothing in the table overlaps, but it moved
  ExpectViolation(bytes, Scenario().context, PT::Violation::NvsMoved);

  bytes = V2_4MB();
  SetSize(bytes, "nvs", 0x4000);
  ExpectViolation(bytes, Scenario().context, PT::Violation::NvsMoved);

  bytes = V2_4MB();
  SetOffset(bytes, "otadata", 0xF000);
  ExpectViolation(bytes, Scenario().context, PT::Violation::Overlap);  // onto app0; geometry is checked first
  SetOffset(bytes, "otadata", 0xE000);
  SetSize(bytes, "otadata", 0x1000);                                   // in place, but shrunk
  ExpectViolation(bytes, Scenario().context, PT::Violation::OtadataMoved);
}

TEST_CASE("A table missing cfg or www, or not a 2.x layout, is refused", "[partition_table][validate]")
{
  auto bytes = V2_4MB();
  SetLabel(bytes, "cfg", "nvs2");
  ExpectViolation(bytes, Scenario().context, PT::Violation::NoCfg);

  bytes = V2_4MB();
  SetLabel(bytes, "www", "static0");
  ExpectViolation(bytes, Scenario().context, PT::Violation::NoWww);

  // Not 2.x: a 1.x table staged over a 1.x table
  ExpectViolation(V1(), Scenario().context, PT::Violation::NoCfg);
}

TEST_CASE("Geometry the bootloader would refuse is refused first", "[partition_table][validate]")
{
  auto bytes = V2_4MB();
  SetOffset(bytes, "app1", 0x1B0800);
  ExpectViolation(bytes, Scenario().context, PT::Violation::SizeUnaligned);  // not even sector aligned

  bytes = V2_4MB();
  SetOffset(bytes, "app1", 0x1B1000);
  ExpectViolation(bytes, Scenario().context, PT::Violation::AppUnaligned);

  bytes = V2_4MB();
  SetSize(bytes, "cfg", 0x3800);
  ExpectViolation(bytes, Scenario().context, PT::Violation::SizeUnaligned);

  bytes = V2_4MB();
  SetSize(bytes, "cfg", 0);
  ExpectViolation(bytes, Scenario().context, PT::Violation::SizeUnaligned);

  bytes = V2_4MB();
  SetSize(bytes, "cfg", 0x4000);  // into www
  ExpectViolation(bytes, Scenario().context, PT::Violation::Overlap);

  bytes = V2_4MB();
  SetSize(bytes, "coredump", 0x20000);  // past 4 MB
  ExpectViolation(bytes, Scenario().context, PT::Violation::PastFlash);

  bytes = V2_4MB();
  SetSize(bytes, "coredump", 0xFFFFF000);  // offset + size wraps
  ExpectViolation(bytes, Scenario().context, PT::Violation::PastFlash);
}
