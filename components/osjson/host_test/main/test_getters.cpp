// Typed getters (tryGetStr/Bool/I64/Double) and type predicates.
#include "unity.h"

#include "json/Json.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

using namespace OpenShock;

// ---- tryGetStr -------------------------------------------------------------

TEST_CASE("tryGetStr: strings succeed, non-strings fail", "[osjson][getters]")
{
  static const char json[] = R"({"s":"hi","n":5,"b":true,"z":null,"o":{},"a":[]})";
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  JSON::JsonView root = doc.root();

  auto sv = root["s"].tryGetStr();
  TEST_ASSERT_TRUE(sv.has_value());
  TEST_ASSERT_TRUE(*sv == "hi");

  TEST_ASSERT_FALSE(root["n"].tryGetStr().has_value());
  TEST_ASSERT_FALSE(root["b"].tryGetStr().has_value());
  TEST_ASSERT_FALSE(root["z"].tryGetStr().has_value());
  TEST_ASSERT_FALSE(root["o"].tryGetStr().has_value());
  TEST_ASSERT_FALSE(root["a"].tryGetStr().has_value());
  TEST_ASSERT_FALSE(root["missing"].tryGetStr().has_value());
}

TEST_CASE("tryGetStr: empty string value", "[osjson][getters]")
{
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(R"({"s":""})"));
  auto sv = doc.root()["s"].tryGetStr();
  TEST_ASSERT_TRUE(sv.has_value());
  TEST_ASSERT_EQUAL_size_t(0, sv->size());
  TEST_ASSERT_TRUE(sv->empty());
}

TEST_CASE("tryGetStr: escape sequences are decoded, raw() keeps them", "[osjson][getters][unescape]")
{
  static const char json[] = R"({"s":"a\nb\"c"})";
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  auto s = doc.root()["s"].tryGetStr();
  TEST_ASSERT_TRUE(s.has_value());
  TEST_ASSERT_TRUE(*s == "a\nb\"c");                        // a real newline and quote
  TEST_ASSERT_EQUAL_size_t(5, s->size());
  TEST_ASSERT_TRUE(doc.root()["s"].raw() == R"(a\nb\"c)");  // the token text, untouched
}

TEST_CASE("tryGetStr: every two-character escape", "[osjson][getters][unescape]")
{
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(R"({"s":"\"\\\/\b\f\n\r\t"})"));
  auto s = doc.root()["s"].tryGetStr();
  TEST_ASSERT_TRUE(s.has_value());
  TEST_ASSERT_TRUE(*s == "\"\\/\b\f\n\r\t");
}

TEST_CASE("tryGetStr: \\u escapes decode to UTF-8", "[osjson][getters][unescape]")
{
  struct Case {
    const char* json;
    std::string expected;
  };
  // clang-format off
  const Case cases[] = {
    {"{\"s\":\"\\u0041\"}",              "A"},                        // 1 byte
    {"{\"s\":\"\\u00e9\"}",              "\xc3\xa9"},                 // 2 bytes, lowercase hex
    {"{\"s\":\"\\u00E9\"}",              "\xc3\xa9"},                 // uppercase hex
    {"{\"s\":\"\\u20AC\"}",              "\xe2\x82\xac"},             // 3 bytes (euro sign)
    {"{\"s\":\"\\uFFFF\"}",              "\xef\xbf\xbf"},             // top of the BMP
    {"{\"s\":\"\\u001f\"}",              "\x1f"},                     // control character
    {"{\"s\":\"x\\u0022y\"}",            "x\"y"},                     // quote spelled as \u
    {"{\"s\":\"\\ud83d\\ude00\"}",       "\xf0\x9f\x98\x80"},         // surrogate pair, U+1F600
    {"{\"s\":\"\\uDBFF\\uDFFF\"}",       "\xf4\x8f\xbf\xbf"},         // highest pair, U+10FFFF
    {"{\"s\":\"caf\\u00e9 \\u20ac5\"}",  "caf\xc3\xa9 \xe2\x82\xac\x35"},
  };
  // clang-format on

  for (const Case& c : cases) {
    JSON::JsonDocument doc;
    TEST_ASSERT_TRUE_MESSAGE(doc.parse(c.json), c.json);
    auto s = doc.root()["s"].tryGetStr();
    TEST_ASSERT_TRUE_MESSAGE(s.has_value(), c.json);
    TEST_ASSERT_TRUE_MESSAGE(*s == c.expected, c.json);
  }
}

TEST_CASE("tryGetStr: \\u0000 decodes to an embedded NUL", "[osjson][getters][unescape]")
{
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(R"({"s":"a\u0000b"})"));
  auto s = doc.root()["s"].tryGetStr();
  TEST_ASSERT_TRUE(s.has_value());
  TEST_ASSERT_EQUAL_size_t(3, s->size());
  TEST_ASSERT_TRUE(*s == std::string("a\0b", 3));
}

TEST_CASE("tryGetStr: unpaired surrogates decode to U+FFFD", "[osjson][getters][unescape]")
{
  struct Case {
    const char* json;
    std::string expected;
  };
  // clang-format off
  const Case cases[] = {
    {"{\"s\":\"\\ud83d\"}",          "\xef\xbf\xbd"},              // high surrogate at the end
    {"{\"s\":\"\\ud83dx\"}",         "\xef\xbf\xbd\x78"},          // followed by a plain character ('x')
    {"{\"s\":\"\\ud83d\\u0041\"}",   "\xef\xbf\xbd\x41"},          // followed by a non-surrogate escape ('A')
    {"{\"s\":\"\\ude00\"}",          "\xef\xbf\xbd"},              // low surrogate on its own
    {"{\"s\":\"\\ud83d\\ud83d\"}",   "\xef\xbf\xbd\xef\xbf\xbd"},  // two high surrogates
  };
  // clang-format on

  for (const Case& c : cases) {
    JSON::JsonDocument doc;
    TEST_ASSERT_TRUE_MESSAGE(doc.parse(c.json), c.json);
    auto s = doc.root()["s"].tryGetStr();
    TEST_ASSERT_TRUE_MESSAGE(s.has_value(), c.json);
    TEST_ASSERT_TRUE_MESSAGE(*s == c.expected, c.json);
  }
}

TEST_CASE("tryGetStr: malformed escapes fail", "[osjson][getters][unescape]")
{
  // jsmn's default (non-strict) mode may tokenize these; the decoder must reject them.
  const char* inputs[] = {
    R"({"s":"\x41"})",
    R"({"s":"\u12G4"})",
    R"({"s":"\u12"})",
  };

  for (const char* input : inputs) {
    JSON::JsonDocument doc;
    if (!doc.parse(input)) {
      continue;  // already rejected by the tokenizer, which is fine too
    }
    TEST_ASSERT_FALSE_MESSAGE(doc.root()["s"].tryGetStr().has_value(), input);
  }
}

TEST_CASE("operator[]: keys written with escapes still match", "[osjson][getters][unescape]")
{
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(R"({"\u0069d":7,"a\"b":"q"})"));
  auto id = doc.root()["id"].tryGetI64();
  TEST_ASSERT_TRUE(id.has_value());
  TEST_ASSERT_EQUAL_INT64(7, *id);
  auto s = doc.root()["a\"b"].tryGetStr();
  TEST_ASSERT_TRUE(s.has_value());
  TEST_ASSERT_TRUE(*s == "q");
}

TEST_CASE("generate -> parse round trip restores every byte value", "[osjson][getters][unescape]")
{
  // All of ASCII (control characters, quote, backslash, DEL) plus multi-byte UTF-8.
  std::string value;
  for (int c = 0; c < 0x80; ++c) value += static_cast<char>(c);
  value += "caf\xc3\xa9 \xe2\x82\xac \xf0\x9f\x98\x80";

  JSON::StringWriter w;
  json_gen_str_t* g = w.gen();
  json_gen_start_object(g);
  JSON::objSetString(g, "k", value);
  json_gen_end_object(g);
  const std::string out = w.finish();

  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(out));
  auto back = doc.root()["k"].tryGetStr();
  TEST_ASSERT_TRUE(back.has_value());
  TEST_ASSERT_EQUAL_size_t(value.size(), back->size());
  TEST_ASSERT_TRUE(*back == value);
}

// ---- tryGetBool ------------------------------------------------------------

TEST_CASE("tryGetBool: true/false succeed, everything else fails", "[osjson][getters]")
{
  static const char json[] = R"({"t":true,"f":false,"n":1,"s":"true","z":null})";
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  JSON::JsonView root = doc.root();

  auto b = root["t"].tryGetBool();
  TEST_ASSERT_TRUE(b.has_value());
  TEST_ASSERT_TRUE(*b);
  b = root["f"].tryGetBool();
  TEST_ASSERT_TRUE(b.has_value());
  TEST_ASSERT_FALSE(*b);

  TEST_ASSERT_FALSE(root["n"].tryGetBool().has_value());  // number
  TEST_ASSERT_FALSE(root["s"].tryGetBool().has_value());  // the STRING "true"
  TEST_ASSERT_FALSE(root["z"].tryGetBool().has_value());  // null
  TEST_ASSERT_FALSE(root["missing"].tryGetBool().has_value());
}

// ---- tryGetI64 -------------------------------------------------------------

TEST_CASE("tryGetI64: valid integers including boundaries", "[osjson][getters]")
{
  static const char json[] = R"({"zero":0,"pos":123,"neg":-123,"i32over":2147483648,)"
                             R"("max":9223372036854775807,"min":-9223372036854775808})";
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  JSON::JsonView root = doc.root();

  std::optional<int64_t> v = root["zero"].tryGetI64();
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_INT64(0, *v);
  v = root["pos"].tryGetI64();
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_INT64(123, *v);
  v = root["neg"].tryGetI64();
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_INT64(-123, *v);
  v = root["i32over"].tryGetI64();
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_INT64(2147483648LL, *v);
  v = root["max"].tryGetI64();
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_INT64(INT64_MAX, *v);
  v = root["min"].tryGetI64();
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_INT64(INT64_MIN, *v);
}

TEST_CASE("tryGetI64: rejects non-integers and partial parses", "[osjson][getters]")
{
  static const char json[] = R"({"flt":1.5,"exp":"1e3","over":9223372036854775808,)"
                             R"("plus":"+5","hex":"0x1F","word":"abc","s":"42","b":true,"z":null})";
  // NOTE: exp/plus/hex/word/s are quoted so they are valid JSON string tokens;
  // tryGetI64 must still reject them because they are not numbers.
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  JSON::JsonView root = doc.root();

  TEST_ASSERT_FALSE(root["flt"].tryGetI64().has_value());   // 1.5 -> '.' is not a digit
  TEST_ASSERT_FALSE(root["over"].tryGetI64().has_value());  // overflows int64
  TEST_ASSERT_FALSE(root["exp"].tryGetI64().has_value());   // string, not a number
  TEST_ASSERT_FALSE(root["plus"].tryGetI64().has_value());
  TEST_ASSERT_FALSE(root["hex"].tryGetI64().has_value());
  TEST_ASSERT_FALSE(root["word"].tryGetI64().has_value());
  TEST_ASSERT_FALSE(root["s"].tryGetI64().has_value());
  TEST_ASSERT_FALSE(root["b"].tryGetI64().has_value());
  TEST_ASSERT_FALSE(root["z"].tryGetI64().has_value());
  TEST_ASSERT_FALSE(root["missing"].tryGetI64().has_value());
}

// ---- tryGetDouble ----------------------------------------------------------

static bool nearly(double a, double b)
{
  return std::fabs(a - b) < 1e-9;
}

TEST_CASE("tryGetDouble: accepts ints, decimals and exponents", "[osjson][getters]")
{
  static const char json[] = R"({"i":42,"d":3.14,"neg":-2.5,"e":1e3,"eneg":1.5e-2,"zero":0.0})";
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  JSON::JsonView root = doc.root();

  std::optional<double> d = root["i"].tryGetDouble();
  TEST_ASSERT_TRUE(d.has_value());
  TEST_ASSERT_TRUE(nearly(42.0, *d));
  d = root["d"].tryGetDouble();
  TEST_ASSERT_TRUE(d.has_value());
  TEST_ASSERT_TRUE(nearly(3.14, *d));
  d = root["neg"].tryGetDouble();
  TEST_ASSERT_TRUE(d.has_value());
  TEST_ASSERT_TRUE(nearly(-2.5, *d));
  d = root["e"].tryGetDouble();
  TEST_ASSERT_TRUE(d.has_value());
  TEST_ASSERT_TRUE(nearly(1000.0, *d));
  d = root["eneg"].tryGetDouble();
  TEST_ASSERT_TRUE(d.has_value());
  TEST_ASSERT_TRUE(nearly(0.015, *d));
  d = root["zero"].tryGetDouble();
  TEST_ASSERT_TRUE(d.has_value());
  TEST_ASSERT_TRUE(nearly(0.0, *d));
}

TEST_CASE("tryGetDouble: rejects non-numbers", "[osjson][getters]")
{
  static const char json[] = R"({"s":"3.14","b":true,"z":null,"o":{},"a":[]})";
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  JSON::JsonView root = doc.root();

  TEST_ASSERT_FALSE(root["s"].tryGetDouble().has_value());
  TEST_ASSERT_FALSE(root["b"].tryGetDouble().has_value());
  TEST_ASSERT_FALSE(root["z"].tryGetDouble().has_value());
  TEST_ASSERT_FALSE(root["o"].tryGetDouble().has_value());
  TEST_ASSERT_FALSE(root["a"].tryGetDouble().has_value());
  TEST_ASSERT_FALSE(root["missing"].tryGetDouble().has_value());
}

TEST_CASE("tryGetDouble: number at/over the 64-byte stack-copy limit", "[osjson][getters]")
{
  // 63-char number fits the internal buf[64]; 64-char number is rejected.
  std::string n63(63, '9');
  std::string n64(64, '9');
  std::string json = "{\"a\":" + n63 + ",\"b\":" + n64 + "}";

  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  JSON::JsonView root = doc.root();

  TEST_ASSERT_TRUE(root["a"].tryGetDouble().has_value());   // fits
  TEST_ASSERT_FALSE(root["b"].tryGetDouble().has_value());  // too long -> rejected, not truncated
}

// ---- type predicates -------------------------------------------------------

TEST_CASE("predicates: exactly one container/leaf kind per value", "[osjson][getters]")
{
  static const char json[] = R"({"o":{},"a":[],"s":"x","n":5,"t":true,"z":null})";
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  JSON::JsonView root = doc.root();

  TEST_ASSERT_TRUE(root["o"].isObject());
  TEST_ASSERT_TRUE(root["a"].isArray());
  TEST_ASSERT_TRUE(root["s"].isString());

  // number: primitive + isNumber, but not null/bool-ish
  TEST_ASSERT_TRUE(root["n"].isPrimitive());
  TEST_ASSERT_TRUE(root["n"].isNumber());
  TEST_ASSERT_FALSE(root["n"].isNull());

  // true: primitive but NOT a number
  TEST_ASSERT_TRUE(root["t"].isPrimitive());
  TEST_ASSERT_FALSE(root["t"].isNumber());
  TEST_ASSERT_FALSE(root["t"].isNull());

  // null: primitive, isNull, not a number
  TEST_ASSERT_TRUE(root["z"].isPrimitive());
  TEST_ASSERT_TRUE(root["z"].isNull());
  TEST_ASSERT_FALSE(root["z"].isNumber());
}

TEST_CASE("predicates: an invalid/default view answers false everywhere", "[osjson][getters]")
{
  JSON::JsonView v;  // default-constructed
  TEST_ASSERT_FALSE(v.valid());
  TEST_ASSERT_FALSE(v.isObject());
  TEST_ASSERT_FALSE(v.isArray());
  TEST_ASSERT_FALSE(v.isString());
  TEST_ASSERT_FALSE(v.isPrimitive());
  TEST_ASSERT_FALSE(v.isNumber());
  TEST_ASSERT_FALSE(v.isNull());
  TEST_ASSERT_EQUAL_INT(0, v.count());
  TEST_ASSERT_FALSE(v.at(0).valid());
  TEST_ASSERT_FALSE(v["k"].valid());
  TEST_ASSERT_TRUE(v.raw().empty());

  TEST_ASSERT_FALSE(v.tryGetStr().has_value());
  TEST_ASSERT_FALSE(v.tryGetI64().has_value());
  TEST_ASSERT_FALSE(v.tryGetDouble().has_value());
  TEST_ASSERT_FALSE(v.tryGetBool().has_value());
}
