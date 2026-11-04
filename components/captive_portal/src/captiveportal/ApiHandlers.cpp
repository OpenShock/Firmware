#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "captiveportal/ApiHandlers.h"

const char* const TAG = "CaptivePortalApi";

#include "AppHooks.h"
#include "BoardDescriptor.h"
#include "captiveportal/CaptivePortalInstance.h"
#include "captiveportal/HttpUtils.h"
#include "captiveportal/Manager.h"
#include "CommandHandler.h"
#include "Convert.h"
#include "enums/OtaUpdateChannel.h"
#include "enums/StatusLed.h"
#include "GatewayConnectionManager.h"
#include "http/ContentTypes.h"
#include "Logging.h"
#include "OtaUpdateConfig.h"
#include "RateLimiter.h"
#include "util/TaskUtils.h"
#include "visual/LedConfig.h"
#include "visual/VisualStateManager.h"
#include "wifi/WiFiManager.h"
#include "wifi/WiFiScanManager.h"

#include "json/Json.h"

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

static const char* const JSON_ERR_INTERNAL        = "{\"error\":\"InternalError\"}";
static const char* const JSON_ERR_MISSING_PARAM   = "{\"error\":\"MissingParam\"}";
static const char* const JSON_ERR_INVALID_PARAM   = "{\"error\":\"InvalidParam\"}";
static const char* const JSON_ERR_INVALID_PIN     = "{\"error\":\"InvalidPin\"}";
static const char* const JSON_ERR_PIN_IN_USE      = "{\"error\":\"PinInUse\"}";
static const char* const JSON_ERR_MISSING_SSID    = "{\"error\":\"MissingSsid\"}";
static const char* const JSON_ERR_INVALID_SSID    = "{\"error\":\"InvalidSsid\"}";
static const char* const JSON_ERR_PASSWORD_SHORT  = "{\"error\":\"PasswordTooShort\"}";
static const char* const JSON_ERR_PASSWORD_LONG   = "{\"error\":\"PasswordTooLong\"}";
static const char* const JSON_ERR_CODE_REQUIRED   = "{\"error\":\"CodeRequired\"}";
static const char* const JSON_ERR_INVALID_CHANNEL = "{\"error\":\"InvalidChannel\"}";
static const char* const JSON_ERR_RATE_LIMITED    = "{\"error\":\"RateLimited\"}";

using namespace OpenShock;
using namespace OpenShock::CaptivePortal;

static OpenShock::RateLimiter& getAccountLinkRateLimiter()
{
  // Only called from the httpd task
  static OpenShock::RateLimiter* rl = nullptr;
  if (rl == nullptr) {
    rl = new OpenShock::RateLimiter();
    rl->addLimit(60'000, 5);    // 5 attempts per minute
    rl->addLimit(300'000, 10);  // 10 attempts per 5 minutes
  }
  return *rl;
}

static esp_err_t sendParamError(httpd_req_t* req, ParamError error)
{
  return sendResp(req, S400, HTTP::ContentType::JSON, error == ParamError::Missing ? JSON_ERR_MISSING_PARAM : JSON_ERR_INVALID_PARAM);
}

static esp_err_t apiBoard(httpd_req_t* req)
{
  bool hasPredefinedPins = Board::Get().rfTxGpio != OPENSHOCK_GPIO_INVALID;
  return sendResp(req, S200, HTTP::ContentType::JSON, hasPredefinedPins ? "{\"has_predefined_pins\":true}" : "{\"has_predefined_pins\":false}");
}

static esp_err_t apiPortalClose(httpd_req_t* req)
{
  CaptivePortal::SetUserDone();
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiWifiScan(httpd_req_t* req)
{
  auto run = getBoolQueryParam(req, "run");
  if (!run && run.error() == ParamError::Invalid) {
    return sendParamError(req, run.error());
  }
  if (run.value_or(true)) {
    WiFiScanManager::StartScan();
  } else {
    WiFiScanManager::AbortScan();
  }
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiWifiNetworksDelete(httpd_req_t* req)
{
  auto ssid = getQueryParam(req, "ssid");
  if (!ssid) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_MISSING_SSID);
  }
  if (!WiFiManager::Forget(ssid->c_str())) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t sendAccountLinkResult(httpd_req_t* req, OpenShock::AccountLinkResultCode result)
{
  if (result == OpenShock::AccountLinkResultCode::Success) {
    return sendResp(req, S200, nullptr, {});
  }
  const char* error = OpenShock::AccountLinkResultCodeToString(result);
  OpenShock::JSON::StringWriter writer;
  json_gen_str_t* gen = writer.gen();
  json_gen_start_object(gen);
  OpenShock::JSON::objSetString(gen, "error", error);
  json_gen_end_object(gen);
  std::string json = writer.finish();
  return sendResp(req, S400, HTTP::ContentType::JSON, json);
}

namespace {
  struct AccountLinkJob {
    // Keeps the instance alive, so its server can't be stopped under the async request; the manager's RetiredList
    // destroys it only after this is dropped.
    std::shared_ptr<CaptivePortal::CaptivePortalInstance> instance;
    httpd_req_t* req;  // async copy, owned until httpd_req_async_handler_complete
    std::string code;
  };

  std::atomic<bool> s_accountLinkInProgress = false;

  // Linking does DNS + TLS + an HTTPS round trip (seconds); running it here keeps the single httpd task free to
  // serve the page, assets and WebSocket meanwhile.
  void accountLinkTask(void* arg)
  {
    auto* job   = static_cast<AccountLinkJob*>(arg);
    auto result = GatewayConnectionManager::Link(std::string_view(job->code));

    sendAccountLinkResult(job->req, result);
    httpd_req_async_handler_complete(job->req);

    delete job;
    s_accountLinkInProgress.store(false);
    vTaskDelete(nullptr);
  }
}  // namespace

static esp_err_t apiAccountLink(httpd_req_t* req)
{
  if (!getAccountLinkRateLimiter().tryRequest()) {
    return sendResp(req, S429, HTTP::ContentType::JSON, JSON_ERR_RATE_LIMITED);
  }
  auto code = getQueryParam(req, "code");
  if (!code) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_CODE_REQUIRED);
  }

  // Null while the instance is still being constructed or once it is retired
  auto instance = CaptivePortal::GetLiveInstance(static_cast<CaptivePortal::CaptivePortalInstance*>(req->user_ctx));
  if (instance == nullptr) {
    return sendResp(req, S503, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }

  if (s_accountLinkInProgress.exchange(true)) {
    return sendResp(req, S429, HTTP::ContentType::JSON, JSON_ERR_RATE_LIMITED);
  }

  httpd_req_t* asyncReq = nullptr;
  if (httpd_req_async_handler_begin(req, &asyncReq) != ESP_OK) {
    s_accountLinkInProgress.store(false);
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }

  auto* job = new AccountLinkJob {std::move(instance), asyncReq, std::move(*code)};
  // TLS needs a large stack
  if (TaskUtils::TaskCreateExpensive(accountLinkTask, "AccountLink", CONFIG_OPENSHOCK_PORTAL_ACCOUNT_LINK_TASK_STACK_SIZE, job, 1, nullptr) != pdPASS) {
    OS_LOGE(TAG, "Failed to create account link task");
    delete job;
    esp_err_t err = sendResp(asyncReq, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
    httpd_req_async_handler_complete(asyncReq);
    s_accountLinkInProgress.store(false);
    return err;
  }

  return ESP_OK;
}

static esp_err_t apiAccountDelete(httpd_req_t* req)
{
  GatewayConnectionManager::UnLink();
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiConfigRfPin(httpd_req_t* req)
{
  auto pinStr = getQueryParam(req, "pin");
  if (!pinStr) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PIN);
  }
  auto parsedPin = Convert::ToGpioNum(*pinStr);
  if (!parsedPin) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PIN);
  }
  gpio_num_t pin   = *parsedPin;
  auto result      = CommandHandler::SetRfTxPin(pin);
  using ResultCode = OpenShock::SetGPIOResultCode;
  if (result != ResultCode::Success) {
    const char* error = result == ResultCode::InvalidPin ? JSON_ERR_INVALID_PIN : result == ResultCode::PinInUse ? JSON_ERR_PIN_IN_USE : JSON_ERR_INTERNAL;
    return sendResp(req, S400, HTTP::ContentType::JSON, error);
  }
  OpenShock::JSON::StringWriter writer;
  json_gen_str_t* gen = writer.gen();
  json_gen_start_object(gen);
  json_gen_obj_set_int(gen, "pin", pin);
  json_gen_end_object(gen);
  std::string json = writer.finish();
  return sendResp(req, S200, HTTP::ContentType::JSON, json);
}

static esp_err_t apiWifiNetworksAdd(httpd_req_t* req)
{
  auto body = recvBody(req);
  if (!body) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_MISSING_SSID);
  }

  auto ssid = getFormParam(*body, "ssid");
  if (!ssid && ssid.error() == ParamError::Invalid) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_SSID);
  }
  if (!ssid || ssid->empty() || ssid->length() > 32) {
    return sendResp(req, S400, HTTP::ContentType::JSON, !ssid || ssid->empty() ? JSON_ERR_MISSING_SSID : JSON_ERR_INVALID_SSID);
  }

  // Missing means an open network; an undecodable one is rejected rather than saved as a different password.
  auto passwordParam = getFormParam(*body, "password");
  if (!passwordParam && passwordParam.error() == ParamError::Invalid) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PARAM);
  }
  std::string password = std::move(passwordParam).value_or(std::string());
  if (!password.empty()) {
    if (password.length() < 8) {
      return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_PASSWORD_SHORT);
    }
    if (password.length() > 63) {
      return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_PASSWORD_LONG);
    }
  }

  bool connect    = true;
  auto connectStr = getFormParam(*body, "connect");
  if (!connectStr && connectStr.error() != ParamError::Missing) {
    return sendParamError(req, connectStr.error());
  }
  if (connectStr) {
    auto parsed = parseBool(*connectStr);
    if (!parsed) {
      return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PARAM);
    }
    connect = *parsed;
  }

  wifi_auth_mode_t authMode = WIFI_AUTH_MAX;
  auto securityStr          = getFormParam(*body, "security");
  if (!securityStr && securityStr.error() != ParamError::Missing) {
    return sendParamError(req, securityStr.error());
  }
  if (securityStr) {
    auto sec = Convert::ToUint8(*securityStr);
    if (!sec || *sec > static_cast<uint8_t>(WIFI_AUTH_MAX)) {
      return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PARAM);
    }
    authMode = static_cast<wifi_auth_mode_t>(*sec);
  }

  if (!WiFiManager::Save(ssid->c_str(), std::string_view(password), connect, authMode)) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiWifiConnect(httpd_req_t* req)
{
  auto body = recvBody(req);
  if (!body) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_MISSING_SSID);
  }
  auto ssid = getFormParam(*body, "ssid");
  if (!ssid) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_MISSING_SSID);
  }
  if (!WiFiManager::Connect(ssid->c_str())) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiWifiDisconnect(httpd_req_t* req)
{
  WiFiManager::Disconnect();
  return sendResp(req, S200, nullptr, {});
}

// PUT /api/ota/settings?enabled=&channel=&interval=&allow=&require=
// Any subset of the OTA settings in one request, applied together in a single config write (and none at all when
// nothing changed), instead of one handler and one flash write per setting.
static esp_err_t apiOtaSettings(httpd_req_t* req)
{
  // Parse everything first, then apply it in one atomic update so a concurrent writer (backend, serial) isn't undone
  std::optional<bool> enabled, allow, require;
  std::optional<OtaUpdateChannel> channel;
  std::optional<uint16_t> interval;

  // Reads one optional boolean parameter; returns false (after responding) on an invalid value.
  auto parseBool = [&](const char* key, std::optional<bool>& out, esp_err_t& response) {
    auto value = getBoolQueryParam(req, key);
    if (!value) {
      if (value.error() == ParamError::Missing) return true;
      response = sendParamError(req, value.error());
      return false;
    }
    out = *value;
    return true;
  };

  esp_err_t response = ESP_OK;
  if (!parseBool("enabled", enabled, response) || !parseBool("allow", allow, response) || !parseBool("require", require, response)) {
    return response;
  }

  // Present but undecodable (an encoded NUL) is an error like an unknown value, never a silently skipped setting
  auto channelStr = getQueryParam(req, "channel");
  if (!channelStr && channelStr.error() != ParamError::Missing) {
    return sendParamError(req, channelStr.error());
  }
  if (channelStr) {
    channel = TryParseOtaUpdateChannel(*channelStr);
    if (!channel) {
      return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_CHANNEL);
    }
  }

  auto intervalStr = getQueryParam(req, "interval");
  if (!intervalStr && intervalStr.error() != ParamError::Missing) {
    return sendParamError(req, intervalStr.error());
  }
  if (intervalStr) {
    // Minutes; 0 would make periodic checks fire on every OTA task wake-up
    interval = Convert::ToUint16(*intervalStr);
    if (!interval || *interval == 0) {
      return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PARAM);
    }
  }

  if (!enabled && !allow && !require && !channel && !interval) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_MISSING_PARAM);
  }

  bool saved = Config::UpdateOtaUpdateConfig([&](Config::OtaUpdateConfig& cfg) {
    bool changed = false;
    auto apply   = [&changed](auto& field, const auto& value) {
      if (value && field != *value) {
        field   = *value;
        changed = true;
      }
    };
    apply(cfg.isEnabled, enabled);
    apply(cfg.allowBackendManagement, allow);
    apply(cfg.requireManualApproval, require);
    apply(cfg.updateChannel, channel);
    apply(cfg.checkInterval, interval);
    return changed;
  });
  if (!saved) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  return sendResp(req, S200, nullptr, {});
}

static esp_err_t apiOtaCheck(httpd_req_t* req)
{
  // Checks the configured channel; the update task reports progress over the gateway as usual.
  if (!AppHooks::OtaRequestUpdateCheck()) {
    return sendResp(req, S503, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  return sendResp(req, S200, nullptr, {});
}

// GET /api/led: the LED settings, which LEDs the board has, and the limits the UI shows
static esp_err_t apiLedGet(httpd_req_t* req)
{
  namespace Vsm = OpenShock::VisualStateManager;

  OpenShock::JSON::StringWriter writer;
  json_gen_str_t* gen = writer.gen();
  json_gen_start_object(gen);
  OpenShock::JSON::objSetString(gen, "mode", LedModeName(Vsm::GetLedMode()));
  for (LedType type : {LedType::Mono, LedType::Rgb}) {
    const char* name = type == LedType::Mono ? "mono" : "rgb";
    OpenShock::JSON::objBegin(gen, name);
    json_gen_obj_set_bool(gen, "present", Vsm::HasLed(type));
    json_gen_obj_set_int(gen, "critical", Vsm::GetLedBrightness(type, LedSignal::Critical));
    json_gen_obj_set_int(gen, "normal", Vsm::GetLedBrightness(type, LedSignal::Normal));
    OpenShock::JSON::objEnd(gen, name);
  }
  json_gen_obj_set_int(gen, "rgbWarnAbove", Vsm::kRgbBrightnessWarnPercent);
  json_gen_obj_set_int(gen, "previewMs", static_cast<int>(Vsm::kPreviewDurationMs));
  json_gen_end_object(gen);
  std::string json = writer.finish();
  return sendResp(req, S200, HTTP::ContentType::JSON, json);
}

// Reads an optional 0-100 percent query parameter; false (after responding) on an invalid value
static bool parsePercentParam(httpd_req_t* req, const char* key, std::optional<uint8_t>& out, esp_err_t& response)
{
  auto str = getQueryParam(req, key);
  if (!str) {
    if (str.error() == ParamError::Missing) return true;
    response = sendParamError(req, str.error());
    return false;
  }
  auto value = Convert::ToUint8(*str);
  if (!value || *value > Config::LedConfig::kMaxBrightness) {
    response = sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PARAM);
    return false;
  }
  out = *value;
  return true;
}

// PUT /api/led?mode=&monoCritical=&monoNormal=&rgbCritical=&rgbNormal=
// Any subset of the LED settings, saved in one config write.
static esp_err_t apiLedPut(httpd_req_t* req)
{
  std::optional<LedMode> mode;
  auto modeStr = getQueryParam(req, "mode");
  if (!modeStr && modeStr.error() != ParamError::Missing) {
    return sendParamError(req, modeStr.error());
  }
  if (modeStr) {
    mode = TryParseLedMode(*modeStr);
    if (!mode) {
      return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PARAM);
    }
  }

  std::optional<uint8_t> monoCritical, monoNormal, rgbCritical, rgbNormal;
  esp_err_t response = ESP_OK;
  if (!parsePercentParam(req, "monoCritical", monoCritical, response) || !parsePercentParam(req, "monoNormal", monoNormal, response) || !parsePercentParam(req, "rgbCritical", rgbCritical, response)
      || !parsePercentParam(req, "rgbNormal", rgbNormal, response))
  {
    return response;
  }

  if (!mode && !monoCritical && !monoNormal && !rgbCritical && !rgbNormal) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_MISSING_PARAM);
  }

  bool saved = VisualStateManager::UpdateLedConfig([&](Config::LedConfig& cfg) {
    bool changed = false;
    auto apply   = [&changed](auto& field, const auto& value) {
      if (value && field != *value) {
        field   = *value;
        changed = true;
      }
    };
    apply(cfg.mode, mode);
    apply(cfg.monoCriticalBrightness, monoCritical);
    apply(cfg.monoNormalBrightness, monoNormal);
    apply(cfg.rgbCriticalBrightness, rgbCritical);
    apply(cfg.rgbNormalBrightness, rgbNormal);
    return changed;
  });
  if (!saved) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  return sendResp(req, S200, nullptr, {});
}

static std::optional<VisualStateManager::LedPattern> parseLedPattern(std::string_view str)
{
  using VisualStateManager::LedPattern;
  struct Entry {
    std::string_view name;
    LedPattern pattern;
  };
  static constexpr Entry kPatterns[] = {
    {"disconnected", LedPattern::WiFiDisconnected},
    {"scanning", LedPattern::WiFiScanning},
    {"nobackend", LedPattern::NoBackend},
    {"connected", LedPattern::Connected},
    {"estop", LedPattern::EStopped},
    {"estopclearing", LedPattern::EStopClearing},
    {"estoprelease", LedPattern::EStopAwaitingRelease},
    {"error", LedPattern::CriticalError},
  };
  for (const Entry& entry : kPatterns) {
    if (StringIEquals(str, entry.name)) {
      return entry.pattern;
    }
  }
  return std::nullopt;
}

static std::optional<uint8_t> parseHexByte(std::string_view str)
{
  uint8_t value = 0;
  for (char c : str) {
    uint8_t digit;
    if (c >= '0' && c <= '9') {
      digit = c - '0';
    } else if (c >= 'a' && c <= 'f') {
      digit = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
      digit = c - 'A' + 10;
    } else {
      return std::nullopt;
    }
    value = static_cast<uint8_t>(value * 16 + digit);
  }
  return value;
}

// POST /api/led/preview?pattern=<name>
// POST /api/led/preview?color=RRGGBB[&led=mono|rgb][&brightness=0-100]
// Takes over the LEDs for VisualStateManager::kPreviewDurationMs after the last call; nothing is saved.
static esp_err_t apiLedPreview(httpd_req_t* req)
{
  auto patternStr = getQueryParam(req, "pattern");
  if (!patternStr && patternStr.error() != ParamError::Missing) {
    return sendParamError(req, patternStr.error());
  }
  if (patternStr) {
    auto pattern = parseLedPattern(*patternStr);
    if (!pattern) {
      return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PARAM);
    }
    VisualStateManager::PreviewPattern(*pattern);
    return sendResp(req, S200, nullptr, {});
  }

  auto colorStr = getQueryParam(req, "color");
  if (!colorStr) {
    return sendParamError(req, colorStr.error());
  }
  std::string_view color = *colorStr;
  if (color.size() != 6) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PARAM);
  }
  auto red   = parseHexByte(color.substr(0, 2));
  auto green = parseHexByte(color.substr(2, 2));
  auto blue  = parseHexByte(color.substr(4, 2));
  if (!red || !green || !blue) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PARAM);
  }

  std::optional<LedType> only;
  auto ledStr = getQueryParam(req, "led");
  if (!ledStr && ledStr.error() != ParamError::Missing) {
    return sendParamError(req, ledStr.error());
  }
  if (ledStr) {
    only = TryParseLedType(*ledStr);
    if (!only) {
      return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PARAM);
    }
  }

  std::optional<uint8_t> brightness;
  esp_err_t response = ESP_OK;
  if (!parsePercentParam(req, "brightness", brightness, response)) {
    return response;
  }

  VisualStateManager::PreviewColor(*red, *green, *blue, only, brightness);
  return sendResp(req, S200, nullptr, {});
}

// DELETE /api/led/preview: ends a preview early
static esp_err_t apiLedPreviewStop(httpd_req_t* req)
{
  VisualStateManager::StopPreview();
  return sendResp(req, S200, nullptr, {});
}

void CaptivePortal::RegisterApiHandlers(httpd_handle_t server, CaptivePortalInstance* instance)
{
  registerHandler(server, "/api/board", HTTP_GET, apiBoard, instance);
  registerHandler(server, "/api/portal/close", HTTP_POST, apiPortalClose, instance);
  registerHandler(server, "/api/wifi/scan", HTTP_POST, apiWifiScan, instance);
  registerHandler(server, "/api/wifi/networks", HTTP_DELETE, apiWifiNetworksDelete, instance);
  registerHandler(server, "/api/wifi/networks", HTTP_POST, apiWifiNetworksAdd, instance);
  registerHandler(server, "/api/wifi/connect", HTTP_POST, apiWifiConnect, instance);
  registerHandler(server, "/api/wifi/disconnect", HTTP_POST, apiWifiDisconnect, instance);
  registerHandler(server, "/api/account/link", HTTP_POST, apiAccountLink, instance);
  registerHandler(server, "/api/account", HTTP_DELETE, apiAccountDelete, instance);
  registerHandler(server, "/api/config/rf/pin", HTTP_PUT, apiConfigRfPin, instance);
  // No /api/config/estop/*: the portal is an open, unauthenticated AP, so the E-Stop is configured over serial only.
  // No /api/ota/domain: the portal is an open, unauthenticated AP, and OTA trusts the hashes served by that domain, so
  // changing it would let anyone in range install firmware. It can only be changed over serial (jsonconfig).
  registerHandler(server, "/api/ota/settings", HTTP_PUT, apiOtaSettings, instance);
  registerHandler(server, "/api/ota/check", HTTP_POST, apiOtaCheck, instance);
  registerHandler(server, "/api/led", HTTP_GET, apiLedGet, instance);
  registerHandler(server, "/api/led", HTTP_PUT, apiLedPut, instance);
  registerHandler(server, "/api/led/preview", HTTP_POST, apiLedPreview, instance);
  registerHandler(server, "/api/led/preview", HTTP_DELETE, apiLedPreviewStop, instance);
}
