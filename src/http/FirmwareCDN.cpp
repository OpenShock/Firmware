#include <freertos/FreeRTOS.h>

#include "http/FirmwareCDN.h"

#include "Common.h"
#include "http/ContentTypes.h"
#include "Logging.h"
#include "util/HexUtils.h"
#include "util/StringUtils.h"

const char* const TAG = "FirmwareCDN";

using namespace std::string_view_literals;

using namespace OpenShock;

static const uint16_t s_acceptedCodes[] = {200, 304};

HTTP::Response<OpenShock::SemVer> HTTP::FirmwareCDN::GetFirmwareVersion(OtaUpdateChannel channel)
{
  std::string_view channelIndexUrl;
  switch (channel) {
    case OtaUpdateChannel::Stable:
      channelIndexUrl = OPENSHOCK_FW_CDN_STABLE_URL ""sv;
      break;
    case OtaUpdateChannel::Beta:
      channelIndexUrl = OPENSHOCK_FW_CDN_BETA_URL ""sv;
      break;
    case OtaUpdateChannel::Develop:
      channelIndexUrl = OPENSHOCK_FW_CDN_DEVELOP_URL ""sv;
      break;
    default:
      OS_LOGE(TAG, "Unknown channel: %u", channel);
      return {RequestResult::InternalError, 0, {}};
  }

  OS_LOGD(TAG, "Fetching firmware version from %.*s", channelIndexUrl.size(), channelIndexUrl.data());

  auto response = OpenShock::HTTP::GetString(
    channelIndexUrl,
    {
      {"Accept", HTTP::ContentType::TextPlain}
  },
    s_acceptedCodes
  );

  if (response.result != OpenShock::HTTP::RequestResult::Success) {
    OS_LOGE(TAG, "Failed to fetch firmware version: [%u] %s", response.code, response.data.c_str());
    return {RequestResult::InternalError, 0, {}};
  }

  OpenShock::SemVer version;
  if (!OpenShock::TryParseSemVer(response.data, version)) {
    OS_LOGE(TAG, "Failed to parse firmware version: %.*s", response.data.size(), response.data.data());
    return {RequestResult::ParseFailed, response.code, {}};
  }

  return {response.result, response.code, version};
}

HTTP::Response<std::vector<std::string>> HTTP::FirmwareCDN::GetFirmwareBoards(const OpenShock::SemVer& version)
{
  std::string channelIndexUrl;
  if (!FormatToString(channelIndexUrl, OPENSHOCK_FW_CDN_BOARDS_INDEX_URL_FORMAT, version.toString().c_str())) {
    OS_LOGE(TAG, "Failed to format URL");
    return {RequestResult::InternalError, 0, {}};
  }

  OS_LOGD(TAG, "Fetching firmware boards from %s", channelIndexUrl.c_str());

  auto response = OpenShock::HTTP::GetString(
    channelIndexUrl,
    {
      {"Accept", HTTP::ContentType::TextPlain}
  },
    s_acceptedCodes
  );

  if (response.result != OpenShock::HTTP::RequestResult::Success) {
    OS_LOGE(TAG, "Failed to fetch firmware boards: [%u] %s", response.code, response.data.c_str());
    return {RequestResult::InternalError, 0, {}};
  }

  std::vector<std::string_view> lines = OpenShock::StringSplitNewLines(response.data);

  std::vector<std::string> boards;
  boards.reserve(lines.size());

  for (std::string_view line : lines) {
    line = OpenShock::StringTrim(line);

    if (line.empty()) {
      continue;
    }

    boards.push_back(std::string(line));
  }

  return {response.result, response.code, std::move(boards)};
}

HTTP::Response<std::vector<FirmwareBinaryHash>> HTTP::FirmwareCDN::GetFirmwareBinaryHashes(const OpenShock::SemVer& version)
{
  auto versionStr = version.toString();

  std::string sha256HashesUrl;
  if (!FormatToString(sha256HashesUrl, OPENSHOCK_FW_CDN_SHA256_HASHES_URL_FORMAT, versionStr.c_str())) {
    OS_LOGE(TAG, "Failed to format URL");
    return {RequestResult::InternalError, 0, {}};
  }

  auto sha256HashesResponse = OpenShock::HTTP::GetString(
    sha256HashesUrl,
    {
      {"Accept", HTTP::ContentType::TextPlain}
  },
    s_acceptedCodes
  );
  if (sha256HashesResponse.result != OpenShock::HTTP::RequestResult::Success) {
    OS_LOGE(TAG, "Failed to fetch hashes: [%u] %s", sha256HashesResponse.code, sha256HashesResponse.data.c_str());
    return {RequestResult::InternalError, 0, {}};
  }

  std::vector<std::string_view> hashesLines = OpenShock::StringSplitNewLines(sha256HashesResponse.data);

  std::vector<FirmwareBinaryHash> hashes;
  for (std::string_view line : hashesLines) {
    std::vector<std::string_view> parts = OpenShock::StringSplitWhiteSpace(line);
    if (parts.size() != 2) {
      OS_LOGE(TAG, "Invalid hashes entry: %.*s", line.size(), line.data());
      return {RequestResult::InternalError, 0, {}};
    }

    auto hash = OpenShock::StringTrim(parts[0]);
    auto file = OpenShock::StringTrim(parts[1]);

    file = OpenShock::StringRemovePrefix(file, "./"sv);

    if (hash.size() != 64) {
      OS_LOGE(TAG, "Invalid hash: %.*s", hash.size(), hash.data());
      return {RequestResult::InternalError, 0, {}};
    }

    FirmwareBinaryHash binaryHash;

    if (!HexUtils::TryParseHex(hash.data(), hash.size(), binaryHash.hash, sizeof(binaryHash.hash))) {
      OS_LOGE(TAG, "Failed to parse hash: %.*s", hash.size(), hash.data());
      return {RequestResult::InternalError, 0, {}};
    }

    binaryHash.name = std::string(file);

    hashes.push_back(std::move(binaryHash));
  }

  return {RequestResult::Success, 200, std::move(hashes)};
}

HTTP::Response<FirmwareReleaseInfo> HTTP::FirmwareCDN::GetFirmwareReleaseInfo(const OpenShock::SemVer& version)
{
  auto versionStr = version.toString();

  FirmwareReleaseInfo release;
  if (!FormatToString(release.appBinaryUrl, OPENSHOCK_FW_CDN_APP_URL_FORMAT, versionStr.c_str())) {
    OS_LOGE(TAG, "Failed to format URL");
    return {RequestResult::InternalError, 0, {}};
  }

  if (!FormatToString(release.filesystemBinaryUrl, OPENSHOCK_FW_CDN_FILESYSTEM_URL_FORMAT, versionStr.c_str())) {
    OS_LOGE(TAG, "Failed to format URL");
    return {RequestResult::InternalError, 0, {}};
  }

  auto response = GetFirmwareBinaryHashes(version);
  if (response.result != HTTP::RequestResult::Success) {
    OS_LOGE(TAG, "Failed to fetch hashes: [%u]", response.code);
    return {response.result, response.code, {}};
  }

  for (const auto& binaryHash : response.data) {
    if (binaryHash.name == "app.bin") {
      static_assert(sizeof(release.appBinaryHash) == sizeof(binaryHash.hash), "Hash size mismatch");
      memcpy(release.appBinaryHash, binaryHash.hash, sizeof(release.appBinaryHash));
    } else if (binaryHash.name == "staticfs.bin") {
      static_assert(sizeof(release.filesystemBinaryHash) == sizeof(binaryHash.hash), "Hash size mismatch");
      memcpy(release.filesystemBinaryHash, binaryHash.hash, sizeof(release.filesystemBinaryHash));
    }
  }

  return {response.result, response.code, release};
}
