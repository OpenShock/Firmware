// SemVer parsing + ordering - used for OTA/firmware version comparisons.
#include "unity.h"

#include "SemVer.h"

#include <string_view>

using namespace OpenShock;

TEST_CASE("TryParseSemVer basic", "[util][semver]")
{
  SemVer v;
  TEST_ASSERT_TRUE(TryParseSemVer("1.2.3", v));
  TEST_ASSERT_EQUAL_UINT16(1, v.major);
  TEST_ASSERT_EQUAL_UINT16(2, v.minor);
  TEST_ASSERT_EQUAL_UINT16(3, v.patch);
  TEST_ASSERT_TRUE(v.prerelease.empty());
  TEST_ASSERT_TRUE(v.build.empty());
  TEST_ASSERT_TRUE(v.isValid());
}

TEST_CASE("TryParseSemVer with dotted prerelease and build", "[util][semver]")
{
  // OpenShock's own RC tag format, e.g. 1.6.0-rc.1 / 1.5.0-rc.7.
  SemVer v;
  TEST_ASSERT_TRUE(TryParseSemVer("1.5.0-rc.7+build.9", v));
  TEST_ASSERT_EQUAL_UINT16(1, v.major);
  TEST_ASSERT_EQUAL_UINT16(5, v.minor);
  TEST_ASSERT_EQUAL_UINT16(0, v.patch);
  TEST_ASSERT_TRUE(v.prerelease == "rc.7");
  TEST_ASSERT_TRUE(v.build == "build.9");
}

TEST_CASE("TryParseSemVer rejects malformed", "[util][semver]")
{
  SemVer v;
  TEST_ASSERT_FALSE(TryParseSemVer("1.2", v));
  TEST_ASSERT_FALSE(TryParseSemVer("", v));
  TEST_ASSERT_FALSE(TryParseSemVer("abc", v));
  TEST_ASSERT_FALSE(TryParseSemVer("1.2.x", v));
  TEST_ASSERT_FALSE(TryParseSemVer("1.2.3.4", v));  // core has too many parts
}

TEST_CASE("SemVer ordering: core precedence", "[util][semver]")
{
  SemVer a, b;
  TEST_ASSERT_TRUE(TryParseSemVer("1.2.3", a));
  TEST_ASSERT_TRUE(TryParseSemVer("1.2.4", b));
  TEST_ASSERT_TRUE(a < b);
  TEST_ASSERT_TRUE(b > a);
  TEST_ASSERT_TRUE(a != b);

  SemVer major1, major2;
  TEST_ASSERT_TRUE(TryParseSemVer("2.0.0", major2));
  TEST_ASSERT_TRUE(TryParseSemVer("1.99.99", major1));
  TEST_ASSERT_TRUE(major1 < major2);
}

TEST_CASE("SemVer ordering: a prerelease is below its release", "[util][semver]")
{
  SemVer rel, pre;
  TEST_ASSERT_TRUE(TryParseSemVer("1.0.0", rel));
  TEST_ASSERT_TRUE(TryParseSemVer("1.0.0-rc1", pre));
  TEST_ASSERT_TRUE(pre < rel);
  TEST_ASSERT_TRUE(rel > pre);
}

TEST_CASE("SemVer ordering: dotted RCs sort numerically", "[util][semver]")
{
  // The bug this replaces: 1.5.0-rc.6 and 1.5.0-rc.7 used to parse equal.
  SemVer rc6, rc7;
  TEST_ASSERT_TRUE(TryParseSemVer("1.5.0-rc.6", rc6));
  TEST_ASSERT_TRUE(TryParseSemVer("1.5.0-rc.7", rc7));
  TEST_ASSERT_TRUE(rc6 < rc7);
  TEST_ASSERT_TRUE(rc6 != rc7);

  // numeric identifiers rank below alphanumeric (rc.9 < rc.beta)
  SemVer rc9, rcbeta;
  TEST_ASSERT_TRUE(TryParseSemVer("1.0.0-rc.9", rc9));
  TEST_ASSERT_TRUE(TryParseSemVer("1.0.0-rc.beta", rcbeta));
  TEST_ASSERT_TRUE(rc9 < rcbeta);

  // more identifiers outrank fewer when the prefix is equal (rc < rc.1)
  SemVer rc, rc1;
  TEST_ASSERT_TRUE(TryParseSemVer("1.0.0-rc", rc));
  TEST_ASSERT_TRUE(TryParseSemVer("1.0.0-rc.1", rc1));
  TEST_ASSERT_TRUE(rc < rc1);
}

TEST_CASE("SemVer: build metadata is ignored for precedence", "[util][semver]")
{
  SemVer a, b;
  TEST_ASSERT_TRUE(TryParseSemVer("1.0.0+build1", a));
  TEST_ASSERT_TRUE(TryParseSemVer("1.0.0+build2", b));
  TEST_ASSERT_FALSE(a < b);
  TEST_ASSERT_FALSE(b < a);
}

TEST_CASE("SemVer toString round-trips with dots", "[util][semver]")
{
  SemVer v;
  TEST_ASSERT_TRUE(TryParseSemVer("3.1.4-beta.2+build.9", v));
  TEST_ASSERT_TRUE(v.toString() == "3.1.4-beta.2+build.9");
}
