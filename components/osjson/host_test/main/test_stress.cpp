// Scale / limits: token-buffer growth, the parse token cap, deep nesting,
// and large generate->parse round-trips.
#include "unity.h"

#include "json/Json.h"

#include <cstdint>
#include <string>
#include <string_view>

using namespace OpenShock;

TEST_CASE("stress: large flat array forces the token buffer to grow", "[osjson][stress]")
{
  // 1000 elements -> ~1001 tokens, well past the initial capacity of 32, so the
  // grow-and-retry path in parse() runs several times.
  const int N      = 1000;
  std::string json = "[";
  for (int i = 0; i < N; ++i) {
    if (i) json += ',';
    json += std::to_string(i);
  }
  json += ']';

  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  JSON::JsonView root = doc.root();
  TEST_ASSERT_TRUE(root.isArray());
  TEST_ASSERT_EQUAL_INT(N, root.count());

  int64_t v = 0;
  TEST_ASSERT_TRUE(root.at(0).tryGetI64(v));
  TEST_ASSERT_EQUAL_INT64(0, v);
  TEST_ASSERT_TRUE(root.at(500).tryGetI64(v));
  TEST_ASSERT_EQUAL_INT64(500, v);
  TEST_ASSERT_TRUE(root.at(N - 1).tryGetI64(v));
  TEST_ASSERT_EQUAL_INT64(N - 1, v);
  TEST_ASSERT_FALSE(root.at(N).valid());
}

TEST_CASE("stress: object with many members, every key resolvable", "[osjson][stress]")
{
  const int N      = 300;
  std::string json = "{";
  for (int i = 0; i < N; ++i) {
    if (i) json += ',';
    json += "\"k" + std::to_string(i) + "\":" + std::to_string(i * 2);
  }
  json += '}';

  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  JSON::JsonView root = doc.root();
  TEST_ASSERT_EQUAL_INT(N, root.count());

  // spot-check first, middle, last, and a miss
  int64_t v = 0;
  TEST_ASSERT_TRUE(root["k0"].tryGetI64(v));
  TEST_ASSERT_EQUAL_INT64(0, v);
  TEST_ASSERT_TRUE(root["k150"].tryGetI64(v));
  TEST_ASSERT_EQUAL_INT64(300, v);
  TEST_ASSERT_TRUE(root["k299"].tryGetI64(v));
  TEST_ASSERT_EQUAL_INT64(598, v);
  TEST_ASSERT_FALSE(root["k300"].valid());
}

TEST_CASE("stress: document needing > 16384 tokens is rejected by the cap", "[osjson][stress]")
{
  // Array of 20000 numbers => 20001 tokens > cap => parse() bails out to false
  // rather than growing unboundedly.
  const int N      = 20000;
  std::string json = "[";
  for (int i = 0; i < N; ++i) {
    if (i) json += ',';
    json += '1';
  }
  json += ']';

  JSON::JsonDocument doc;
  TEST_ASSERT_FALSE(doc.parse(json));
  TEST_ASSERT_FALSE(doc.root().valid());
}

TEST_CASE("stress: just under the cap still parses", "[osjson][stress]")
{
  // 16000 elements -> 16001 tokens, needs capacity 32768? No: capacity doubles
  // 32,64,...,16384. 16001 <= 16384 so it fits without tripping the cap.
  const int N      = 16000;
  std::string json = "[";
  for (int i = 0; i < N; ++i) {
    if (i) json += ',';
    json += '7';
  }
  json += ']';

  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  TEST_ASSERT_EQUAL_INT(N, doc.root().count());
  int64_t v = 0;
  TEST_ASSERT_TRUE(doc.root().at(N - 1).tryGetI64(v));
  TEST_ASSERT_EQUAL_INT64(7, v);
}

TEST_CASE("stress: moderately deep nesting navigates correctly", "[osjson][stress]")
{
  // Build {"a":{"a":{...{"v":depth}...}}} and walk back down to the leaf.
  const int depth = 40;
  std::string json;
  for (int i = 0; i < depth; ++i) json += "{\"a\":";
  json += std::to_string(depth);
  for (int i = 0; i < depth; ++i) json += "}";

  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));

  JSON::JsonView v = doc.root();
  for (int i = 0; i < depth; ++i) {
    TEST_ASSERT_TRUE(v.isObject());
    v = v["a"];
  }
  int64_t leaf = 0;
  TEST_ASSERT_TRUE(v.tryGetI64(leaf));
  TEST_ASSERT_EQUAL_INT64(depth, leaf);
}

TEST_CASE("stress: large generate -> parse round-trip", "[osjson][stress]")
{
  const int N = 2000;

  std::string out;
  {
    JSON::StringWriter w;
    json_gen_str_t* g = w.gen();
    json_gen_start_array(g);
    for (int i = 0; i < N; ++i) {
      json_gen_start_object(g);
      json_gen_obj_set_int(g, "i", i);
      json_gen_end_object(g);
    }
    json_gen_end_array(g);
    out = w.finish();
  }

  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(out));
  JSON::JsonView root = doc.root();
  TEST_ASSERT_EQUAL_INT(N, root.count());

  int64_t v = 0;
  TEST_ASSERT_TRUE(root.at(0)["i"].tryGetI64(v));
  TEST_ASSERT_EQUAL_INT64(0, v);
  TEST_ASSERT_TRUE(root.at(N - 1)["i"].tryGetI64(v));
  TEST_ASSERT_EQUAL_INT64(N - 1, v);
}
