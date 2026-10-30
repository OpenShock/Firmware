// Parsing images written by scripts/utils/wwwimage.py (the fixtures CMake packs at configure time), and refusing
// every way a field of one can be wrong.
#include "unity.h"

#include "Hashing.h"
#include "www/WwwImage.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace OpenShock;

namespace {
  std::vector<uint8_t> ReadFile(const char* name)
  {
    std::string path = std::string(WWW_FIXTURE_DIR) + "/" + name;
    FILE* f          = fopen(path.c_str(), "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path.c_str());
    std::vector<uint8_t> data;
    uint8_t buf[4096];
    std::size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
      data.insert(data.end(), buf, buf + n);
    }
    fclose(f);
    return data;
  }

  std::vector<uint8_t> Fixture()
  {
    return ReadFile("assets.bin");
  }

  Www::Image ParseOk(const std::vector<uint8_t>& bytes)
  {
    auto image = Www::Image::Parse(bytes);
    TEST_ASSERT_TRUE_MESSAGE(image.has_value(), image ? "" : Www::ParseErrorName(image.error()));
    return *image;
  }

  void ExpectError(const std::vector<uint8_t>& bytes, Www::ParseError expected)
  {
    auto image = Www::Image::Parse(bytes);
    TEST_ASSERT_FALSE_MESSAGE(image.has_value(), "parsed a corrupt image");
    TEST_ASSERT_EQUAL_STRING(Www::ParseErrorName(expected), Www::ParseErrorName(image.error()));
  }

  void Put16(std::vector<uint8_t>& bytes, std::size_t at, uint16_t value)
  {
    bytes[at]     = static_cast<uint8_t>(value);
    bytes[at + 1] = static_cast<uint8_t>(value >> 8);
  }
  void Put32(std::vector<uint8_t>& bytes, std::size_t at, uint32_t value)
  {
    for (int i = 0; i < 4; ++i) bytes[at + i] = static_cast<uint8_t>(value >> (8 * i));
  }
  uint16_t Get16(const std::vector<uint8_t>& bytes, std::size_t at)
  {
    return static_cast<uint16_t>(bytes[at] | (bytes[at + 1] << 8));
  }

  std::string Body(const Www::Asset& asset)
  {
    return std::string(reinterpret_cast<const char*>(asset.body.data()), asset.body.size());
  }
}  // namespace

TEST_CASE("An image packed by the Python packer parses and serves every asset", "[www]")
{
  const auto bytes = Fixture();
  Www::Image image = ParseOk(bytes);

  TEST_ASSERT_EQUAL_size_t(bytes.size(), image.ImageSize());
  TEST_ASSERT_EQUAL_size_t(44, image.Count());  // 4 named files + 40 under many/

  auto index = image.Find("index.html");
  TEST_ASSERT_TRUE(index.has_value());
  TEST_ASSERT_EQUAL_STRING("<!doctype html><title>OpenShock</title>", Body(*index).c_str());
  TEST_ASSERT_TRUE(index->gzip());  // packed with --gzip: the flag is what is tested, the bytes are plain
  TEST_ASSERT_FALSE(index->immutable());

  auto chunk = image.Find("_app/immutable/chunk.abc123.js");
  TEST_ASSERT_TRUE(chunk.has_value());
  TEST_ASSERT_TRUE(chunk->immutable());
  TEST_ASSERT_EQUAL_STRING("export default 1;", Body(*chunk).c_str());

  TEST_ASSERT_EQUAL_STRING("MIT", Body(*image.Find("LICENSES.txt")).c_str());
  TEST_ASSERT_TRUE(image.Find("logo.svg").has_value());
  for (int i = 0; i < 40; ++i) {
    std::string path = "many/f" + std::to_string(i) + ".txt";
    auto asset       = image.Find(path);
    TEST_ASSERT_TRUE_MESSAGE(asset.has_value(), path.c_str());
    TEST_ASSERT_EQUAL_STRING(("file " + std::to_string(i)).c_str(), Body(*asset).c_str());
    TEST_ASSERT_EQUAL_STRING(path.c_str(), std::string(asset->path).c_str());
  }

  TEST_ASSERT_FALSE(image.Find("missing").has_value());
  TEST_ASSERT_FALSE(image.Find("/index.html").has_value());  // stored without the leading slash
  TEST_ASSERT_FALSE(image.Find("").has_value());
  TEST_ASSERT_FALSE(image.Find("index.htm").has_value());
  TEST_ASSERT_FALSE(image.Find("index.html.gz").has_value());
}

TEST_CASE("Entries are sorted and every body is 4-byte aligned", "[www]")
{
  Www::Image image = ParseOk(Fixture());
  std::string previous;
  for (std::size_t i = 0; i < image.Count(); ++i) {
    Www::Asset asset = image.At(i);
    std::string path(asset.path);
    TEST_ASSERT_TRUE_MESSAGE(previous < path, "paths out of order");
    previous = path;
    TEST_ASSERT_EQUAL_size_t(0, reinterpret_cast<std::uintptr_t>(asset.body.data()) % Www::kAlign);
  }
}

TEST_CASE("The bytes after the image are ignored, as on a partition larger than it", "[www]")
{
  auto bytes             = Fixture();
  const std::size_t size = bytes.size();
  bytes.insert(bytes.end(), 4096, 0xFF);
  Www::Image image = ParseOk(bytes);
  TEST_ASSERT_EQUAL_size_t(size, image.ImageSize());
  TEST_ASSERT_EQUAL_size_t(size, image.Bytes().size());
}

TEST_CASE("An empty image is valid and finds nothing", "[www]")
{
  Www::Image image = ParseOk(ReadFile("empty.bin"));
  TEST_ASSERT_EQUAL_size_t(0, image.Count());
  TEST_ASSERT_EQUAL_size_t(Www::kHeaderSize, image.ImageSize());
  TEST_ASSERT_FALSE(image.Find("index.html").has_value());
}

TEST_CASE("Every truncation is rejected", "[www]")
{
  const auto bytes = Fixture();
  for (std::size_t len : {std::size_t(0), std::size_t(4), Www::kHeaderSize - 1, Www::kHeaderSize, Www::kHeaderSize + 40, bytes.size() - 1}) {
    std::vector<uint8_t> cut(bytes.begin(), bytes.begin() + len);
    auto image = Www::Image::Parse(cut);
    TEST_ASSERT_FALSE_MESSAGE(image.has_value(), "a truncated image parsed");
  }
}

TEST_CASE("Each corrupted header field is rejected with its own reason", "[www]")
{
  auto bytes = Fixture();
  bytes[0] ^= 0xFF;
  ExpectError(bytes, Www::ParseError::BadMagic);

  bytes = Fixture();
  Put16(bytes, 4, 2);
  ExpectError(bytes, Www::ParseError::BadVersion);

  bytes = Fixture();
  Put16(bytes, 6, 60000);  // more entries than the directory holds
  ExpectError(bytes, Www::ParseError::BadDirectory);

  bytes = Fixture();
  Put32(bytes, 8, 2);  // smaller than the entries
  ExpectError(bytes, Www::ParseError::BadDirectory);

  bytes = Fixture();
  Put32(bytes, 8, 0x100000);  // past the end
  ExpectError(bytes, Www::ParseError::BadDirectory);

  bytes = Fixture();
  Put32(bytes, 8, Get16(bytes, 6) * Www::kEntrySize + 2);  // unaligned
  ExpectError(bytes, Www::ParseError::BadDirectory);

  bytes = Fixture();
  bytes[12] ^= 0x01;  // the hash itself
  ExpectError(bytes, Www::ParseError::HashMismatch);

  bytes     = Fixture();
  bytes[28] = 0x01;  // reserved
  ExpectError(bytes, Www::ParseError::BadReserved);

  bytes = Fixture();
  bytes[bytes.size() - 1] ^= 0x01;  // a body byte: the hash no longer matches
  ExpectError(bytes, Www::ParseError::HashMismatch);
}

TEST_CASE("Each corrupted entry field is rejected", "[www]")
{
  const std::size_t entry0 = Www::kHeaderSize;

  auto bytes = Fixture();
  Put16(bytes, entry0, 0xFFFF);  // path offset past the string table
  ExpectError(bytes, Www::ParseError::BadPath);

  bytes = Fixture();
  Put16(bytes, entry0 + 2, 0x8000);  // unknown flag
  ExpectError(bytes, Www::ParseError::BadFlags);

  bytes = Fixture();
  Put32(bytes, entry0 + 12, 1);  // reserved
  ExpectError(bytes, Www::ParseError::BadReserved);

  bytes = Fixture();
  Put32(bytes, entry0 + 4, 0x10);  // body inside the header
  ExpectError(bytes, Www::ParseError::BadBody);

  bytes = Fixture();
  Put32(bytes, entry0 + 4, Get16(bytes, 6) * 0 + Www::kHeaderSize + 2);  // unaligned
  ExpectError(bytes, Www::ParseError::BadBody);

  bytes = Fixture();
  Put32(bytes, entry0 + 8, 0x7FFFFFFF);  // size past the end
  ExpectError(bytes, Www::ParseError::BadBody);

  bytes = Fixture();
  Put32(bytes, entry0 + 8, 0xFFFFFFF0);  // offset + size overflows
  ExpectError(bytes, Www::ParseError::BadBody);

  // Swap the first two entries' path offsets: no longer increasing
  bytes         = Fixture();
  uint16_t off0 = Get16(bytes, entry0);
  uint16_t off1 = Get16(bytes, entry0 + Www::kEntrySize);
  Put16(bytes, entry0, off1);
  Put16(bytes, entry0 + Www::kEntrySize, off0);
  ExpectError(bytes, Www::ParseError::NotSorted);

  // The last path loses its terminator (it is the last byte of the string table, before the padding)
  bytes = Fixture();
  {
    const uint16_t count      = Get16(bytes, 6);
    const std::size_t strings = Www::kHeaderSize + count * Www::kEntrySize;
    const std::size_t dirEnd  = Www::kHeaderSize + (bytes[8] | (bytes[9] << 8) | (bytes[10] << 16) | (static_cast<uint32_t>(bytes[11]) << 24));
    for (std::size_t i = strings; i < dirEnd; ++i) {
      bytes[i] = 'x';  // every NUL gone
    }
    ExpectError(bytes, Www::ParseError::BadPath);
  }
}

TEST_CASE("A failing hasher is reported, not treated as a match", "[www]")
{
  HostTest::FailNextHash = true;
  auto image             = Www::Image::Parse(Fixture());
  TEST_ASSERT_FALSE(image.has_value());
  TEST_ASSERT_EQUAL_STRING(Www::ParseErrorName(Www::ParseError::HashFailed), Www::ParseErrorName(image.error()));
}
