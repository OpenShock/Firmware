#include "SemVer.h"

const char* const TAG = "SemVer";

#include "Convert.h"
#include "Logging.h"
#include "util/DigitCounter.h"
#include "util/StringUtils.h"

using namespace OpenShock;

// https://semver.org/#backusnaur-form-grammar-for-valid-semver-versions
#pragma region Validation Functions

constexpr bool _semverIsLetter(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
constexpr bool _semverIsPositiveDigit(char c)
{
  return c >= '1' && c <= '9';
}
constexpr bool _semverIsDigit(char c)
{
  return c == '0' || _semverIsPositiveDigit(c);
}
constexpr bool _semverIsDigits(std::string_view str)
{
  if (str.empty()) {
    return false;
  }

  for (auto c : str) {
    if (!_semverIsDigit(c)) {
      return false;
    }
  }

  return true;
}
constexpr bool _semverIsNonDigit(char c)
{
  return _semverIsLetter(c) || c == '-';
}
constexpr bool _semverIsIdentifierChararacter(char c)
{
  return _semverIsDigit(c) || _semverIsNonDigit(c);
}
constexpr bool _semverIsIdentifierChararacters(std::string_view str)
{
  if (str.empty()) {
    return false;
  }

  for (auto c : str) {
    if (!_semverIsIdentifierChararacter(c)) {
      return false;
    }
  }

  return true;
}
constexpr bool _semverIsNumericIdentifier(std::string_view str)
{
  if (str.empty()) {
    return false;
  }

  if (str.length() == 1) {
    return _semverIsDigit(str[0]);
  }

  return _semverIsPositiveDigit(str[0]) && _semverIsDigits(str.substr(1));
}
constexpr bool _semverIsAlphanumericIdentifier(std::string_view str)
{
  if (str.empty()) {
    return false;
  }

  if (str.length() == 1) {
    return _semverIsNonDigit(str[0]);
  }

  std::size_t nonDigitPos = std::string_view::npos;
  for (std::size_t i = 0; i < str.length(); ++i) {
    if (_semverIsNonDigit(str[i])) {
      nonDigitPos = i;
      break;
    }
  }

  if (nonDigitPos == std::string_view::npos) {
    return false;
  }

  auto after = str.substr(nonDigitPos + 1);

  if (nonDigitPos == 0) {
    return _semverIsIdentifierChararacters(after);
  }

  auto before = str.substr(0, nonDigitPos);

  if (nonDigitPos == str.length() - 1) {
    return _semverIsIdentifierChararacters(before);
  }

  return _semverIsIdentifierChararacters(before) && _semverIsIdentifierChararacters(after);
}
constexpr bool _semverIsBuildIdentifier(std::string_view str)
{
  return _semverIsAlphanumericIdentifier(str) || _semverIsDigits(str);
}
constexpr bool _semverIsPrereleaseIdentifier(std::string_view str)
{
  return _semverIsAlphanumericIdentifier(str) || _semverIsNumericIdentifier(str);
}
constexpr bool _semverIsDotSeperatedBuildIdentifiers(std::string_view str)
{
  if (str.empty()) {
    return false;
  }

  auto dotIdx = str.find('.');
  while (dotIdx != std::string_view::npos) {
    auto part = str.substr(0, dotIdx);
    if (!_semverIsBuildIdentifier(part)) {
      return false;
    }

    str    = str.substr(dotIdx + 1);
    dotIdx = str.find('.');
  }

  return _semverIsBuildIdentifier(str);
}
constexpr bool _semverIsDotSeperatedPreleaseIdentifiers(std::string_view str)
{
  if (str.empty()) {
    return false;
  }

  auto dotIdx = str.find('.');
  while (dotIdx != std::string_view::npos) {
    auto part = str.substr(0, dotIdx);
    if (!_semverIsPrereleaseIdentifier(part)) {
      return false;
    }

    str    = str.substr(dotIdx + 1);
    dotIdx = str.find('.');
  }

  return _semverIsPrereleaseIdentifier(str);
}

// For readability
#define _semverIsPatch      _semverIsNumericIdentifier
#define _semverIsMinor      _semverIsNumericIdentifier
#define _semverIsMajor      _semverIsNumericIdentifier
#define _semverIsPrerelease _semverIsDotSeperatedPreleaseIdentifiers
#define _semverIsBuild      _semverIsDotSeperatedBuildIdentifiers

constexpr bool _semverIsVersionCore(std::string_view str)
{
  if (str.empty()) {
    return false;
  }

  std::string_view parts[3];
  if (!OpenShock::TryStringSplit(str, '.', parts)) {
    return false;
  }

  return _semverIsMajor(parts[0]) && _semverIsMinor(parts[1]) && _semverIsPatch(parts[2]);
}
constexpr bool _semverIsSemver(std::string_view str)
{
  if (str.empty()) {
    return false;
  }

  auto dashPos = str.find('-');
  auto plusPos = str.find('+');

  if (dashPos == std::string_view::npos && plusPos == std::string_view::npos) {
    return _semverIsVersionCore(str);
  }

  if (dashPos != std::string_view::npos && plusPos != std::string_view::npos) {
    if (dashPos > plusPos) {
      return false;
    }

    auto core       = str.substr(0, dashPos);
    auto prerelease = str.substr(dashPos + 1, plusPos - dashPos - 1);
    auto build      = str.substr(plusPos + 1);

    return _semverIsVersionCore(core) && _semverIsPrerelease(prerelease) && _semverIsBuild(build);
  }

  if (dashPos != std::string_view::npos) {
    auto core       = str.substr(0, dashPos);
    auto prerelease = str.substr(dashPos + 1);

    return _semverIsVersionCore(core) && _semverIsPrerelease(prerelease);
  }

  if (plusPos != std::string_view::npos) {
    auto core  = str.substr(0, plusPos);
    auto build = str.substr(plusPos + 1);

    return _semverIsVersionCore(core) && _semverIsBuild(build);
  }

  return false;
}
#pragma endregion

bool SemVer::isValid() const
{
  if (!this->prerelease.empty() && !_semverIsPrereleaseIdentifier(this->prerelease)) {
    return false;
  }

  if (!this->build.empty() && !_semverIsBuildIdentifier(this->build)) {
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
    str.append(prerelease.data(), prerelease.length());
  }

  if (!build.empty()) {
    str.push_back('+');
    str.append(build.data(), build.length());
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

  std::string_view restStr = patchStr.substr(std::min(dashIdx, plusIdx));
  patchStr.remove_suffix(restStr.length());

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

      if (!semver.build.empty() && !_semverIsBuild(semver.build)) {
        OS_LOGE(TAG, "Invalid build: %s", semver.build.c_str());
        return false;
      }
    }

    if (dashIdx != std::string_view::npos) {
      semver.prerelease = restStr.substr(1);

      if (!semver.prerelease.empty() && !_semverIsPrerelease(semver.prerelease)) {
        OS_LOGE(TAG, "Invalid prerelease: %s", semver.prerelease.c_str());
        return false;
      }
    }
  }

  return true;
}
