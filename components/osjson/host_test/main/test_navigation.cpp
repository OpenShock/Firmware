// Object member lookup, array indexing, count(), raw(), and skip() correctness.
#include "unity.h"

#include "json/Json.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

using namespace OpenShock;

TEST_CASE("operator[]: present/missing keys and non-objects", "[osjson][nav]")
{
  static const char json[] = R"({"a":1,"b":2})";
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  JSON::JsonView root = doc.root();

  TEST_ASSERT_TRUE(root["a"].valid());
  TEST_ASSERT_TRUE(root["b"].valid());
  TEST_ASSERT_FALSE(root["c"].valid());
  TEST_ASSERT_FALSE(root[""].valid());

  // indexing into a non-object yields an invalid view (no crash)
  TEST_ASSERT_FALSE(root["a"]["nested"].valid());
}

TEST_CASE("operator[]: case-sensitive and prefix-distinct keys", "[osjson][nav]")
{
  static const char json[] = R"({"name":1,"Name":2,"n":3,"nam":4})";
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  JSON::JsonView root = doc.root();

  std::optional<int64_t> v;
  v = root["name"].tryGetI64();
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_INT64(1, *v);
  v = root["Name"].tryGetI64();  // different case -> different key
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_INT64(2, *v);
  v = root["n"].tryGetI64();  // must not match "name"/"nam"
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_INT64(3, *v);
  v = root["nam"].tryGetI64();
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_INT64(4, *v);
}

TEST_CASE("operator[]: empty-string key can be looked up", "[osjson][nav]")
{
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(R"({"":42})"));
  std::optional<int64_t> v;
  v = doc.root()[""].tryGetI64();
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_INT64(42, *v);
}

TEST_CASE("operator[]: duplicate keys resolve to the first occurrence", "[osjson][nav]")
{
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(R"({"k":1,"k":2})"));
  std::optional<int64_t> v;
  v = doc.root()["k"].tryGetI64();
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_INT64(1, *v);
}

TEST_CASE("operator[]: lookup skips over object- and array-valued members", "[osjson][nav]")
{
  // The value before "target" is a nested structure; skip() must jump the whole
  // subtree so the following key is found correctly.
  static const char json[] = R"({"big":{"x":[1,2,{"y":3}],"z":{"w":4}},"arr":[9,8,7],"target":123})";
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  JSON::JsonView root = doc.root();

  std::optional<int64_t> v;
  v = root["target"].tryGetI64();
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_INT64(123, *v);

  // and the nested paths themselves resolve
  v = root["big"]["z"]["w"].tryGetI64();
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_INT64(4, *v);
  v = root["big"]["x"].at(2)["y"].tryGetI64();
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_INT64(3, *v);
}

TEST_CASE("count(): arrays, objects, primitives, invalid", "[osjson][nav]")
{
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(R"({"a":[1,2,3,4],"o":{"x":1,"y":2},"n":5})"));
  JSON::JsonView root = doc.root();

  TEST_ASSERT_EQUAL_INT(3, root.count());       // object: member count
  TEST_ASSERT_EQUAL_INT(4, root["a"].count());  // array length
  TEST_ASSERT_EQUAL_INT(2, root["o"].count());  // nested object members
  TEST_ASSERT_EQUAL_INT(0, root["n"].count());  // primitive
  TEST_ASSERT_EQUAL_INT(0, root["missing"].count());
}

TEST_CASE("at(): valid indices, out of range, and non-arrays", "[osjson][nav]")
{
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(R"([10,20,30])"));
  JSON::JsonView arr = doc.root();

  std::optional<int64_t> v;
  v = arr.at(0).tryGetI64();
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_INT64(10, *v);
  v = arr.at(2).tryGetI64();
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_INT64(30, *v);

  TEST_ASSERT_FALSE(arr.at(3).valid());   // past the end
  TEST_ASSERT_FALSE(arr.at(-1).valid());  // negative
  TEST_ASSERT_FALSE(arr.at(1000).valid());

  // at() on an object is invalid (only arrays are indexable)
  JSON::JsonDocument obj;
  TEST_ASSERT_TRUE(obj.parse(R"({"a":1})"));
  TEST_ASSERT_FALSE(obj.root().at(0).valid());
}

TEST_CASE("arrays: nested and mixed-type elements", "[osjson][nav]")
{
  static const char json[] = R"([[1,2],[3,4],"five",true,null,{"k":6},[]])";
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  JSON::JsonView root = doc.root();

  TEST_ASSERT_EQUAL_INT(7, root.count());

  std::optional<int64_t> v;
  TEST_ASSERT_TRUE(root.at(0).isArray());
  v = root.at(0).at(1).tryGetI64();
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_INT64(2, *v);
  v = root.at(1).at(0).tryGetI64();
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_INT64(3, *v);

  std::optional<std::string> sv;
  sv = root.at(2).tryGetStr();
  TEST_ASSERT_TRUE(sv.has_value());
  TEST_ASSERT_TRUE(*sv == "five");

  std::optional<bool> b;
  b = root.at(3).tryGetBool();
  TEST_ASSERT_TRUE(b.has_value());
  TEST_ASSERT_TRUE(*b);

  TEST_ASSERT_TRUE(root.at(4).isNull());

  TEST_ASSERT_TRUE(root.at(5).isObject());
  v = root.at(5)["k"].tryGetI64();
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_INT64(6, *v);

  TEST_ASSERT_TRUE(root.at(6).isArray());
  TEST_ASSERT_EQUAL_INT(0, root.at(6).count());
}

TEST_CASE("raw(): zero-copy token text for strings, numbers, containers", "[osjson][nav]")
{
  static const char json[] = R"({"s":"hi","n":-12.5,"a":[1,2]})";
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(json));
  JSON::JsonView root = doc.root();

  // string raw() is the content WITHOUT the surrounding quotes
  std::string_view s = root["s"].raw();
  TEST_ASSERT_TRUE(s == "hi");
  TEST_ASSERT_TRUE(s.data() >= json && s.data() < json + sizeof(json));

  // primitive raw() is the literal text
  TEST_ASSERT_TRUE(root["n"].raw() == "-12.5");

  // container raw() spans the whole array text
  std::string_view a = root["a"].raw();
  TEST_ASSERT_TRUE(a.size() >= 2);
  TEST_ASSERT_EQUAL_INT('[', a.front());
  TEST_ASSERT_EQUAL_INT(']', a.back());
}

TEST_CASE("chaining on invalid views never dereferences", "[osjson][nav]")
{
  JSON::JsonDocument doc;
  TEST_ASSERT_TRUE(doc.parse(R"({"a":1})"));
  JSON::JsonView root = doc.root();

  // deep chain through missing keys and bad indices stays invalid, no crash
  TEST_ASSERT_FALSE(root["nope"]["deeper"].at(3)["evenmore"].valid());
  TEST_ASSERT_FALSE(root["nope"].at(0)["x"].tryGetI64().has_value());
}
