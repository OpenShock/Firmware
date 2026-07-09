#include "SemVer.h"

const char* const TAG = "SemVer";

#include "Convert.h"
#include "Logging.h"
#include "util/DigitCounter.h"
#include "util/StringUtils.h"

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
#define semverIsPatch      semverIsNumericIdentifier
#define semverIsMinor      semverIsNumericIdentifier
#define semverIsMajor      semverIsNumericIdentifier
#define semverIsPrerelease semverIsDotSeperatedPreleaseIdentifiers
#define semverIsBuild      semverIsDotSeperatedBuildIdentifiers

static constexpr bool semverIsVersionCore(std::string_view str)
{
  if (str.empty()) {
    return false;
  }

  std::string_view parts[3];
  if (!OpenShock::TryStringSplit(str, '.', parts)) {
    return false;
  }

  return semverIsMajor(parts[0]) && semverIsMinor(parts[1]) && semverIsPatch(parts[2]);
}
static constexpr bool semverIsSemver(std::string_view str)
{
  if (str.empty()) {
    return false;
  }

  auto dashPos = str.find('-');
  auto plusPos = str.find('+');

  if (dashPos == std::string_view::npos && plusPos == std::string_view::npos) {
    return semverIsVersionCore(str);
  }

  if (dashPos != std::string_view::npos && plusPos != std::string_view::npos) {
    if (dashPos > plusPos) {
      return false;
    }

    auto core       = str.substr(0, dashPos);
    auto prerelease = str.substr(dashPos + 1, plusPos - dashPos - 1);
    auto build      = str.substr(plusPos + 1);

    return semverIsVersionCore(core) && semverIsPrerelease(prerelease) && semverIsBuild(build);
  }

  if (dashPos != std::string_view::npos) {
    auto core       = str.substr(0, dashPos);
    auto prerelease = str.substr(dashPos + 1);

    return semverIsVersionCore(core) && semverIsPrerelease(prerelease);
  }

  if (plusPos != std::string_view::npos) {
    auto core  = str.substr(0, plusPos);
    auto build = str.substr(plusPos + 1);

    return semverIsVersionCore(core) && semverIsBuild(build);
  }

  return false;
}
#pragma endregion

bool SemVer::isValid() const
{
  if (!this->prerelease.empty() && !semverIsPrereleaseIdentifier(this->prerelease)) {
    return false;
  }

  if (!this->build.empty() && !semverIsBuildIdentifier(this->build)) {
    return false;
  }

  return true;
}

std::string SemVer::toString() const
{
  std::size_t length = 2 + Util::Digits10Count(major) + Util::Digits10Count(minor) + Util::Digits10Count(patch);
  if (!prerelease.empty()) {
    length += 1 + prerelease.length();
  }
  if (!build.empty()) {
    length += 1 + build.length();
  }

  std::string str;
  str.reserve(length);

  Convert::FromUint16(major, str);
  str.push_back('.');
  Convert::FromUint16(minor, str);
  str.push_back('.');
  Convert::FromUint16(patch, str);

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
  SemVer otherSemVer;
  if (!OpenShock::TryParseSemVer(other, otherSemVer)) {
    return false;
  }

  return *this == otherSemVer;
}

bool SemVer::operator<(std::string_view other) const
{
  SemVer otherSemVer;
  if (!OpenShock::TryParseSemVer(other, otherSemVer)) {
    return false;
  }

  return *this < otherSemVer;
}

bool OpenShock::TryParseSemVer(std::string_view semverStr, SemVer& semver)
{
  // Peel off build (after the first '+') then prerelease (after the first '-'),
  // leaving the bare major.minor.patch core. Splitting the whole string by '.'
  // up front would corrupt a dotted prerelease/build such as "-rc.7".
  std::string_view rest = semverStr;

  std::string_view build;
  size_t plusIdx = rest.find('+');
  if (plusIdx != std::string_view::npos) {
    build = rest.substr(plusIdx + 1);
    rest  = rest.substr(0, plusIdx);
  }

  std::string_view prerelease;
  size_t dashIdx = rest.find('-');
  if (dashIdx != std::string_view::npos) {
    prerelease = rest.substr(dashIdx + 1);
    rest       = rest.substr(0, dashIdx);
  }

  std::string_view parts[3];
  if (!OpenShock::TryStringSplit(rest, '.', parts)) {
    OS_LOGE(TAG, "Failed to split version core: %.*s", static_cast<int>(rest.length()), rest.data());
    return false;
  }

  if (!Convert::ToUint16(parts[0], semver.major) || !Convert::ToUint16(parts[1], semver.minor) || !Convert::ToUint16(parts[2], semver.patch)) {
    OS_LOGE(TAG, "Invalid version core: %.*s", static_cast<int>(rest.length()), rest.data());
    return false;
  }

  if (!prerelease.empty() && !semverIsPrerelease(prerelease)) {
    OS_LOGE(TAG, "Invalid prerelease: %.*s", static_cast<int>(prerelease.length()), prerelease.data());
    return false;
  }
  if (!build.empty() && !semverIsBuild(build)) {
    OS_LOGE(TAG, "Invalid build: %.*s", static_cast<int>(build.length()), build.data());
    return false;
  }

  semver.prerelease.assign(prerelease);
  semver.build.assign(build);

  return true;
}
