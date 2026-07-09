// Typed getters (tryGetStr/Bool/I64/Double) and type predicates.
#include "unity.h"

#include "json/Json.h"

#include <cmath>
#include <cstdint>
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

  std::string_view sv;
  TEST_ASSERT_TRUE(root["s"].tryGetStr(sv));
  TEST_ASSERT_TRUE(sv == "hi");

  TEST_ASSERT_FALSE(root["n"].tryGetStr(sv));
  TEST_ASSERT_FALSE(root["b"].tryGetStr(sv));
  TEST_ASSERT_FALSE(root["z"].tryGetStr(sv));
  TEST_ASSERT_FALSE(root["o"].tryGetStr(sv));
  TEST_ASSERT_FALSE(root["a"].tryGetStr(sv));
  TEST_ASSERT_FALSE(root["missing"].tryGetStr(sv));
}

TEST_CASE("tryGetStr: empty string value", "[osjson][getters]")
{
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(R"({"s":""})"));
  std::string_view sv;
  TEST_ASSERT_TRUE(doc.root()["s"].tryGetStr(sv));
  TEST_ASSERT_EQUAL_size_t(0, sv.size());
  TEST_ASSERT_TRUE(sv.empty());
}

TEST_CASE("tryGetStr: escape sequences are returned RAW (jsmn does not unescape)", "[osjson][getters]")
{
  // Documents current behaviour: the view is the raw bytes between the quotes,
  // escapes are NOT processed. Consumers that need real bytes must unescape.
  static const char json[] = R"({"s":"a\nb\"c"})";
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  std::string_view sv;
  TEST_ASSERT_TRUE(doc.root()["s"].tryGetStr(sv));
  TEST_ASSERT_TRUE(sv == R"(a\nb\"c)");  // backslash-n and backslash-quote, literally
  TEST_ASSERT_EQUAL_size_t(7, sv.size());
}

// ---- tryGetBool ------------------------------------------------------------

TEST_CASE("tryGetBool: true/false succeed, everything else fails", "[osjson][getters]")
{
  static const char json[] = R"({"t":true,"f":false,"n":1,"s":"true","z":null})";
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  JSON::JsonView root = doc.root();

  bool b = false;
  TEST_ASSERT_TRUE(root["t"].tryGetBool(b));
  TEST_ASSERT_TRUE(b);
  TEST_ASSERT_TRUE(root["f"].tryGetBool(b));
  TEST_ASSERT_FALSE(b);

  TEST_ASSERT_FALSE(root["n"].tryGetBool(b));  // number
  TEST_ASSERT_FALSE(root["s"].tryGetBool(b));  // the STRING "true"
  TEST_ASSERT_FALSE(root["z"].tryGetBool(b));  // null
  TEST_ASSERT_FALSE(root["missing"].tryGetBool(b));
}

// ---- tryGetI64 -------------------------------------------------------------

TEST_CASE("tryGetI64: valid integers including boundaries", "[osjson][getters]")
{
  static const char json[] = R"({"zero":0,"pos":123,"neg":-123,"i32over":2147483648,)"
                             R"("max":9223372036854775807,"min":-9223372036854775808,"leadzero":007})";
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  JSON::JsonView root = doc.root();

  int64_t v = 0;
  TEST_ASSERT_TRUE(root["zero"].tryGetI64(v));
  TEST_ASSERT_EQUAL_INT64(0, v);
  TEST_ASSERT_TRUE(root["pos"].tryGetI64(v));
  TEST_ASSERT_EQUAL_INT64(123, v);
  TEST_ASSERT_TRUE(root["neg"].tryGetI64(v));
  TEST_ASSERT_EQUAL_INT64(-123, v);
  TEST_ASSERT_TRUE(root["i32over"].tryGetI64(v));
  TEST_ASSERT_EQUAL_INT64(2147483648LL, v);
  TEST_ASSERT_TRUE(root["max"].tryGetI64(v));
  TEST_ASSERT_EQUAL_INT64(INT64_MAX, v);
  TEST_ASSERT_TRUE(root["min"].tryGetI64(v));
  TEST_ASSERT_EQUAL_INT64(INT64_MIN, v);
  TEST_ASSERT_TRUE(root["leadzero"].tryGetI64(v));  // "007" -> 7 (from_chars consumes all digits)
  TEST_ASSERT_EQUAL_INT64(7, v);
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

  int64_t v = 0;
  TEST_ASSERT_FALSE(root["flt"].tryGetI64(v));   // 1.5 -> from_chars stops at '.'
  TEST_ASSERT_FALSE(root["over"].tryGetI64(v));  // overflows int64
  TEST_ASSERT_FALSE(root["exp"].tryGetI64(v));   // string, not a number
  TEST_ASSERT_FALSE(root["plus"].tryGetI64(v));
  TEST_ASSERT_FALSE(root["hex"].tryGetI64(v));
  TEST_ASSERT_FALSE(root["word"].tryGetI64(v));
  TEST_ASSERT_FALSE(root["s"].tryGetI64(v));
  TEST_ASSERT_FALSE(root["b"].tryGetI64(v));
  TEST_ASSERT_FALSE(root["z"].tryGetI64(v));
  TEST_ASSERT_FALSE(root["missing"].tryGetI64(v));
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

  double d = 0;
  TEST_ASSERT_TRUE(root["i"].tryGetDouble(d));
  TEST_ASSERT_TRUE(nearly(42.0, d));
  TEST_ASSERT_TRUE(root["d"].tryGetDouble(d));
  TEST_ASSERT_TRUE(nearly(3.14, d));
  TEST_ASSERT_TRUE(root["neg"].tryGetDouble(d));
  TEST_ASSERT_TRUE(nearly(-2.5, d));
  TEST_ASSERT_TRUE(root["e"].tryGetDouble(d));
  TEST_ASSERT_TRUE(nearly(1000.0, d));
  TEST_ASSERT_TRUE(root["eneg"].tryGetDouble(d));
  TEST_ASSERT_TRUE(nearly(0.015, d));
  TEST_ASSERT_TRUE(root["zero"].tryGetDouble(d));
  TEST_ASSERT_TRUE(nearly(0.0, d));
}

TEST_CASE("tryGetDouble: rejects non-numbers", "[osjson][getters]")
{
  static const char json[] = R"({"s":"3.14","b":true,"z":null,"o":{},"a":[]})";
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  JSON::JsonView root = doc.root();

  double d = 0;
  TEST_ASSERT_FALSE(root["s"].tryGetDouble(d));
  TEST_ASSERT_FALSE(root["b"].tryGetDouble(d));
  TEST_ASSERT_FALSE(root["z"].tryGetDouble(d));
  TEST_ASSERT_FALSE(root["o"].tryGetDouble(d));
  TEST_ASSERT_FALSE(root["a"].tryGetDouble(d));
  TEST_ASSERT_FALSE(root["missing"].tryGetDouble(d));
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

  double d = 0;
  TEST_ASSERT_TRUE(root["a"].tryGetDouble(d));   // fits
  TEST_ASSERT_FALSE(root["b"].tryGetDouble(d));  // too long -> rejected, not truncated
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

  std::string_view sv;
  int64_t i;
  double d;
  bool b;
  TEST_ASSERT_FALSE(v.tryGetStr(sv));
  TEST_ASSERT_FALSE(v.tryGetI64(i));
  TEST_ASSERT_FALSE(v.tryGetDouble(d));
  TEST_ASSERT_FALSE(v.tryGetBool(b));
}
