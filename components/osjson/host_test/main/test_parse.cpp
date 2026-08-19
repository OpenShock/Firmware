// JsonDocument::parse + root() edge cases.
#include "unity.h"

#include "json/Json.h"

#include <string>
#include <string_view>

using namespace OpenShock;

TEST_CASE("parse: empty and whitespace-only inputs are rejected", "[osjson][parse]")
{
  JSON::JsonDocument doc;
  TEST_ASSERT_FALSE(doc.parse(""));
  TEST_ASSERT_FALSE(doc.ok());
  TEST_ASSERT_FALSE(doc.root().valid());

  TEST_ASSERT_FALSE(doc.parse("   "));
  TEST_ASSERT_FALSE(doc.parse("\t\n\r "));
}

TEST_CASE("parse: root() is invalid before any parse", "[osjson][parse]")
{
  JSON::JsonDocument doc;
  TEST_ASSERT_FALSE(doc.ok());
  TEST_ASSERT_FALSE(doc.root().valid());
}

TEST_CASE("parse: malformed documents are rejected", "[osjson][parse]")
{
  JSON::JsonDocument doc;
  TEST_ASSERT_FALSE(doc.parse("{"));
  TEST_ASSERT_FALSE(doc.parse("["));
  TEST_ASSERT_FALSE(doc.parse("[1,2"));
  TEST_ASSERT_FALSE(doc.parse("{\"a\":"));
  TEST_ASSERT_FALSE(doc.parse("{\"a\""));          // key with no value
  TEST_ASSERT_FALSE(doc.parse("\"unterminated"));  // unterminated string
  TEST_ASSERT_FALSE(doc.parse("{ not valid json"));
}

TEST_CASE("parse: a failed parse leaves root() invalid", "[osjson][parse]")
{
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(R"({"a":1})"));
  TEST_ASSERT_TRUE(doc.root().valid());

  TEST_ASSERT_FALSE(doc.parse("{bad"));
  TEST_ASSERT_FALSE(doc.ok());
  TEST_ASSERT_FALSE(doc.root().valid());
}

TEST_CASE("parse: object / array roots report the right container type", "[osjson][parse]")
{
  JSON::JsonDocument obj;
  TEST_ASSERT_TRUE(obj.parse(R"({"a":1})"));
  TEST_ASSERT_TRUE(obj.root().isObject());
  TEST_ASSERT_FALSE(obj.root().isArray());

  JSON::JsonDocument arr;
  TEST_ASSERT_TRUE(arr.parse(R"([1,2,3])"));
  TEST_ASSERT_TRUE(arr.root().isArray());
  TEST_ASSERT_FALSE(arr.root().isObject());
}

TEST_CASE("parse: empty object and empty array", "[osjson][parse]")
{
  JSON::JsonDocument obj;
  TEST_ASSERT_TRUE(obj.parse("{}"));
  TEST_ASSERT_TRUE(obj.root().isObject());
  TEST_ASSERT_EQUAL_INT(0, obj.root().count());
  TEST_ASSERT_FALSE(obj.root()["anything"].valid());

  JSON::JsonDocument arr;
  TEST_ASSERT_TRUE(arr.parse("[]"));
  TEST_ASSERT_TRUE(arr.root().isArray());
  TEST_ASSERT_EQUAL_INT(0, arr.root().count());
  TEST_ASSERT_FALSE(arr.root().at(0).valid());
}

TEST_CASE("parse: scalar document at the root", "[osjson][parse]")
{
  JSON::JsonDocument num;
  TEST_ASSERT_TRUE(num.parse("42"));
  TEST_ASSERT_TRUE(num.root().isNumber());
  int64_t n = 0;
  TEST_ASSERT_TRUE(num.root().tryGetI64(n));
  TEST_ASSERT_EQUAL_INT64(42, n);

  JSON::JsonDocument str;
  TEST_ASSERT_TRUE(str.parse("\"hello\""));
  TEST_ASSERT_TRUE(str.root().isString());
  std::string_view sv;
  TEST_ASSERT_TRUE(str.root().tryGetStr(sv));
  TEST_ASSERT_TRUE(sv == "hello");

  JSON::JsonDocument boolean;
  TEST_ASSERT_TRUE(boolean.parse("true"));
  bool b = false;
  TEST_ASSERT_TRUE(boolean.root().tryGetBool(b));
  TEST_ASSERT_TRUE(b);

  JSON::JsonDocument null;
  TEST_ASSERT_TRUE(null.parse("null"));
  TEST_ASSERT_TRUE(null.root().isNull());
}

TEST_CASE("parse: re-parsing the same document reuses state cleanly", "[osjson][parse]")
{
  JSON::JsonDocument doc;

  TEST_ASSERT_TRUE(doc.parse(R"({"a":1,"b":2,"c":3})"));
  TEST_ASSERT_EQUAL_INT(3, doc.root().count());

  TEST_ASSERT_TRUE(doc.parse(R"({"x":9})"));
  TEST_ASSERT_EQUAL_INT(1, doc.root().count());
  int64_t x = 0;
  TEST_ASSERT_TRUE(doc.root()["x"].tryGetI64(x));
  TEST_ASSERT_EQUAL_INT64(9, x);
  TEST_ASSERT_FALSE(doc.root()["a"].valid());  // key from the previous parse is gone
}

TEST_CASE("parse: source buffer must outlive the document (zero-copy contract)", "[osjson][parse]")
{
  std::string src = R"({"name":"value"})";

  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(src));

  std::string_view sv;
  TEST_ASSERT_TRUE(doc.root()["name"].tryGetStr(sv));
  // The view must point straight into src, not into a copy.
  TEST_ASSERT_TRUE(sv.data() >= src.data() && sv.data() < src.data() + src.size());
  TEST_ASSERT_TRUE(sv == "value");
}
