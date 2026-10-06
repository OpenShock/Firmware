// StringWriter / json_generator output: shape, escaping, flush boundary.
#include "unity.h"

#include "json/Json.h"

#include <string>
#include <string_view>

using namespace OpenShock;

TEST_CASE("generate: empty object and empty array", "[osjson][gen]")
{
  {
    JSON::StringWriter w;
    json_gen_start_object(w.gen());
    json_gen_end_object(w.gen());
    TEST_ASSERT_TRUE(w.finish() == "{}");
  }
  {
    JSON::StringWriter w;
    json_gen_start_array(w.gen());
    json_gen_end_array(w.gen());
    TEST_ASSERT_TRUE(w.finish() == "[]");
  }
}

TEST_CASE("generate: flat object with mixed scalar types", "[osjson][gen]")
{
  JSON::StringWriter w;
  json_gen_str_t* g = w.gen();
  json_gen_start_object(g);
  json_gen_obj_set_string(g, "s", "hi");
  json_gen_obj_set_int(g, "n", -7);
  json_gen_obj_set_bool(g, "b", true);
  json_gen_obj_set_null(g, "z");
  json_gen_end_object(g);

  TEST_ASSERT_TRUE(w.finish() == R"({"s":"hi","n":-7,"b":true,"z":null})");
}

TEST_CASE("generate: nested objects and arrays", "[osjson][gen]")
{
  JSON::StringWriter w;
  json_gen_str_t* g = w.gen();
  json_gen_start_object(g);
  json_gen_push_object(g, "outer");
  json_gen_obj_set_int(g, "x", 1);
  json_gen_push_array(g, "list");
  json_gen_arr_set_int(g, 10);
  json_gen_arr_set_int(g, 20);
  json_gen_pop_array(g);
  json_gen_pop_object(g);
  json_gen_end_object(g);

  TEST_ASSERT_TRUE(w.finish() == R"({"outer":{"x":1,"list":[10,20]}})");
}

TEST_CASE("generate: array of objects", "[osjson][gen]")
{
  JSON::StringWriter w;
  json_gen_str_t* g = w.gen();
  json_gen_start_array(g);
  for (int i = 1; i <= 3; ++i) {
    json_gen_start_object(g);
    json_gen_obj_set_int(g, "id", i);
    json_gen_end_object(g);
  }
  json_gen_end_array(g);

  TEST_ASSERT_TRUE(w.finish() == R"([{"id":1},{"id":2},{"id":3}])");
}

// json_generator 2.x escapes string values itself (RFC 8259 section 7), which is
// why JSON::objSetString / arrSetString must hand it the plain text: escaping
// there as well would escape twice. The two tests below pin the library side.
TEST_CASE("generate: raw json_gen_obj_set_string escapes quotes/backslashes", "[osjson][gen][raw]")
{
  JSON::StringWriter w;
  json_gen_str_t* g = w.gen();
  json_gen_start_object(g);
  json_gen_obj_set_string(g, "k", R"(a"b\c)");  // value bytes: a " b \ c
  json_gen_end_object(g);

  TEST_ASSERT_TRUE(w.finish() == R"({"k":"a\"b\\c"})");
}

TEST_CASE("generate: raw json_gen_obj_set_string escapes control chars", "[osjson][gen][raw]")
{
  JSON::StringWriter w;
  json_gen_str_t* g = w.gen();
  json_gen_start_object(g);
  json_gen_obj_set_string(g, "k", "line1\nline2\ttab");
  json_gen_end_object(g);

  TEST_ASSERT_TRUE(w.finish() == R"({"k":"line1\nline2\ttab"})");
}

// ---- JSON::objSetString / arrSetString: escaped exactly once ----

TEST_CASE("escape: objSetString escapes quotes and backslashes", "[osjson][gen][escape]")
{
  JSON::StringWriter w;
  json_gen_str_t* g = w.gen();
  json_gen_start_object(g);
  JSON::objSetString(g, "k", R"(a"b\c)");  // value bytes: a " b \ c
  json_gen_end_object(g);

  std::string out = w.finish();
  TEST_ASSERT_TRUE(out == R"({"k":"a\"b\\c"})");

  // escaped exactly once: valid JSON that decodes back to the original bytes
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(out));
  TEST_ASSERT_TRUE(doc.root().isObject());
  std::string back;
  TEST_ASSERT_TRUE(doc.root()["k"].tryGetStr(back));
  TEST_ASSERT_TRUE(back == R"(a"b\c)");
}

TEST_CASE("escape: objSetString escapes the short-form control chars", "[osjson][gen][escape]")
{
  JSON::StringWriter w;
  json_gen_str_t* g = w.gen();
  json_gen_start_object(g);
  JSON::objSetString(g, "k", "\n\t\r\b\f");  // LF TAB CR BS FF
  json_gen_end_object(g);

  TEST_ASSERT_TRUE(w.finish() == R"({"k":"\n\t\r\b\f"})");
}

TEST_CASE("escape: objSetString \\u-escapes other control chars", "[osjson][gen][escape]")
{
  std::string value;
  value += '\x01';
  value += '\x1f';

  JSON::StringWriter w;
  json_gen_str_t* g = w.gen();
  json_gen_start_object(g);
  JSON::objSetString(g, "k", value);
  json_gen_end_object(g);

  TEST_ASSERT_TRUE(w.finish() == "{\"k\":\"\\u0001\\u001f\"}");
}

TEST_CASE("escape: RFC 8259 boundaries (NUL, solidus, DEL, 0x1F/0x20)", "[osjson][gen][escape]")
{
  // U+0000 must be escaped as 0000 (embedded NUL survives via string_view).
  {
    std::string v;
    v += '\0';
    JSON::StringWriter w;
    json_gen_str_t* g = w.gen();
    json_gen_start_object(g);
    JSON::objSetString(g, "k", v);
    json_gen_end_object(g);
    TEST_ASSERT_TRUE(w.finish() == "{\"k\":\"\\u0000\"}");
  }
  // '/' (0x2F) is NOT required to be escaped -> passthrough.
  {
    JSON::StringWriter w;
    json_gen_str_t* g = w.gen();
    json_gen_start_object(g);
    JSON::objSetString(g, "k", "a/b");
    json_gen_end_object(g);
    TEST_ASSERT_TRUE(w.finish() == R"({"k":"a/b"})");
  }
  // 0x1F is the last control char (escaped); 0x20 (space) and 0x7F (DEL) are
  // both in the RFC `unescaped` set -> passthrough.
  {
    std::string v;
    v += '\x1f';
    v += ' ';
    v += '\x7f';
    JSON::StringWriter w;
    json_gen_str_t* g = w.gen();
    json_gen_start_object(g);
    JSON::objSetString(g, "k", v);
    json_gen_end_object(g);
    TEST_ASSERT_TRUE(w.finish() == "{\"k\":\"\\u001f \x7f\"}");
  }
}

TEST_CASE("escape: plain and UTF-8 strings pass through unchanged", "[osjson][gen][escape]")
{
  {
    JSON::StringWriter w;
    json_gen_str_t* g = w.gen();
    json_gen_start_object(g);
    JSON::objSetString(g, "k", "hello world");
    json_gen_end_object(g);
    TEST_ASSERT_TRUE(w.finish() == R"({"k":"hello world"})");
  }
  {
    // UTF-8 multibyte (bytes >= 0x20) must not be touched.
    JSON::StringWriter w;
    json_gen_str_t* g = w.gen();
    json_gen_start_object(g);
    JSON::objSetString(g, "k", "caf\xc3\xa9");  // "café"
    json_gen_end_object(g);
    TEST_ASSERT_TRUE(w.finish() == "{\"k\":\"caf\xc3\xa9\"}");
  }
}

TEST_CASE("escape: arrSetString escapes array string elements", "[osjson][gen][escape]")
{
  JSON::StringWriter w;
  json_gen_str_t* g = w.gen();
  json_gen_start_array(g);
  JSON::arrSetString(g, R"(a"b)");
  JSON::arrSetString(g, "plain");
  json_gen_end_array(g);

  std::string out = w.finish();
  TEST_ASSERT_TRUE(out == R"(["a\"b","plain"])");

  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(out));
  TEST_ASSERT_EQUAL_INT(2, doc.root().count());
}

TEST_CASE("escape: a WiFi password with a quote serializes to valid JSON", "[osjson][gen][escape]")
{
  // The real-world motivation: a password containing a double quote.
  JSON::StringWriter w;
  json_gen_str_t* g = w.gen();
  json_gen_start_object(g);
  JSON::objSetString(g, "ssid", "Home");
  JSON::objSetString(g, "password", R"(p@ss"word\)");
  json_gen_end_object(g);
  std::string out = w.finish();

  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(out));  // would be malformed via the raw call
  JSON::JsonView root = doc.root();

  std::string ssid;
  TEST_ASSERT_TRUE(root["ssid"].tryGetStr(ssid));
  TEST_ASSERT_TRUE(ssid == "Home");
  // the password decodes back to exactly what was written
  std::string pw;
  TEST_ASSERT_TRUE(root["password"].tryGetStr(pw));
  TEST_ASSERT_TRUE(pw == R"(p@ss"word\)");
}

TEST_CASE("generate: value larger than the 256-byte flush buffer", "[osjson][gen]")
{
  // Forces StringWriter::flushCb to run several times; output must be intact.
  std::string big(1000, 'x');

  JSON::StringWriter w;
  json_gen_str_t* g = w.gen();
  json_gen_start_object(g);
  json_gen_obj_set_string(g, "data", big.c_str());
  json_gen_end_object(g);
  std::string out = w.finish();

  std::string expected = "{\"data\":\"" + big + "\"}";
  TEST_ASSERT_EQUAL_size_t(expected.size(), out.size());
  TEST_ASSERT_TRUE(out == expected);
}

TEST_CASE("generate -> parse round-trip preserves scalar values", "[osjson][gen]")
{
  std::string out;
  {
    JSON::StringWriter w;
    json_gen_str_t* g = w.gen();
    json_gen_start_object(g);
    json_gen_obj_set_string(g, "ssid", "net");
    json_gen_obj_set_int(g, "id", 7);
    json_gen_obj_set_bool(g, "on", true);
    json_gen_push_array(g, "nums");
    json_gen_arr_set_int(g, 1);
    json_gen_arr_set_int(g, 2);
    json_gen_arr_set_int(g, 3);
    json_gen_pop_array(g);
    json_gen_end_object(g);
    out = w.finish();
  }

  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(out));
  JSON::JsonView root = doc.root();

  std::string ssid;
  TEST_ASSERT_TRUE(root["ssid"].tryGetStr(ssid));
  TEST_ASSERT_TRUE(ssid == "net");

  int64_t id = 0;
  TEST_ASSERT_TRUE(root["id"].tryGetI64(id));
  TEST_ASSERT_EQUAL_INT64(7, id);

  bool on = false;
  TEST_ASSERT_TRUE(root["on"].tryGetBool(on));
  TEST_ASSERT_TRUE(on);

  TEST_ASSERT_EQUAL_INT(3, root["nums"].count());
  int64_t sum = 0, v = 0;
  for (int i = 0; i < root["nums"].count(); ++i) {
    TEST_ASSERT_TRUE(root["nums"].at(i).tryGetI64(v));
    sum += v;
  }
  TEST_ASSERT_EQUAL_INT64(6, sum);
}
