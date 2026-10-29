// The parser and the in-place retype against the 1.x table every fielded hub carries and a corpus of broken copies.
#include "unity.h"

#include "Md5.h"
#include "partition_layout/PartitionTable.h"

#include <cstring>
#include <string>
#include <vector>

using namespace OpenShock;
namespace PT = OpenShock::PartitionTable;

namespace {
  struct Row {
    const char* label;
    uint8_t type;
    uint8_t subtype;
    uint32_t offset;
    uint32_t size;
  };

  // partitions/legacy/ota_4mb_v1.csv
  const std::vector<Row> kV1 = {
    {     "nvs", PT::kTypeData,    PT::kSubtypeDataNvs, 0x009000, 0x005000},
    { "otadata", PT::kTypeData,                   0x00, 0x00E000, 0x002000},
    {    "app0",  PT::kTypeApp,                   0x10, 0x010000, 0x1A0000},
    {    "app1",  PT::kTypeApp,                   0x11, 0x1B0000, 0x1A0000},
    {  "config", PT::kTypeData, PT::kSubtypeDataSpiffs, 0x350000, 0x003000},
    { "static0", PT::kTypeData, PT::kSubtypeDataSpiffs, 0x353000, 0x09D000},
    {"coredump", PT::kTypeData,                   0x03, 0x3F0000, 0x010000},
  };

  void Put32(uint8_t* p, uint32_t value)
  {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(value >> (8 * i));
  }

  // The bytes gen_esp32part.py writes for `rows`: the entries, the MD5 entry, 0xFF to the end.
  PT::Bytes Build(const std::vector<Row>& rows)
  {
    PT::Bytes bytes;
    bytes.fill(0xFF);
    std::size_t pos = 0;
    for (const Row& row : rows) {
      uint8_t* p = bytes.data() + pos;
      p[0]       = PT::kMagic & 0xFF;
      p[1]       = PT::kMagic >> 8;
      p[2]       = row.type;
      p[3]       = row.subtype;
      Put32(p + 4, row.offset);
      Put32(p + 8, row.size);
      memset(p + 12, 0, PT::kLabelSize);
      memcpy(p + 12, row.label, strlen(row.label));
      Put32(p + 28, 0);
      pos += PT::kEntrySize;
    }
    bytes[pos]     = PT::kMd5Magic & 0xFF;
    bytes[pos + 1] = PT::kMd5Magic >> 8;
    uint8_t digest[16];
    PT::Internal::Md5(bytes.data(), pos, digest);
    memcpy(bytes.data() + pos + 16, digest, 16);
    return bytes;
  }

  PT::Table ParseOk(std::span<const uint8_t> bytes)
  {
    PT::Table table {};
    auto result = PT::Parse(bytes, table);
    TEST_ASSERT_TRUE_MESSAGE(result.has_value(), result ? "" : PT::ParseErrorName(result.error()));
    return table;
  }

  void ExpectParseError(std::span<const uint8_t> bytes, PT::ParseError expected)
  {
    PT::Table table {};
    auto result = PT::Parse(bytes, table);
    TEST_ASSERT_FALSE_MESSAGE(result.has_value(), "parsed a broken table");
    TEST_ASSERT_EQUAL_STRING(PT::ParseErrorName(expected), PT::ParseErrorName(result.error()));
  }

  void ExpectRetypeError(PT::Bytes bytes, std::string_view label, std::string_view newLabel, PT::RetypeError expected)
  {
    const PT::Bytes before = bytes;
    auto result            = PT::Retype(bytes, label, PT::kTypeData, PT::kSubtypeDataNvs, newLabel);
    TEST_ASSERT_FALSE_MESSAGE(result.has_value(), "retyped what it should not have");
    TEST_ASSERT_EQUAL_STRING(PT::RetypeErrorName(expected), PT::RetypeErrorName(result.error()));
    TEST_ASSERT_TRUE_MESSAGE(bytes == before, "a refused retype changed the table");
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

TEST_CASE("The 1.x table parses with its MD5", "[partition_table]")
{
  PT::Table v1 = ParseOk(Build(kV1));
  TEST_ASSERT_EQUAL_size_t(7, v1.count);
  TEST_ASSERT_EQUAL_STRING("nvs", v1.entries[0].label);
  TEST_ASSERT_EQUAL_HEX32(0x9000, v1.entries[0].offset);
  TEST_ASSERT_EQUAL_HEX32(0x5000, v1.entries[0].size);
  TEST_ASSERT_EQUAL_UINT8(PT::kTypeData, v1.entries[0].type);
  TEST_ASSERT_EQUAL_UINT8(PT::kSubtypeDataNvs, v1.entries[0].subtype);
  const PT::Entry* config = v1.Find("config");
  TEST_ASSERT_NOT_NULL(config);
  TEST_ASSERT_EQUAL_UINT8(PT::kSubtypeDataSpiffs, config->subtype);
  TEST_ASSERT_EQUAL_HEX32(0x350000, config->offset);
  TEST_ASSERT_NULL(v1.Find("cfg"));
}

TEST_CASE("A table without an MD5 entry, or with a wrong one, is refused", "[partition_table][corrupt]")
{
  const PT::Bytes good     = Build(kV1);
  const std::size_t md5Pos = kV1.size() * PT::kEntrySize;

  PT::Bytes bytes = good;
  bytes[16] ^= 0x01;  // a byte inside the first entry: the MD5 no longer matches
  ExpectParseError(bytes, PT::ParseError::Md5Mismatch);

  bytes = good;
  memset(bytes.data() + md5Pos, 0xFF, PT::kEntrySize);  // the MD5 entry erased
  ExpectParseError(bytes, PT::ParseError::NoMd5);

  bytes = good;
  bytes[md5Pos + 16] ^= 0x01;  // the digest itself
  ExpectParseError(bytes, PT::ParseError::Md5Mismatch);

  bytes                              = good;
  bytes[md5Pos + PT::kEntrySize + 5] = 0x00;  // garbage after the MD5 entry
  ExpectParseError(bytes, PT::ParseError::Trailing);

  bytes = good;
  memcpy(bytes.data() + md5Pos + PT::kEntrySize, bytes.data(), PT::kEntrySize);  // an entry after the MD5 entry
  ExpectParseError(bytes, PT::ParseError::Trailing);

  PT::Bytes erased;
  erased.fill(0xFF);
  ExpectParseError(erased, PT::ParseError::NoEntries);
  ExpectParseError(std::vector<uint8_t>(10, 0xFF), PT::ParseError::TooShort);
  ExpectParseError(std::span<const uint8_t>(good).first(3 * PT::kEntrySize), PT::ParseError::NoMd5);
}

TEST_CASE("Retype changes one entry's type, subtype and label and re-seals the MD5", "[partition_table][retype]")
{
  PT::Bytes bytes = Build(kV1);
  TEST_ASSERT_TRUE(PT::Retype(bytes, "config", PT::kTypeData, PT::kSubtypeDataNvs, "cfg").has_value());

  // What gen_esp32part.py would write for the retyped table, byte for byte
  std::vector<Row> expected = kV1;
  expected[4]               = {"cfg", PT::kTypeData, PT::kSubtypeDataNvs, 0x350000, 0x003000};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(Build(expected).data(), bytes.data(), PT::kMaxLen);

  PT::Table table = ParseOk(bytes);
  TEST_ASSERT_NULL(table.Find("config"));
  const PT::Entry* cfg = table.Find("cfg");
  TEST_ASSERT_NOT_NULL(cfg);
  TEST_ASSERT_EQUAL_UINT8(PT::kSubtypeDataNvs, cfg->subtype);
  TEST_ASSERT_EQUAL_HEX32(0x350000, cfg->offset);
  TEST_ASSERT_EQUAL_HEX32(0x3000, cfg->size);
}

TEST_CASE("Retype refuses what it cannot do and leaves the table alone", "[partition_table][retype]")
{
  const PT::Bytes good = Build(kV1);
  ExpectRetypeError(good, "nope", "cfg", PT::RetypeError::NoSuchEntry);
  ExpectRetypeError(good, "config", "static0", PT::RetypeError::LabelTaken);

  PT::Bytes broken = good;
  broken[16] ^= 0x01;
  ExpectRetypeError(broken, "config", "cfg", PT::RetypeError::Unparsable);
}

TEST_CASE("Retype takes a label of the full 16 bytes and refuses a longer one", "[partition_table][retype]")
{
  PT::Bytes bytes = Build(kV1);
  TEST_ASSERT_TRUE(PT::Retype(bytes, "config", PT::kTypeData, PT::kSubtypeDataNvs, "sixteen-chars-xx").has_value());
  TEST_ASSERT_NOT_NULL(ParseOk(bytes).Find("sixteen-chars-xx"));

  ExpectRetypeError(Build(kV1), "config", "seventeen-chars-x", PT::RetypeError::LabelTooLong);
}
