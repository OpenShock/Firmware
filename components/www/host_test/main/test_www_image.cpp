// Parsing images written by scripts/utils/wwwimage.py (the fixtures CMake packs at configure time), and refusing
// every way a field of one can be wrong.
#include "unity.h"

#include "Hashing.h"
#include "www/WwwImage.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace OpenShock;

// The fixture's node table, breadth-first, names folded to lowercase ('_' sorts before letters):
//   0 root (dir)              children [1, 6)
//   1 _app (dir)              children [6, 7)
//   2 index.html
//   3 licenses.txt            (LICENSES.txt on disk)
//   4 logo.svg
//   5 many (dir)              children [7, 47)
//   6 immutable (dir)         children [47, 48)
//   7..46 many/f*.txt
//   47 chunk.abc123.js
namespace {
  constexpr std::size_t kNodeCount = 48;

  std::size_t NodeAt(std::size_t index)
  {
    return Www::kHeaderSize + Www::kNodeSize * index;
  }

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
  TEST_ASSERT_EQUAL_size_t(kNodeCount, image.Count());

  auto index = image.Find("index.html");
  TEST_ASSERT_TRUE(index.has_value());
  TEST_ASSERT_EQUAL_STRING("<!doctype html><title>OpenShock</title>", Body(*index).c_str());  // the fixture is not really gzipped
  TEST_ASSERT_EQUAL_STRING("index.html", std::string(index->name).c_str());
  TEST_ASSERT_FALSE(index->immutable());

  auto chunk = image.Find("_app/immutable/chunk.abc123.js");
  TEST_ASSERT_TRUE(chunk.has_value());
  TEST_ASSERT_TRUE(chunk->immutable());
  TEST_ASSERT_EQUAL_STRING("chunk.abc123.js", std::string(chunk->name).c_str());
  TEST_ASSERT_EQUAL_STRING("export default 1;", Body(*chunk).c_str());

  // Names are stored lowercase and looked up case-insensitively
  auto licenses = image.Find("LICENSES.txt");
  TEST_ASSERT_TRUE(licenses.has_value());
  TEST_ASSERT_EQUAL_STRING("MIT", Body(*licenses).c_str());
  TEST_ASSERT_EQUAL_STRING("licenses.txt", std::string(licenses->name).c_str());
  TEST_ASSERT_TRUE(image.Find("licenses.txt").has_value());
  TEST_ASSERT_TRUE(image.Find("INDEX.HTML").has_value());
  TEST_ASSERT_TRUE(image.Find("_App/Immutable/Chunk.ABC123.js").has_value());
  TEST_ASSERT_TRUE(image.Find("logo.svg").has_value());
  for (int i = 0; i < 40; ++i) {
    std::string path = "many/f" + std::to_string(i) + ".txt";
    auto asset       = image.Find(path);
    TEST_ASSERT_TRUE_MESSAGE(asset.has_value(), path.c_str());
    TEST_ASSERT_EQUAL_STRING(("file " + std::to_string(i)).c_str(), Body(*asset).c_str());
  }

  TEST_ASSERT_FALSE(image.Find("missing").has_value());
  TEST_ASSERT_FALSE(image.Find("many/missing").has_value());
  TEST_ASSERT_FALSE(image.Find("/index.html").has_value());  // stored without the leading slash
  TEST_ASSERT_FALSE(image.Find("").has_value());
  TEST_ASSERT_FALSE(image.Find("index.htm").has_value());
  TEST_ASSERT_FALSE(image.Find("index.html.gz").has_value());
  TEST_ASSERT_FALSE(image.Find("_app").has_value());  // a directory is not an asset
  TEST_ASSERT_FALSE(image.Find("_app/immutable").has_value());
  TEST_ASSERT_FALSE(image.Find("_app/").has_value());
  TEST_ASSERT_FALSE(image.Find("index.html/").has_value());
  TEST_ASSERT_FALSE(image.Find("many/f5.txt/x").has_value());  // a file is not a directory
  TEST_ASSERT_FALSE(image.Find("many//f5.txt").has_value());
}

TEST_CASE("The root is node 0, directory runs are sorted and every body is 4-byte aligned", "[www]")
{
  const auto bytes = Fixture();
  Www::Image image = ParseOk(bytes);

  Www::Node root = image.At(0);
  TEST_ASSERT_TRUE(root.isDirectory());
  TEST_ASSERT_EQUAL_size_t(0, root.name.size());
  TEST_ASSERT_EQUAL_UINT32(1, root.offset);
  TEST_ASSERT_EQUAL_UINT32(5, root.size);

  std::size_t files = 0, directories = 0;
  for (std::size_t i = 0; i < image.Count(); ++i) {
    Www::Node node = image.At(i);
    if (node.isDirectory()) {
      ++directories;
      for (std::size_t j = node.offset + 1; j < node.offset + node.size; ++j) {
        TEST_ASSERT_TRUE_MESSAGE(image.At(j - 1).name < image.At(j).name, "children out of order");
      }
    } else {
      ++files;
      TEST_ASSERT_EQUAL_size_t(0, reinterpret_cast<std::uintptr_t>(bytes.data() + node.offset) % Www::kAlign);
    }
  }
  TEST_ASSERT_EQUAL_size_t(44, files);       // 4 named files + 40 under many/
  TEST_ASSERT_EQUAL_size_t(4, directories);  // root, _app, _app/immutable, many
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
  const auto bytes = ReadFile("empty.bin");
  Www::Image image = ParseOk(bytes);
  TEST_ASSERT_EQUAL_size_t(1, image.Count());  // just the root
  TEST_ASSERT_EQUAL_size_t(bytes.size(), image.ImageSize());
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
  Put16(bytes, 6, 60000);  // more nodes than the directory holds
  ExpectError(bytes, Www::ParseError::BadDirectory);

  bytes = Fixture();
  Put16(bytes, 6, 0);  // no root
  ExpectError(bytes, Www::ParseError::BadTree);

  bytes = Fixture();
  Put32(bytes, 8, 2);  // smaller than the nodes
  ExpectError(bytes, Www::ParseError::BadDirectory);

  bytes = Fixture();
  Put32(bytes, 8, 0x100000);  // past the end
  ExpectError(bytes, Www::ParseError::BadDirectory);

  bytes = Fixture();
  Put32(bytes, 8, Get16(bytes, 6) * Www::kNodeSize + 2);  // unaligned
  ExpectError(bytes, Www::ParseError::BadDirectory);

  bytes = Fixture();
  bytes[12] ^= 0x01;  // the hash itself
  ExpectError(bytes, Www::ParseError::HashMismatch);

  bytes     = Fixture();
  bytes[44] = 0x01;  // first reserved byte, right after the hash
  ExpectError(bytes, Www::ParseError::BadReserved);

  bytes     = Fixture();
  bytes[63] = 0x01;  // last reserved byte
  ExpectError(bytes, Www::ParseError::BadReserved);

  bytes = Fixture();
  bytes[bytes.size() - 1] ^= 0x01;  // a body byte: the hash no longer matches
  ExpectError(bytes, Www::ParseError::HashMismatch);
}

TEST_CASE("Each corrupted node field is rejected", "[www]")
{
  const std::size_t root = NodeAt(0);
  const std::size_t file = NodeAt(2);  // index.html

  auto bytes = Fixture();
  Put16(bytes, file, 0xFFFF);  // name offset past the string table
  ExpectError(bytes, Www::ParseError::BadName);

  bytes = Fixture();
  Put16(bytes, file + 2, 0x8000);  // unknown flag
  ExpectError(bytes, Www::ParseError::BadFlags);

  bytes = Fixture();
  Put32(bytes, file + 12, 1);  // reserved
  ExpectError(bytes, Www::ParseError::BadReserved);

  bytes = Fixture();
  Put32(bytes, file + 4, 0x10);  // body inside the header
  ExpectError(bytes, Www::ParseError::BadBody);

  bytes = Fixture();
  Put32(bytes, file + 4, Www::kHeaderSize + 2);  // unaligned
  ExpectError(bytes, Www::ParseError::BadBody);

  bytes = Fixture();
  Put32(bytes, file + 8, 0x7FFFFFFF);  // size past the end
  ExpectError(bytes, Www::ParseError::BadBody);

  bytes = Fixture();
  Put32(bytes, file + 8, 0xFFFFFFF0);  // offset + size overflows
  ExpectError(bytes, Www::ParseError::BadBody);

  // An uppercase letter in a stored name
  bytes = Fixture();
  {
    static constexpr char kName[] = "index.html";
    auto it                       = std::search(bytes.begin(), bytes.end(), kName, kName + sizeof(kName));  // includes the NUL
    TEST_ASSERT_TRUE(it != bytes.end());
    *it = 'I';
    ExpectError(bytes, Www::ParseError::BadName);
  }

  // The last path loses its terminator (it is the last byte of the string table, before the padding)
  bytes = Fixture();
  {
    const uint16_t count      = Get16(bytes, 6);
    const std::size_t strings = Www::kHeaderSize + count * Www::kNodeSize;
    const std::size_t dirEnd  = Www::kHeaderSize + (bytes[8] | (bytes[9] << 8) | (bytes[10] << 16) | (static_cast<uint32_t>(bytes[11]) << 24));
    for (std::size_t i = strings; i < dirEnd; ++i) {
      bytes[i] = 'x';  // every NUL gone
    }
    ExpectError(bytes, Www::ParseError::BadName);
  }

  // The root gets a non-empty name (index.html's)
  bytes = Fixture();
  Put16(bytes, root, Get16(bytes, file));
  ExpectError(bytes, Www::ParseError::BadTree);
}

TEST_CASE("Each corrupted directory is rejected", "[www]")
{
  const std::size_t root     = NodeAt(0);
  const std::size_t app      = NodeAt(1);  // _app (dir), children [6, 7)
  const std::size_t index    = NodeAt(2);  // index.html
  const std::size_t licenses = NodeAt(3);  // licenses.txt
  const std::size_t imm      = NodeAt(6);  // _app/immutable (dir), children [47, 48)

  auto bytes = Fixture();
  Put16(bytes, root + 2, 0);  // the root is not a directory
  ExpectError(bytes, Www::ParseError::BadTree);

  bytes = Fixture();
  Put32(bytes, root + 4, 2);  // the root's children must start at 1
  ExpectError(bytes, Www::ParseError::BadTree);

  bytes = Fixture();
  Put32(bytes, root + 8, 100);  // more children than nodes
  ExpectError(bytes, Www::ParseError::BadTree);

  bytes = Fixture();
  Put16(bytes, app + 2, Www::kFlagDirectory | Www::kFlagImmutable);  // immutable directory
  ExpectError(bytes, Www::ParseError::BadFlags);

  bytes = Fixture();
  Put32(bytes, app + 4, 7);  // _app's run must follow the root's
  ExpectError(bytes, Www::ParseError::BadTree);

  bytes = Fixture();
  Put32(bytes, imm + 8, 0);  // chunk.abc123.js then belongs to no directory
  ExpectError(bytes, Www::ParseError::BadTree);

  // Swap the names of index.html and licenses.txt: the root's children are no longer increasing
  bytes         = Fixture();
  uint16_t off2 = Get16(bytes, index);
  uint16_t off3 = Get16(bytes, licenses);
  Put16(bytes, index, off3);
  Put16(bytes, licenses, off2);
  ExpectError(bytes, Www::ParseError::NotSorted);
}

TEST_CASE("A failing hasher is reported, not treated as a match", "[www]")
{
  HostTest::FailNextHash = true;
  auto image             = Www::Image::Parse(Fixture());
  TEST_ASSERT_FALSE(image.has_value());
  TEST_ASSERT_EQUAL_STRING(Www::ParseErrorName(Www::ParseError::HashFailed), Www::ParseErrorName(image.error()));
}
