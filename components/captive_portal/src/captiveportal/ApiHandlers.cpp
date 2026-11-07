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
#include "GatewayConnectionManager.h"
#include "http/ContentTypes.h"
#include "Logging.h"
#include "OtaUpdateConfig.h"
#include "RateLimiter.h"
#include "util/TaskUtils.h"
#include "wifi/AccessPoint.h"
#include "wifi/WiFiConfig.h"
#include "wifi/WiFiConstants.h"
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
static const char* const JSON_ERR_INVALID_AP_SSID = "{\"error\":\"InvalidApSsid\"}";
static const char* const JSON_ERR_INVALID_AP_PASS = "{\"error\":\"InvalidApPassword\"}";

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
  if (!ssid || ssid->empty() || ssid->length() > WiFiConstants::kSsidMaxLength) {
    return sendResp(req, S400, HTTP::ContentType::JSON, !ssid || ssid->empty() ? JSON_ERR_MISSING_SSID : JSON_ERR_INVALID_SSID);
  }

  // Missing means an open network; an undecodable one is rejected rather than saved as a different password.
  auto passwordParam = getFormParam(*body, "password");
  if (!passwordParam && passwordParam.error() == ParamError::Invalid) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PARAM);
  }
  std::string password = std::move(passwordParam).value_or(std::string());
  if (!password.empty()) {
    if (password.length() < WiFiConstants::kPassphraseMinLength) {
      return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_PASSWORD_SHORT);
    }
    if (password.length() > WiFiConstants::kPassphraseMaxLength) {
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

// PUT /api/ap, form body: any of
//   ssid=<name>                      the access point's name; empty for the default (the prefix with the hub's MAC)
//   protection=default|open|custom   the build's default password, none, or `password`
//   password=<8-63 printable ASCII>  with protection=custom
// Applied the next time the captive portal starts: applying it now would drop the client that asked, which is most
// likely on this access point.
//
// Answers {"ssid": <name, "" for the default>, "protection": "default"|"open"|"custom"} as the hub reads the saved
// settings, since a name equal to the bare prefix or a password equal to the build's default are the defaults.
static esp_err_t sendAccessPointSettings(httpd_req_t* req)
{
  std::string ssid     = Config::GetWiFiAccessPointSSID();
  std::string password = Config::GetWiFiAccessPointPassword();

  const char* protection = "custom";
  switch (AccessPoint::ProtectionOf(password)) {
    case AccessPoint::Protection::Open:
      protection = "open";
      break;
    case AccessPoint::Protection::DefaultPassword:
      protection = "default";
      break;
    case AccessPoint::Protection::CustomPassword:
      break;
  }

  OpenShock::JSON::StringWriter writer;
  json_gen_str_t* gen = writer.gen();
  json_gen_start_object(gen);
  OpenShock::JSON::objSetString(gen, "ssid", AccessPoint::IsDefaultSSID(ssid) ? std::string_view() : std::string_view(ssid));
  OpenShock::JSON::objSetString(gen, "protection", protection);
  json_gen_end_object(gen);
  std::string json = writer.finish();
  return sendResp(req, S200, HTTP::ContentType::JSON, json);
}

static esp_err_t apiAccessPoint(httpd_req_t* req)
{
  auto body = recvBody(req);
  if (!body) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_MISSING_PARAM);
  }

  bool resetSsid = false;
  std::optional<std::string> ssid;
  auto ssidParam = getFormParam(*body, "ssid");
  if (!ssidParam && ssidParam.error() != ParamError::Missing) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_AP_SSID);
  }
  if (ssidParam) {
    if (ssidParam->empty()) {
      resetSsid = true;
    } else if (!AccessPoint::IsValidSSID(*ssidParam)) {
      return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_AP_SSID);
    } else {
      ssid = std::move(*ssidParam);
    }
  }

  bool resetPassword = false;
  std::optional<std::string> password;
  auto protection = getFormParam(*body, "protection");
  if (!protection && protection.error() != ParamError::Missing) {
    return sendParamError(req, protection.error());
  }
  if (protection) {
    if (*protection == "default") {
      resetPassword = true;
    } else if (*protection == "open") {
      password = std::string();
    } else if (*protection == "custom") {
      auto passwordParam = getFormParam(*body, "password");
      if (!passwordParam) {
        return sendResp(req, S400, HTTP::ContentType::JSON, passwordParam.error() == ParamError::Missing ? JSON_ERR_PASSWORD_SHORT : JSON_ERR_INVALID_AP_PASS);
      }
      if (passwordParam->length() < WiFiConstants::kPassphraseMinLength) {
        return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_PASSWORD_SHORT);
      }
      if (passwordParam->length() > WiFiConstants::kPassphraseMaxLength) {
        return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_PASSWORD_LONG);
      }
      if (!AccessPoint::IsValidPassword(*passwordParam)) {
        return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_AP_PASS);
      }
      password = std::move(*passwordParam);
    } else {
      return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_INVALID_PARAM);
    }
  }

  if (!resetSsid && !ssid && !resetPassword && !password) {
    return sendResp(req, S400, HTTP::ContentType::JSON, JSON_ERR_MISSING_PARAM);
  }

  // Both passed the checks above, so a refusal here is the config store failing to save
  bool saved = true;
  if (resetSsid) {
    saved = Config::ResetWiFiAccessPointSSID();
  } else if (ssid) {
    saved = Config::SetWiFiAccessPointSSID(std::move(*ssid));
  }
  if (!saved) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  if (resetPassword) {
    saved = Config::ResetWiFiAccessPointPassword();
  } else if (password) {
    saved = Config::SetWiFiAccessPointPassword(std::move(*password));
  }
  if (!saved) {
    return sendResp(req, S500, HTTP::ContentType::JSON, JSON_ERR_INTERNAL);
  }
  return sendAccessPointSettings(req);
}

void CaptivePortal::RegisterApiHandlers(httpd_handle_t server, CaptivePortalInstance* instance)
{
  registerHandler<apiBoard>(server, "/api/board", HTTP_GET, instance);
  registerHandler<apiPortalClose>(server, "/api/portal/close", HTTP_POST, instance);
  registerHandler<apiWifiScan>(server, "/api/wifi/scan", HTTP_POST, instance);
  registerHandler<apiWifiNetworksDelete>(server, "/api/wifi/networks", HTTP_DELETE, instance);
  registerHandler<apiWifiNetworksAdd>(server, "/api/wifi/networks", HTTP_POST, instance);
  registerHandler<apiWifiConnect>(server, "/api/wifi/connect", HTTP_POST, instance);
  registerHandler<apiWifiDisconnect>(server, "/api/wifi/disconnect", HTTP_POST, instance);
  registerHandler<apiAccountLink>(server, "/api/account/link", HTTP_POST, instance);
  registerHandler<apiAccountDelete>(server, "/api/account", HTTP_DELETE, instance);
  registerHandler<apiConfigRfPin>(server, "/api/config/rf/pin", HTTP_PUT, instance);
  // No /api/config/estop/*: the portal is an open, unauthenticated AP, so the E-Stop is configured over serial only.
  // No /api/ota/domain: the portal is an open, unauthenticated AP, and OTA trusts the hashes served by that domain, so
  // changing it would let anyone in range install firmware. It can only be changed over serial (jsonconfig).
  registerHandler<apiOtaSettings>(server, "/api/ota/settings", HTTP_PUT, instance);
  registerHandler<apiOtaCheck>(server, "/api/ota/check", HTTP_POST, instance);
  registerHandler<apiAccessPoint>(server, "/api/ap", HTTP_PUT, instance);
}
