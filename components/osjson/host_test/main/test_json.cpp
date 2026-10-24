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
#include <optional>
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

  std::optional<std::string> s;
  s = root["name"].tryGetStr();
  TEST_ASSERT_TRUE(s.has_value());
  TEST_ASSERT_EQUAL_size_t(5, s->size());
  TEST_ASSERT_TRUE(*s == "hello");
  // Zero-copy: raw() must point straight into the source buffer.
  std::string_view raw = root["name"].raw();
  TEST_ASSERT_TRUE(raw.data() >= json && raw.data() < json + sizeof(json));

  std::optional<int64_t> n;
  n = root["n"].tryGetI64();
  TEST_ASSERT_TRUE(n.has_value());
  TEST_ASSERT_EQUAL_INT64(42, *n);

  std::optional<int64_t> neg;
  neg = root["neg"].tryGetI64();
  TEST_ASSERT_TRUE(neg.has_value());
  TEST_ASSERT_EQUAL_INT64(-7, *neg);

  std::optional<bool> flag;
  flag = root["flag"].tryGetBool();
  TEST_ASSERT_TRUE(flag.has_value());
  TEST_ASSERT_TRUE(*flag);
}

TEST_CASE("missing key and type mismatch", "[osjson]")
{
  static const char json[] = R"({"name":"x","n":5})";

  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  JSON::JsonView root = doc.root();

  TEST_ASSERT_FALSE(root["missing"].valid());

  TEST_ASSERT_FALSE(root["name"].tryGetBool().has_value());  // string, not bool
  TEST_ASSERT_FALSE(root["name"].tryGetI64().has_value());   // string, not number
  TEST_ASSERT_FALSE(root["n"].tryGetStr().has_value());      // number, not string
}

TEST_CASE("nested objects and arrays", "[osjson]")
{
  static const char json[] = R"({"obj":{"x":1,"y":2},"arr":[10,20,30]})";

  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  JSON::JsonView root = doc.root();

  std::optional<int64_t> y;
  y = root["obj"]["y"].tryGetI64();
  TEST_ASSERT_TRUE(y.has_value());
  TEST_ASSERT_EQUAL_INT64(2, *y);

  JSON::JsonView arr = root["arr"];
  TEST_ASSERT_TRUE(arr.isArray());
  TEST_ASSERT_EQUAL_INT(3, arr.count());

  std::optional<int64_t> v;
  v = arr.at(1).tryGetI64();
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_INT64(20, *v);
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

  std::optional<std::string> ssid;
  ssid = root["ssid"].tryGetStr();
  TEST_ASSERT_TRUE(ssid.has_value());
  TEST_ASSERT_TRUE(*ssid == "net");

  std::optional<int64_t> id;
  id = root["id"].tryGetI64();
  TEST_ASSERT_TRUE(id.has_value());
  TEST_ASSERT_EQUAL_INT64(7, *id);

  std::optional<bool> on;
  on = root["on"].tryGetBool();
  TEST_ASSERT_TRUE(on.has_value());
  TEST_ASSERT_TRUE(*on);
}

// Host-only (linux target) test binary. FreeRTOS is mocked, so there is no ESP
// startup to call app_main - provide a plain main() that drives Unity directly.
extern "C" int main(void)
{
  UNITY_BEGIN();
  unity_run_all_tests();
  return UNITY_END();
}
