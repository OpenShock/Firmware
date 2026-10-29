// SemVer parsing + ordering - used for OTA/firmware version comparisons.
#include "unity.h"

#include "SemVer.h"

#include <string_view>

using namespace OpenShock;

TEST_CASE("TryParseSemVer basic", "[util][semver]")
{
  auto v = TryParseSemVer("1.2.3");
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_UINT16(1, v->major);
  TEST_ASSERT_EQUAL_UINT16(2, v->minor);
  TEST_ASSERT_EQUAL_UINT16(3, v->patch);
  TEST_ASSERT_TRUE(v->prerelease.empty());
  TEST_ASSERT_TRUE(v->build.empty());
  TEST_ASSERT_TRUE(v->isValid());
}

TEST_CASE("TryParseSemVer with dotted prerelease and build", "[util][semver]")
{
  // OpenShock's own RC tag format, e.g. 1.6.0-rc.1 / 1.5.0-rc.7.
  auto v = TryParseSemVer("1.5.0-rc.7+build.9");
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_EQUAL_UINT16(1, v->major);
  TEST_ASSERT_EQUAL_UINT16(5, v->minor);
  TEST_ASSERT_EQUAL_UINT16(0, v->patch);
  TEST_ASSERT_TRUE(v->prerelease == "rc.7");
  TEST_ASSERT_TRUE(v->build == "build.9");
}

TEST_CASE("TryParseSemVer rejects malformed", "[util][semver]")
{
  TEST_ASSERT_FALSE(TryParseSemVer("1.2").has_value());
  TEST_ASSERT_FALSE(TryParseSemVer("").has_value());
  TEST_ASSERT_FALSE(TryParseSemVer("abc").has_value());
  TEST_ASSERT_FALSE(TryParseSemVer("1.2.x").has_value());
  TEST_ASSERT_FALSE(TryParseSemVer("1.2.3.4").has_value());    // core has too many parts
  TEST_ASSERT_FALSE(TryParseSemVer("01.2.3").has_value());     // leading zero in the core
  TEST_ASSERT_FALSE(TryParseSemVer("1. 2.3").has_value());     // whitespace in the core
  TEST_ASSERT_FALSE(TryParseSemVer("1.2.65536").has_value());  // component out of uint16_t range
}

TEST_CASE("SemVer ordering: core precedence", "[util][semver]")
{
  auto a = TryParseSemVer("1.2.3");
  auto b = TryParseSemVer("1.2.4");
  TEST_ASSERT_TRUE(a.has_value() && b.has_value());
  TEST_ASSERT_TRUE(*a < *b);
  TEST_ASSERT_TRUE(*b > *a);
  TEST_ASSERT_TRUE(*a != *b);

  auto major2 = TryParseSemVer("2.0.0");
  auto major1 = TryParseSemVer("1.99.99");
  TEST_ASSERT_TRUE(major1.has_value() && major2.has_value());
  TEST_ASSERT_TRUE(*major1 < *major2);
}

TEST_CASE("SemVer ordering: a prerelease is below its release", "[util][semver]")
{
  auto rel = TryParseSemVer("1.0.0");
  auto pre = TryParseSemVer("1.0.0-rc1");
  TEST_ASSERT_TRUE(rel.has_value() && pre.has_value());
  TEST_ASSERT_TRUE(*pre < *rel);
  TEST_ASSERT_TRUE(*rel > *pre);
}

TEST_CASE("SemVer ordering: dotted RCs sort numerically", "[util][semver]")
{
  // The bug this replaces: 1.5.0-rc.6 and 1.5.0-rc.7 used to parse equal.
  auto rc6 = TryParseSemVer("1.5.0-rc.6");
  auto rc7 = TryParseSemVer("1.5.0-rc.7");
  TEST_ASSERT_TRUE(rc6.has_value() && rc7.has_value());
  TEST_ASSERT_TRUE(*rc6 < *rc7);
  TEST_ASSERT_TRUE(*rc6 != *rc7);

  // numeric identifiers rank below alphanumeric (rc.9 < rc.beta)
  auto rc9    = TryParseSemVer("1.0.0-rc.9");
  auto rcbeta = TryParseSemVer("1.0.0-rc.beta");
  TEST_ASSERT_TRUE(rc9.has_value() && rcbeta.has_value());
  TEST_ASSERT_TRUE(*rc9 < *rcbeta);

  // more identifiers outrank fewer when the prefix is equal (rc < rc.1)
  auto rc  = TryParseSemVer("1.0.0-rc");
  auto rc1 = TryParseSemVer("1.0.0-rc.1");
  TEST_ASSERT_TRUE(rc.has_value() && rc1.has_value());
  TEST_ASSERT_TRUE(*rc < *rc1);
}

TEST_CASE("SemVer: build metadata is ignored for precedence", "[util][semver]")
{
  auto a = TryParseSemVer("1.0.0+build1");
  auto b = TryParseSemVer("1.0.0+build2");
  TEST_ASSERT_TRUE(a.has_value() && b.has_value());
  TEST_ASSERT_FALSE(*a < *b);
  TEST_ASSERT_FALSE(*b < *a);
}

TEST_CASE("SemVer toString round-trips with dots", "[util][semver]")
{
  auto v = TryParseSemVer("3.1.4-beta.2+build.9");
  TEST_ASSERT_TRUE(v.has_value());
  TEST_ASSERT_TRUE(v->toString() == "3.1.4-beta.2+build.9");
}

TEST_CASE("SemVer toString: zero and maximal components, optional parts", "[util][semver]")
{
  TEST_ASSERT_TRUE(SemVer(0, 0, 0).toString() == "0.0.0");
  TEST_ASSERT_TRUE(SemVer(65535, 65535, 65535).toString() == "65535.65535.65535");  // the longest core
  TEST_ASSERT_TRUE(SemVer(10, 0, 7, "rc.1", "").toString() == "10.0.7-rc.1");
  TEST_ASSERT_TRUE(SemVer(1, 20, 300, "", "sha.abc").toString() == "1.20.300+sha.abc");
  TEST_ASSERT_TRUE(SemVer(65535, 65535, 65535, "rc.1", "b.2").toString() == "65535.65535.65535-rc.1+b.2");
}

TEST_CASE("SemVer isValid accepts dotted prerelease and build", "[util][semver]")
{
  TEST_ASSERT_TRUE(SemVer(1, 5, 0, "rc.7", "build.9").isValid());
  TEST_ASSERT_FALSE(SemVer(1, 5, 0, "rc..7", "").isValid());
}

TEST_CASE("TryParseSemVer rejects an empty prerelease or build", "[util][semver]")
{
  TEST_ASSERT_FALSE(TryParseSemVer("1.0.0-").has_value());
  TEST_ASSERT_FALSE(TryParseSemVer("1.0.0+").has_value());
  TEST_ASSERT_FALSE(TryParseSemVer("1.0.0-+").has_value());
  TEST_ASSERT_FALSE(TryParseSemVer("1.2.x").has_value());
}

TEST_CASE("SemVer ordering operators agree when only build metadata differs", "[util][semver]")
{
  auto a = TryParseSemVer("1.0.0+x");
  auto b = TryParseSemVer("1.0.0+y");
  TEST_ASSERT_TRUE(a.has_value() && b.has_value());
  TEST_ASSERT_FALSE(*a > *b);
  TEST_ASSERT_FALSE(*b > *a);
  TEST_ASSERT_TRUE(*a <= *b);
  TEST_ASSERT_TRUE(*a >= *b);
  TEST_ASSERT_TRUE(*a != *b);  // equality still includes build metadata
}

TEST_CASE("SemVer string comparisons fail closed on garbage", "[util][semver]")
{
  SemVer v(1, 0, 0);
  TEST_ASSERT_FALSE(v < "garbage");
  TEST_ASSERT_FALSE(v <= "garbage");
  TEST_ASSERT_FALSE(v > "garbage");
  TEST_ASSERT_FALSE(v >= "garbage");
  TEST_ASSERT_FALSE(v == "garbage");
  TEST_ASSERT_TRUE(v > "0.9.0");
}
