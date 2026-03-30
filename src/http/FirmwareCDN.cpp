#include <freertos/FreeRTOS.h>

#include "http/FirmwareCDN.h"

#include "Common.h"
#include "http/ContentTypes.h"
#include "Logging.h"
#include "util/HexUtils.h"

#include <cJSON.h>

const char* const TAG = "FirmwareCDN";

using namespace std::string_view_literals;

using namespace OpenShock;

static bool parseLatestResponse(const std::string& jsonStr, OpenShock::SemVer& version, FirmwareReleaseInfo& release)
{
  cJSON* root = cJSON_Parse(jsonStr.c_str());
  if (root == nullptr) {
    OS_LOGE(TAG, "Failed to parse JSON response");
    return false;
  }

  // Extract version string.
  cJSON* versionObj = cJSON_GetObjectItemCaseSensitive(root, "version");
  if (!cJSON_IsString(versionObj) || versionObj->valuestring == nullptr) {
    OS_LOGE(TAG, "Missing or invalid 'version' field in response");
    cJSON_Delete(root);
    return false;
  }

  if (!OpenShock::TryParseSemVer(versionObj->valuestring, version)) {
    OS_LOGE(TAG, "Failed to parse version string: %s", versionObj->valuestring);
    cJSON_Delete(root);
    return false;
  }

  // Extract artifacts for our board.
  cJSON* artifacts = cJSON_GetObjectItemCaseSensitive(root, "artifacts");
  if (!cJSON_IsObject(artifacts)) {
    OS_LOGE(TAG, "Missing or invalid 'artifacts' field in response");
    cJSON_Delete(root);
    return false;
  }

  cJSON* boardArtifacts = cJSON_GetObjectItemCaseSensitive(artifacts, OPENSHOCK_FW_BOARD);
  if (!cJSON_IsArray(boardArtifacts)) {
    OS_LOGE(TAG, "No artifacts found for board '%s'", OPENSHOCK_FW_BOARD);
    cJSON_Delete(root);
    return false;
  }

  bool foundApp = false, foundStaticFs = false;
  cJSON* artifact = nullptr;
  cJSON_ArrayForEach(artifact, boardArtifacts)
  {
    if (!cJSON_IsObject(artifact)) {
      continue;
    }

    cJSON* typeObj = cJSON_GetObjectItemCaseSensitive(artifact, "type");
    cJSON* urlObj  = cJSON_GetObjectItemCaseSensitive(artifact, "url");
    cJSON* hashObj = cJSON_GetObjectItemCaseSensitive(artifact, "sha256Hash");

    if (!cJSON_IsString(typeObj) || !cJSON_IsString(urlObj) || !cJSON_IsString(hashObj)) {
      continue;
    }

    std::string_view type = typeObj->valuestring;
    const char* url       = urlObj->valuestring;
    const char* hash      = hashObj->valuestring;

    if (type == "app"sv) {
      release.appBinaryUrl = url;
      if (HexUtils::TryParseHex(hash, 64, release.appBinaryHash, 32) != 32) {
        OS_LOGE(TAG, "Failed to parse app binary hash");
        cJSON_Delete(root);
        return false;
      }
      foundApp = true;
    } else if (type == "staticfs"sv) {
      release.filesystemBinaryUrl = url;
      if (HexUtils::TryParseHex(hash, 64, release.filesystemBinaryHash, 32) != 32) {
        OS_LOGE(TAG, "Failed to parse filesystem binary hash");
        cJSON_Delete(root);
        return false;
      }
      foundStaticFs = true;
    }
  }

  cJSON_Delete(root);

  if (!foundApp) {
    OS_LOGE(TAG, "No 'app' artifact found for board '%s'", OPENSHOCK_FW_BOARD);
    return false;
  }

  if (!foundStaticFs) {
    OS_LOGE(TAG, "No 'staticfs' artifact found for board '%s'", OPENSHOCK_FW_BOARD);
    return false;
  }

  return true;
}

HTTP::Response<HTTP::FirmwareCDN::LatestRelease> HTTP::FirmwareCDN::GetLatestRelease(OtaUpdateChannel channel, const std::string& repoDomain)
{
  const char* channelStr;
  switch (channel) {
    case OtaUpdateChannel::Stable:
      channelStr = "stable";
      break;
    case OtaUpdateChannel::Beta:
      channelStr = "beta";
      break;
    case OtaUpdateChannel::Develop:
      channelStr = "develop";
      break;
    default:
      OS_LOGE(TAG, "Unknown channel: %u", channel);
      return {RequestResult::InternalError, 0, {}};
  }

  // Build URL: https://{repoDomain}/v2/firmware/latest/{channel}?board={board}
  std::string url = "https://";
  url += repoDomain;
  url += "/v2/firmware/latest/";
  url += channelStr;
  url += "?board=";
  url += OPENSHOCK_FW_BOARD;

  OS_LOGD(TAG, "Fetching latest firmware from %s", url.c_str());

  static const uint16_t s_acceptedCodes[] = {200, 304};

  auto response = OpenShock::HTTP::GetString(
    url,
    {
      {"Accept", HTTP::ContentType::JSON}
  },
    s_acceptedCodes
  );

  if (response.result != OpenShock::HTTP::RequestResult::Success) {
    OS_LOGE(TAG, "Failed to fetch latest firmware: [%u] %s", response.code, response.data.c_str());
    return {RequestResult::InternalError, 0, {}};
  }

  LatestRelease latest;
  if (!parseLatestResponse(response.data, latest.version, latest.release)) {
    OS_LOGE(TAG, "Failed to parse firmware release response");
    return {RequestResult::ParseFailed, response.code, {}};
  }

  return {response.result, response.code, std::move(latest)};
}

HTTP::Response<FirmwareReleaseInfo> HTTP::FirmwareCDN::GetRelease(const OpenShock::SemVer& version, const std::string& repoDomain)
{
  std::string versionStr = version.toString();

  // Build URL: https://{repoDomain}/v2/firmware/versions/{version}?board={board}
  std::string url = "https://";
  url += repoDomain;
  url += "/v2/firmware/versions/";
  url += versionStr;
  url += "?board=";
  url += OPENSHOCK_FW_BOARD;

  OS_LOGD(TAG, "Fetching firmware release %s from %s", versionStr.c_str(), url.c_str());

  static const uint16_t s_acceptedCodes[] = {200, 304};

  auto response = OpenShock::HTTP::GetString(
    url,
    {
      {"Accept", HTTP::ContentType::JSON}
  },
    s_acceptedCodes
  );

  if (response.result != OpenShock::HTTP::RequestResult::Success) {
    OS_LOGE(TAG, "Failed to fetch firmware release: [%u] %s", response.code, response.data.c_str());
    return {RequestResult::InternalError, 0, {}};
  }

  // Reuse the same parser — the response format includes version + artifacts.
  OpenShock::SemVer parsedVersion;
  FirmwareReleaseInfo release;
  if (!parseLatestResponse(response.data, parsedVersion, release)) {
    OS_LOGE(TAG, "Failed to parse firmware release response");
    return {RequestResult::ParseFailed, response.code, {}};
  }

  return {response.result, response.code, std::move(release)};
}
