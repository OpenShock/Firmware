#include "SemVer.h"

const char* const TAG = "SemVer";

#include "Convert.h"
#include "Logging.h"
#include "StringHelpers.h"

#include <charconv>

using namespace OpenShock;

// https://semver.org/#backusnaur-form-grammar-for-valid-semver-versions
#pragma region Validation Functions

static constexpr bool semverIsLetter(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static constexpr bool semverIsPositiveDigit(char c)
{
  return c >= '1' && c <= '9';
}
static constexpr bool semverIsDigit(char c)
{
  return c == '0' || semverIsPositiveDigit(c);
}
static constexpr bool semverIsDigits(std::string_view str)
{
  if (str.empty()) {
    return false;
  }

  for (auto c : str) {
    if (!semverIsDigit(c)) {
      return false;
    }
  }

  return true;
}
static constexpr bool semverIsNonDigit(char c)
{
  return semverIsLetter(c) || c == '-';
}
static constexpr bool semverIsIdentifierChararacter(char c)
{
  return semverIsDigit(c) || semverIsNonDigit(c);
}
static constexpr bool semverIsIdentifierChararacters(std::string_view str)
{
  if (str.empty()) {
    return false;
  }

  for (auto c : str) {
    if (!semverIsIdentifierChararacter(c)) {
      return false;
    }
  }

  return true;
}
static constexpr bool semverIsNumericIdentifier(std::string_view str)
{
  if (str.empty()) {
    return false;
  }

  if (str.length() == 1) {
    return semverIsDigit(str[0]);
  }

  return semverIsPositiveDigit(str[0]) && semverIsDigits(str.substr(1));
}
static constexpr bool semverIsAlphanumericIdentifier(std::string_view str)
{
  if (str.empty()) {
    return false;
  }

  if (str.length() == 1) {
    return semverIsNonDigit(str[0]);
  }

  std::size_t nonDigitPos = std::string_view::npos;
  for (std::size_t i = 0; i < str.length(); ++i) {
    if (semverIsNonDigit(str[i])) {
      nonDigitPos = i;
      break;
    }
  }

  if (nonDigitPos == std::string_view::npos) {
    return false;
  }

  auto after = str.substr(nonDigitPos + 1);

  if (nonDigitPos == 0) {
    return semverIsIdentifierChararacters(after);
  }

  auto before = str.substr(0, nonDigitPos);

  if (nonDigitPos == str.length() - 1) {
    return semverIsIdentifierChararacters(before);
  }

  return semverIsIdentifierChararacters(before) && semverIsIdentifierChararacters(after);
}
static constexpr bool semverIsBuildIdentifier(std::string_view str)
{
  return semverIsAlphanumericIdentifier(str) || semverIsDigits(str);
}
static constexpr bool semverIsPrereleaseIdentifier(std::string_view str)
{
  return semverIsAlphanumericIdentifier(str) || semverIsNumericIdentifier(str);
}
static constexpr bool semverIsDotSeperatedBuildIdentifiers(std::string_view str)
{
  if (str.empty()) {
    return false;
  }

  auto dotIdx = str.find('.');
  while (dotIdx != std::string_view::npos) {
    auto part = str.substr(0, dotIdx);
    if (!semverIsBuildIdentifier(part)) {
      return false;
    }

    str    = str.substr(dotIdx + 1);
    dotIdx = str.find('.');
  }

  return semverIsBuildIdentifier(str);
}
static constexpr bool semverIsDotSeperatedPreleaseIdentifiers(std::string_view str)
{
  if (str.empty()) {
    return false;
  }

  auto dotIdx = str.find('.');
  while (dotIdx != std::string_view::npos) {
    auto part = str.substr(0, dotIdx);
    if (!semverIsPrereleaseIdentifier(part)) {
      return false;
    }

    str    = str.substr(dotIdx + 1);
    dotIdx = str.find('.');
  }

  return semverIsPrereleaseIdentifier(str);
}

// For readability
#define semverIsPrerelease semverIsDotSeperatedPreleaseIdentifiers
#define semverIsBuild      semverIsDotSeperatedBuildIdentifiers

#pragma endregion

bool SemVer::isValid() const
{
  // Same dot-separated grammar TryParseSemVer accepts (e.g. "rc.7")
  if (!this->prerelease.empty() && !semverIsPrerelease(this->prerelease)) {
    return false;
  }

  if (!this->build.empty() && !semverIsBuild(this->build)) {
    return false;
  }

  return true;
}

std::string SemVer::toString() const
{
  // "65535.65535.65535" is the longest version core
  std::size_t length = 17;
  if (!prerelease.empty()) {
    length += 1 + prerelease.length();
  }
  if (!build.empty()) {
    length += 1 + build.length();
  }

  std::string str;
  str.reserve(length);

  // Each part through its own buffer, appending only what to_chars wrote: never a write past what it returned.
  auto appendNumber = [&str](uint16_t value) {
    char digits[5];  // "65535"
    auto [end, ec] = std::to_chars(digits, digits + sizeof(digits), value);
    if (ec == std::errc {}) {
      str.append(digits, end);
    }
  };

  appendNumber(major);
  str.push_back('.');
  appendNumber(minor);
  str.push_back('.');
  appendNumber(patch);

  if (!prerelease.empty()) {
    str.push_back('-');
    str.append(prerelease);
  }

  if (!build.empty()) {
    str.push_back('+');
    str.append(build);
  }

  return str;
}

bool SemVer::operator==(const SemVer& other) const
{
  return major == other.major && minor == other.minor && patch == other.patch && prerelease == other.prerelease && build == other.build;
}

static bool semverIdentifierIsNumeric(std::string_view id)
{
  if (id.empty()) {
    return false;
  }
  for (char c : id) {
    if (c < '0' || c > '9') {
      return false;
    }
  }
  return true;
}

// Compare two non-empty dot-separated prerelease strings per semver 11.4:
// numeric identifiers compare numerically and rank below alphanumeric ones;
// alphanumeric compare in ASCII order; a larger set of identifiers outranks a
// smaller one when all preceding identifiers are equal. Returns <0 / 0 / >0.
static int semverComparePrerelease(std::string_view a, std::string_view b)
{
  while (!a.empty() || !b.empty()) {
    if (a.empty()) return -1;  // a ran out first -> fewer identifiers -> lower
    if (b.empty()) return 1;

    size_t ad            = a.find('.');
    size_t bd            = b.find('.');
    std::string_view aid = a.substr(0, ad);
    std::string_view bid = b.substr(0, bd);

    bool an = semverIdentifierIsNumeric(aid);
    bool bn = semverIdentifierIsNumeric(bid);
    if (an && bn) {
      if (aid.length() != bid.length()) return aid.length() < bid.length() ? -1 : 1;  // no leading zeros
      int c = aid.compare(bid);
      if (c != 0) return c < 0 ? -1 : 1;
    } else if (an != bn) {
      return an ? -1 : 1;  // numeric ranks below alphanumeric
    } else {
      int c = aid.compare(bid);
      if (c != 0) return c < 0 ? -1 : 1;
    }

    a = (ad == std::string_view::npos) ? std::string_view {} : a.substr(ad + 1);
    b = (bd == std::string_view::npos) ? std::string_view {} : b.substr(bd + 1);
  }
  return 0;
}

bool SemVer::operator<(const SemVer& other) const
{
  if (major != other.major) return major < other.major;
  if (minor != other.minor) return minor < other.minor;
  if (patch != other.patch) return patch < other.patch;

  // Prerelease precedence (semver 11.3): a version WITH a prerelease is lower
  // than the associated normal version. Build metadata is ignored (semver 10).
  bool thisPre  = !prerelease.empty();
  bool otherPre = !other.prerelease.empty();
  if (thisPre != otherPre) {
    return thisPre;  // this has a prerelease, other is the release -> this < other
  }
  if (!thisPre) {
    return false;  // both normal versions of the same core -> equal precedence
  }

  return semverComparePrerelease(prerelease, other.prerelease) < 0;
}

bool SemVer::operator==(std::string_view other) const
{
  auto otherSemVer = OpenShock::TryParseSemVer(other);
  if (!otherSemVer) {
    return false;
  }

  return *this == *otherSemVer;
}

bool SemVer::operator<(std::string_view other) const
{
  auto otherSemVer = OpenShock::TryParseSemVer(other);
  return otherSemVer && *this < *otherSemVer;
}

bool SemVer::operator<=(std::string_view other) const
{
  auto otherSemVer = OpenShock::TryParseSemVer(other);
  return otherSemVer && *this <= *otherSemVer;
}

bool SemVer::operator>(std::string_view other) const
{
  auto otherSemVer = OpenShock::TryParseSemVer(other);
  return otherSemVer && *this > *otherSemVer;
}

bool SemVer::operator>=(std::string_view other) const
{
  auto otherSemVer = OpenShock::TryParseSemVer(other);
  return otherSemVer && *this >= *otherSemVer;
}

std::optional<SemVer> OpenShock::TryParseSemVer(std::string_view semverStr)
{
  SemVer semver;

  // Peel off build (after the first '+') then prerelease (after the first '-'),
  // leaving the bare major.minor.patch core. Splitting the whole string by '.'
  // up front would corrupt a dotted prerelease/build such as "-rc.7".
  std::string_view rest = semverStr;

  // A separator must be followed by at least one identifier ("1.0.0-" and "1.0.0+" are invalid).
  std::string_view build;
  size_t plusIdx = rest.find('+');
  if (plusIdx != std::string_view::npos) {
    build = rest.substr(plusIdx + 1);
    rest  = rest.substr(0, plusIdx);
    if (build.empty()) {
      OS_LOGE(TAG, "Empty build metadata");
      return std::nullopt;
    }
  }

  std::string_view prerelease;
  size_t dashIdx = rest.find('-');
  if (dashIdx != std::string_view::npos) {
    prerelease = rest.substr(dashIdx + 1);
    rest       = rest.substr(0, dashIdx);
    if (prerelease.empty()) {
      OS_LOGE(TAG, "Empty prerelease");
      return std::nullopt;
    }
  }

  auto parts = OpenShock::TryStringSplit<3>(rest, '.');
  if (!parts) {
    OS_LOGE(TAG, "Failed to split version core: %.*s", static_cast<int>(rest.length()), rest.data());
    return std::nullopt;
  }

  auto parsedMajor = Convert::ToUint16((*parts)[0]);
  auto parsedMinor = Convert::ToUint16((*parts)[1]);
  auto parsedPatch = Convert::ToUint16((*parts)[2]);
  if (!parsedMajor || !parsedMinor || !parsedPatch) {
    OS_LOGE(TAG, "Invalid version core: %.*s", static_cast<int>(rest.length()), rest.data());
    return std::nullopt;
  }

  semver.major = *parsedMajor;
  semver.minor = *parsedMinor;
  semver.patch = *parsedPatch;

  if (!prerelease.empty() && !semverIsPrerelease(prerelease)) {
    OS_LOGE(TAG, "Invalid prerelease: %.*s", static_cast<int>(prerelease.length()), prerelease.data());
    return std::nullopt;
  }
  if (!build.empty() && !semverIsBuild(build)) {
    OS_LOGE(TAG, "Invalid build: %.*s", static_cast<int>(build.length()), build.data());
    return std::nullopt;
  }

  semver.prerelease.assign(prerelease);
  semver.build.assign(build);

  return semver;
}
