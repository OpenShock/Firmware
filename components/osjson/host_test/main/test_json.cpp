// Host (linux target) unit tests for the osjson component.
//
//   cd components/osjson/host_test
//   idf.py --preview set-target linux
//   idf.py build
//   ./build/osjson_host_test.elf
//
#include "unity.h"

#include "json/Json.h"

#include <cstring>
#include <string>
#include <string_view>

using namespace OpenShock;

TEST_CASE("parse scalar object", "[osjson]")
{
  static const char json[] = R"({"name":"hello","n":42,"flag":true,"neg":-7})";

  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));

  JSON::JsonView root = doc.root();
  TEST_ASSERT_TRUE(root.isObject());

  std::string_view s;
  TEST_ASSERT_TRUE(root["name"].tryGetStr(s));
  TEST_ASSERT_EQUAL_size_t(5, s.size());
  TEST_ASSERT_TRUE(s == "hello");
  // Zero-copy: the view must point straight into the source buffer.
  TEST_ASSERT_TRUE(s.data() >= json && s.data() < json + sizeof(json));

  int64_t n = 0;
  TEST_ASSERT_TRUE(root["n"].tryGetI64(n));
  TEST_ASSERT_EQUAL_INT64(42, n);

  int64_t neg = 0;
  TEST_ASSERT_TRUE(root["neg"].tryGetI64(neg));
  TEST_ASSERT_EQUAL_INT64(-7, neg);

  bool flag = false;
  TEST_ASSERT_TRUE(root["flag"].tryGetBool(flag));
  TEST_ASSERT_TRUE(flag);
}

TEST_CASE("missing key and type mismatch", "[osjson]")
{
  static const char json[] = R"({"name":"x","n":5})";

  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  JSON::JsonView root = doc.root();

  TEST_ASSERT_FALSE(root["missing"].valid());

  bool b = false;
  TEST_ASSERT_FALSE(root["name"].tryGetBool(b));  // string, not bool
  int64_t n = 0;
  TEST_ASSERT_FALSE(root["name"].tryGetI64(n));   // string, not number
  std::string_view s;
  TEST_ASSERT_FALSE(root["n"].tryGetStr(s));      // number, not string
}

TEST_CASE("nested objects and arrays", "[osjson]")
{
  static const char json[] = R"({"obj":{"x":1,"y":2},"arr":[10,20,30]})";

  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  JSON::JsonView root = doc.root();

  int64_t y = 0;
  TEST_ASSERT_TRUE(root["obj"]["y"].tryGetI64(y));
  TEST_ASSERT_EQUAL_INT64(2, y);

  JSON::JsonView arr = root["arr"];
  TEST_ASSERT_TRUE(arr.isArray());
  TEST_ASSERT_EQUAL_INT(3, arr.count());

  int64_t v = 0;
  TEST_ASSERT_TRUE(arr.at(1).tryGetI64(v));
  TEST_ASSERT_EQUAL_INT64(20, v);
  TEST_ASSERT_FALSE(arr.at(3).valid());  // out of range
}

TEST_CASE("malformed input is rejected", "[osjson]")
{
  JSON::JsonDocument doc;
  TEST_ASSERT_FALSE(doc.parse("{ not valid json"));
  TEST_ASSERT_FALSE(doc.parse(""));
  TEST_ASSERT_FALSE(doc.root().valid());
}

TEST_CASE("generate then parse round-trip", "[osjson]")
{
  std::string out;
  {
    JSON::StringWriter writer;
    json_gen_str_t* gen = writer.gen();
    json_gen_start_object(gen);
    json_gen_obj_set_string(gen, "ssid", "net");
    json_gen_obj_set_int(gen, "id", 7);
    json_gen_obj_set_bool(gen, "on", true);
    json_gen_end_object(gen);
    out = writer.finish();
  }

  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(out));
  JSON::JsonView root = doc.root();

  std::string_view ssid;
  TEST_ASSERT_TRUE(root["ssid"].tryGetStr(ssid));
  TEST_ASSERT_TRUE(ssid == "net");

  int64_t id = 0;
  TEST_ASSERT_TRUE(root["id"].tryGetI64(id));
  TEST_ASSERT_EQUAL_INT64(7, id);

  bool on = false;
  TEST_ASSERT_TRUE(root["on"].tryGetBool(on));
  TEST_ASSERT_TRUE(on);
}

// Host-only (linux target) test binary. FreeRTOS is mocked, so there is no ESP
// startup to call app_main - provide a plain main() that drives Unity directly.
extern "C" int main(void)
{
  UNITY_BEGIN();
  unity_run_all_tests();
  return UNITY_END();
}
