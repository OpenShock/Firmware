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

bool SemVer::operator<(const SemVer& other) const
{
  if (major < other.major) {
    return true;
  }
  if (major > other.major) {
    return false;
  }

  if (minor < other.minor) {
    return true;
  }
  if (minor > other.minor) {
    return false;
  }

  if (patch < other.patch) {
    return true;
  }
  if (patch > other.patch) {
    return false;
  }

  if (prerelease < other.prerelease) {
    return true;
  }
  if (prerelease > other.prerelease) {
    return false;
  }

  return build < other.build;
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
  std::string_view parts[3];
  if (!OpenShock::TryStringSplit(semverStr, '.', parts)) {
    OS_LOGE(TAG, "Failed to split version string: %.*s", semverStr.length(), semverStr.data());
    return false;
  }

  std::string_view majorStr = parts[0], minorStr = parts[1], patchStr = parts[2];

  size_t plusIdx = patchStr.find('+');
  size_t dashIdx = patchStr.find('-');

  std::string_view restStr;
  if (dashIdx != std::string_view::npos || plusIdx != std::string_view::npos) {
    restStr = patchStr.substr(std::min(dashIdx, plusIdx));
    patchStr.remove_suffix(restStr.length());
  }

  if (!Convert::ToUint16(majorStr, semver.major)) {
    OS_LOGE(TAG, "Invalid major version: %.*s", majorStr.length(), majorStr.data());
    return false;
  }

  if (!Convert::ToUint16(minorStr, semver.minor)) {
    OS_LOGE(TAG, "Invalid minor version: %.*s", minorStr.length(), minorStr.data());
    return false;
  }

  if (!Convert::ToUint16(patchStr, semver.patch)) {
    OS_LOGE(TAG, "Invalid patch version: %.*s", patchStr.length(), patchStr.data());
    return false;
  }

  if (!restStr.empty()) {
    if (plusIdx != std::string_view::npos) {
      semver.build = restStr.substr((plusIdx - patchStr.length()) + 1);
      restStr.remove_suffix(semver.build.length() + 1);

      if (!semver.build.empty() && !semverIsBuild(semver.build)) {
        OS_LOGE(TAG, "Invalid build: %s", semver.build.c_str());
        return false;
      }
    }

    if (dashIdx != std::string_view::npos) {
      semver.prerelease = restStr.substr(1);

      if (!semver.prerelease.empty() && !semverIsPrerelease(semver.prerelease)) {
        OS_LOGE(TAG, "Invalid prerelease: %s", semver.prerelease.c_str());
        return false;
      }
    }
  }

  return true;
}
