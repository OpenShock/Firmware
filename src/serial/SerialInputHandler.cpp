#include "serial/SerialInputHandler.h"

const char* const TAG = "SerialInputHandler";

#include "Chipset.h"
#include "CommandHandler.h"
#include "config/Config.h"
#include "config/SerialInputConfig.h"
#include "FormatHelpers.h"
#include "http/HTTPRequestManager.h"
#include "Convert.h"
#include "Logging.h"
#include "serialization/JsonAPI.h"
#include "serialization/JsonSerial.h"
#include "Time.h"
#include "util/Base64Utils.h"
#include "util/StringUtils.h"
#include "wifi/WiFiManager.h"

#include <cJSON.h>
#include <Esp.h>

#include <cstring>
#include <string_view>
#include <unordered_map>

namespace std {
  struct hash_ci {
    std::size_t operator()(std::string_view str) const {
      std::size_t hash = 7;

      for (int i = 0; i < str.size(); ++i) {
        hash = hash * 31 + tolower(str[i]);
      }

      return hash;
    }
  };

  template<>
  struct less<std::string_view> {
    bool operator()(std::string_view a, std::string_view b) const { return a < b; }
  };

  struct equals_ci {
    bool operator()(std::string_view a, std::string_view b) const { return strncasecmp(a.data(), b.data(), std::max(a.size(), b.size())) == 0; }
  };
}  // namespace std

using namespace std::string_view_literals;

#define SERPR_SYS(format, ...)      Serial.printf("$SYS$|" format "\n", ##__VA_ARGS__)
#define SERPR_RESPONSE(format, ...) SERPR_SYS("Response|" format, ##__VA_ARGS__)
#define SERPR_SUCCESS(format, ...)  SERPR_SYS("Success|" format, ##__VA_ARGS__)
#define SERPR_ERROR(format, ...)    SERPR_SYS("Error|" format, ##__VA_ARGS__)

using namespace OpenShock;

const int64_t PASTE_INTERVAL_THRESHOLD_MS       = 20;
const std::size_t SERIAL_BUFFER_CLEAR_THRESHOLD = 512;

struct SerialCmdHandler {
  std::string_view cmd;
  const char* helpResponse;
  void (*commandHandler)(std::string_view, bool);
};

static bool s_echoEnabled = true;
static std::unordered_map<std::string_view, SerialCmdHandler, std::hash_ci, std::equals_ci> s_commandHandlers;

/// @brief Tries to parse a boolean from a string (case-insensitive)
/// @param str Input string
/// @param strLen Length of input string
/// @param out Output boolean
/// @return True if the argument is a boolean, false otherwise
bool _tryParseBool(std::string_view str, bool& out) {
  return OpenShock::Convert::ToBool(OpenShock::StringTrim(str), out);
}

void _handleVersionCommand(std::string_view arg, bool isAutomated) {
  (void)arg;

  Serial.print("\n");
  SerialInputHandler::PrintVersionInfo();
}

void _handleRestartCommand(std::string_view arg, bool isAutomated) {
  (void)arg;

  Serial.println("Restarting ESP...");
  ESP.restart();
}

void _handleFactoryResetCommand(std::string_view arg, bool isAutomated) {
  (void)arg;

  Serial.println("Resetting to factory defaults...");
  Config::FactoryReset();
  Serial.println("Restarting...");
  ESP.restart();
}

void _handleRfTxPinCommand(std::string_view arg, bool isAutomated) {
  if (arg.empty()) {
    uint8_t txPin;
    if (!Config::GetRFConfigTxPin(txPin)) {
      SERPR_ERROR("Failed to get RF TX pin from config");
      return;
    }

    // Get rmt pin
    SERPR_RESPONSE("RmtPin|%u", txPin);
    return;
  }

  uint8_t pin;
  if (!OpenShock::Convert::ToUint8(arg, pin)) {
    SERPR_ERROR("Invalid argument (number invalid or out of range)");
  }

  OpenShock::SetRfPinResultCode result = OpenShock::CommandHandler::SetRfTxPin(pin);

  switch (result) {
    case OpenShock::SetRfPinResultCode::InvalidPin:
      SERPR_ERROR("Invalid argument (invalid pin)");
      break;

    case OpenShock::SetRfPinResultCode::InternalError:
      SERPR_ERROR("Internal error while setting RF TX pin");
      break;

    case OpenShock::SetRfPinResultCode::Success:
      SERPR_SUCCESS("Saved config");
      break;

    default:
      SERPR_ERROR("Unknown error while setting RF TX pin");
      break;
  }
}

void _handleDomainCommand(std::string_view arg, bool isAutomated) {
  if (arg.empty()) {
    std::string domain;
    if (!Config::GetBackendDomain(domain)) {
      SERPR_ERROR("Failed to get domain from config");
      return;
    }

    // Get domain
    SERPR_RESPONSE("Domain|%s", domain.c_str());
    return;
  }

  // Check if the domain is too long
  // TODO: Remove magic number
  if (arg.length() + 40 >= OPENSHOCK_URI_BUFFER_SIZE) {
    SERPR_ERROR("Domain name too long, please try increasing the \"OPENSHOCK_URI_BUFFER_SIZE\" constant in source code");
    return;
  }

  char uri[OPENSHOCK_URI_BUFFER_SIZE];
  sprintf(uri, "https://%.*s/1", arg.length(), arg.data());

  auto resp = HTTP::GetJSON<Serialization::JsonAPI::BackendVersionResponse>(
    uri,
    {
      {"Accept", "application/json"}
  },
    Serialization::JsonAPI::ParseBackendVersionJsonResponse,
    {200}
  );

  if (resp.result != HTTP::RequestResult::Success) {
    SERPR_ERROR("Tried to connect to \"%.*s\", but failed with status [%d], refusing to save domain to config", arg.length(), arg.data(), resp.code);
    return;
  }

  OS_LOGI(TAG, "Successfully connected to \"%.*s\", version: %s, commit: %s, current time: %s", arg.length(), arg.data(), resp.data.version.c_str(), resp.data.commit.c_str(), resp.data.currentTime.c_str());

  bool result = OpenShock::Config::SetBackendDomain(arg);

  if (!result) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config, restarting...");

  // Restart to use the new domain
  ESP.restart();
}

void _handleAuthtokenCommand(std::string_view arg, bool isAutomated) {
  if (arg.empty()) {
    std::string authToken;
    if (!Config::GetBackendAuthToken(authToken)) {
      SERPR_ERROR("Failed to get auth token from config");
      return;
    }

    // Get auth token
    SERPR_RESPONSE("AuthToken|%s", authToken.c_str());
    return;
  }

  bool result = OpenShock::Config::SetBackendAuthToken(arg);

  if (result) {
    SERPR_SUCCESS("Saved config");
  } else {
    SERPR_ERROR("Failed to save config");
  }
}

void _handleLcgOverrideCommand(std::string_view arg, bool isAutomated) {
  if (arg.empty()) {
    std::string lcgOverride;
    if (!Config::GetBackendLCGOverride(lcgOverride)) {
      SERPR_ERROR("Failed to get LCG override from config");
      return;
    }

    // Get LCG override
    SERPR_RESPONSE("LcgOverride|%s", lcgOverride.c_str());
    return;
  }

  if (OpenShock::StringStartsWith(arg, "clear"sv)) {
    if (arg.size() != 5) {
      SERPR_ERROR("Invalid command (clear command should not have any arguments)");
      return;
    }

    bool result = OpenShock::Config::SetBackendLCGOverride(std::string());
    if (result) {
      SERPR_SUCCESS("Cleared LCG override");
    } else {
      SERPR_ERROR("Failed to clear LCG override");
    }
    return;
  }

  if (OpenShock::StringStartsWith(arg, "set "sv)) {
    if (arg.size() <= 4) {
      SERPR_ERROR("Invalid command (set command should have an argument)");
      return;
    }

    std::string_view domain = arg.substr(4);

    if (domain.size() + 40 >= OPENSHOCK_URI_BUFFER_SIZE) {
      SERPR_ERROR("Domain name too long, please try increasing the \"OPENSHOCK_URI_BUFFER_SIZE\" constant in source code");
      return;
    }

    char uri[OPENSHOCK_URI_BUFFER_SIZE];
    sprintf(uri, "https://%.*s/1", static_cast<int>(domain.size()), domain.data());

    auto resp = HTTP::GetJSON<Serialization::JsonAPI::LcgInstanceDetailsResponse>(
      uri,
      {
        {"Accept", "application/json"}
    },
      Serialization::JsonAPI::ParseLcgInstanceDetailsJsonResponse,
      {200}
    );

    if (resp.result != HTTP::RequestResult::Success) {
      SERPR_ERROR("Tried to connect to \"%.*s\", but failed with status [%d], refusing to save domain to config", domain.size(), domain.data(), resp.code);
      return;
    }

    OS_LOGI(
      TAG,
      "Successfully connected to \"%.*s\", name: %s, version: %s, current time: %s, country code: %s, FQDN: %s",
      domain.size(),
      domain.data(),
      resp.data.name.c_str(),
      resp.data.version.c_str(),
      resp.data.currentTime.c_str(),
      resp.data.countryCode.c_str(),
      resp.data.fqdn.c_str()
    );

    bool result = OpenShock::Config::SetBackendLCGOverride(domain);

    if (result) {
      SERPR_SUCCESS("Saved config");
    } else {
      SERPR_ERROR("Failed to save config");
    }
    return;
  }

  SERPR_ERROR("Invalid subcommand");
}

void _handleHostnameCommand(std::string_view arg, bool isAutomated) {
  if (arg.empty()) {
    std::string hostname;
    if (!Config::GetWiFiHostname(hostname)) {
      SERPR_ERROR("Failed to get hostname from config");
      return;
    }

    // Get hostname
    SERPR_RESPONSE("Hostname|%s", hostname.c_str());
    return;
  }

  bool result = OpenShock::Config::SetWiFiHostname(arg);

  if (result) {
    SERPR_SUCCESS("Saved config, restarting...");

    ESP.restart();
  } else {
    SERPR_ERROR("Failed to save config");
  }
}

void _handleNetworksCommand(std::string_view arg, bool isAutomated) {
  cJSON* root;

  if (arg.empty()) {
    root = cJSON_CreateArray();
    if (root == nullptr) {
      SERPR_ERROR("Failed to create JSON array");
      return;
    }

    if (!Config::GetWiFiCredentials(root, true)) {
      SERPR_ERROR("Failed to get WiFi credentials from config");
      return;
    }

    char* out = cJSON_PrintUnformatted(root);
    if (out == nullptr) {
      SERPR_ERROR("Failed to print JSON");
      return;
    }

    SERPR_RESPONSE("Networks|%s", out);

    cJSON_free(out);
    return;
  }

  root = cJSON_ParseWithLength(arg.data(), arg.length());
  if (root == nullptr) {
    SERPR_ERROR("Failed to parse JSON: %s", cJSON_GetErrorPtr());
    return;
  }

  if (cJSON_IsArray(root) == 0) {
    SERPR_ERROR("Invalid argument (not an array)");
    return;
  }

  std::vector<Config::WiFiCredentials> creds;

  uint8_t id     = 1;
  cJSON* network = nullptr;
  cJSON_ArrayForEach(network, root) {
    Config::WiFiCredentials cred;

    if (!cred.FromJSON(network)) {
      SERPR_ERROR("Failed to parse network");
      return;
    }

    if (cred.id == 0) {
      cred.id = id++;
    }

    OS_LOGI(TAG, "Adding network \"%s\" to config, id=%u", cred.ssid.c_str(), cred.id);

    creds.emplace_back(std::move(cred));
  }

  if (!OpenShock::Config::SetWiFiCredentials(creds)) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config");

  OpenShock::WiFiManager::RefreshNetworkCredentials();
}

void _handleKeepAliveCommand(std::string_view arg, bool isAutomated) {
  bool keepAliveEnabled;

  if (arg.empty()) {
    // Get keep alive status
    if (!Config::GetRFConfigKeepAliveEnabled(keepAliveEnabled)) {
      SERPR_ERROR("Failed to get keep-alive status from config");
      return;
    }

    SERPR_RESPONSE("KeepAlive|%s", keepAliveEnabled ? "true" : "false");
    return;
  }

  if (!_tryParseBool(arg, keepAliveEnabled)) {
    SERPR_ERROR("Invalid argument (not a boolean)");
    return;
  }

  bool result = OpenShock::CommandHandler::SetKeepAliveEnabled(keepAliveEnabled);

  if (result) {
    SERPR_SUCCESS("Saved config");
  } else {
    SERPR_ERROR("Failed to save config");
  }
}

void _handleSerialEchoCommand(std::string_view arg, bool isAutomated) {
  if (arg.empty()) {
    // Get current serial echo status
    SERPR_RESPONSE("SerialEcho|%s", s_echoEnabled ? "true" : "false");
    return;
  }

  bool enabled;
  if (!_tryParseBool(arg, enabled)) {
    SERPR_ERROR("Invalid argument (not a boolean)");
    return;
  }

  bool result   = Config::SetSerialInputConfigEchoEnabled(enabled);
  s_echoEnabled = enabled;

  if (result) {
    SERPR_SUCCESS("Saved config");
  } else {
    SERPR_ERROR("Failed to save config");
  }
}

void _handleValidGpiosCommand(std::string_view arg, bool isAutomated) {
  if (!arg.empty()) {
    SERPR_ERROR("Invalid argument (too many arguments)");
    return;
  }

  auto pins = OpenShock::GetValidGPIOPins();

  std::string buffer;
  buffer.reserve(pins.count() * 4);

  for (std::size_t i = 0; i < pins.size(); i++) {
    if (pins[i]) {
      buffer.append(std::to_string(i));
      buffer.append(",");
    }
  }

  if (!buffer.empty()) {
    buffer.pop_back();
  }

  SERPR_RESPONSE("ValidGPIOs|%s", buffer.c_str());
}

void _handleJsonConfigCommand(std::string_view arg, bool isAutomated) {
  if (arg.empty()) {
    // Get raw config
    std::string json = Config::GetAsJSON(true);

    SERPR_RESPONSE("JsonConfig|%s", json.c_str());
    return;
  }

  if (!Config::SaveFromJSON(arg)) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config, restarting...");

  ESP.restart();
}

void _handleRawConfigCommand(std::string_view arg, bool isAutomated) {
  if (arg.empty()) {
    std::vector<uint8_t> buffer;

    // Get raw config
    if (!Config::GetRaw(buffer)) {
      SERPR_ERROR("Failed to get raw config");
      return;
    }

    std::string base64;
    if (!OpenShock::Base64Utils::Encode(buffer.data(), buffer.size(), base64)) {
      SERPR_ERROR("Failed to encode raw config to base64");
      return;
    }

    SERPR_RESPONSE("RawConfig|%s", base64.c_str());
    return;
  }

  std::vector<uint8_t> buffer;
  if (!OpenShock::Base64Utils::Decode(arg.data(), arg.length(), buffer)) {
    SERPR_ERROR("Failed to decode base64");
    return;
  }

  if (!Config::SetRaw(buffer.data(), buffer.size())) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config, restarting...");

  ESP.restart();
}

void _handleDebugInfoCommand(std::string_view arg, bool isAutomated) {
  (void)arg;

  SERPR_RESPONSE("RTOSInfo|Free Heap|%u", xPortGetFreeHeapSize());
  SERPR_RESPONSE("RTOSInfo|Min Free Heap|%u", xPortGetMinimumEverFreeHeapSize());

  const int64_t now = OpenShock::millis();
  SERPR_RESPONSE("RTOSInfo|UptimeMS|%lli", now);

  const int64_t seconds = now / 1000;
  const int64_t minutes = seconds / 60;
  const int64_t hours   = minutes / 60;
  const int64_t days    = hours / 24;
  SERPR_RESPONSE("RTOSInfo|Uptime|%llid %llih %llim %llis", days, hours % 24, minutes % 60, seconds % 60);

  OpenShock::WiFiNetwork network;
  bool connected = OpenShock::WiFiManager::GetConnectedNetwork(network);
  SERPR_RESPONSE("WiFiInfo|Connected|%s", connected ? "true" : "false");
  if (connected) {
    SERPR_RESPONSE("WiFiInfo|SSID|%s", network.ssid);
    SERPR_RESPONSE("WiFiInfo|BSSID|" BSSID_FMT, BSSID_ARG(network.bssid));

    char ipAddressBuffer[64];
    OpenShock::WiFiManager::GetIPAddress(ipAddressBuffer);
    SERPR_RESPONSE("WiFiInfo|IPv4|%s", ipAddressBuffer);
    OpenShock::WiFiManager::GetIPv6Address(ipAddressBuffer);
    SERPR_RESPONSE("WiFiInfo|IPv6|%s", ipAddressBuffer);
  }
}

void _handleRFTransmitCommand(std::string_view arg, bool isAutomated) {
  if (arg.empty()) {
    SERPR_ERROR("No command");
    return;
  }
  cJSON* root = cJSON_ParseWithLength(arg.data(), arg.length());
  if (root == nullptr) {
    SERPR_ERROR("Failed to parse JSON: %s", cJSON_GetErrorPtr());
    return;
  }

  OpenShock::Serialization::JsonSerial::ShockerCommand cmd;
  bool parsed = Serialization::JsonSerial::ParseShockerCommand(root, cmd);

  cJSON_Delete(root);

  if (!parsed) {
    SERPR_ERROR("Failed to parse shocker command");
    return;
  }

  if (!OpenShock::CommandHandler::HandleCommand(cmd.model, cmd.id, cmd.command, cmd.intensity, cmd.durationMs)) {
    SERPR_ERROR("Failed to send command");
    return;
  }

  SERPR_SUCCESS("Command sent");
}

void _handleHelpCommand(std::string_view arg, bool isAutomated) {
  arg = OpenShock::StringTrim(arg);
  if (arg.empty()) {
    SerialInputHandler::PrintWelcomeHeader();

    // Raw string literal (1+ to remove the first newline)
    Serial.print(1 + R"(
help                    print this menu
help         <command>  print help for a command
version                 print version information
restart                 restart the board
sysinfo                 print debug information for various subsystems
echo                    get serial echo enabled
echo         <bool>     set serial echo enabled
validgpios              list all valid GPIO pins
rftxpin                 get radio transmit pin
rftxpin      <pin>      set radio transmit pin
domain                  get backend domain
domain       <domain>   set backend domain
authtoken               get auth token
authtoken    <token>    set auth token
hostname                get network hostname
hostname     <hostname> set network hostname
networks                get all saved networks
networks     <json>     set all saved networks
keepalive               get shocker keep-alive enabled
keepalive    <bool>     set shocker keep-alive enabled
jsonconfig              get configuration as JSON
jsonconfig   <json>     set configuration from JSON
rawconfig               get raw configuration as base64
rawconfig    <base64>   set raw configuration from base64
rftransmit   <json>     transmit a RF command
factoryreset            reset device to factory defaults and restart
)");
    return;
  }

  // Get help for a specific command
  auto it = s_commandHandlers.find(arg);
  if (it != s_commandHandlers.end()) {
    Serial.print(it->second.helpResponse);
    return;
  }

  SERPR_ERROR("Command \"%.*s\" not found", arg.length(), arg.data());
}

static const SerialCmdHandler kVersionCmdHandler = {
  "version"sv,
  R"(version
  Print version information
  Example:
    version
)",
  _handleVersionCommand,
};
static const SerialCmdHandler kRestartCmdHandler = {
  "restart"sv,
  R"(restart
  Restart the board
  Example:
    restart
)",
  _handleRestartCommand,
};
static const SerialCmdHandler kSystemInfoCmdHandler = {
  "sysinfo"sv,
  R"(sysinfo
  Get system information from RTOS, WiFi, etc.
  Example:
    sysinfo
)",
  _handleDebugInfoCommand,
};
static const SerialCmdHandler kSerialEchoCmdHandler = {
  "echo"sv,
  R"(echo
  Get the serial echo status.
  If enabled, typed characters are echoed back to the serial port.

echo [<bool>]
  Enable/disable serial echo.
  Arguments:
    <bool> must be a boolean.
  Example:
    echo true
)",
  _handleSerialEchoCommand,
};
static const SerialCmdHandler kValidGpiosCmdHandler = {
  "validgpios"sv,
  R"(validgpios
  List all valid GPIO pins
  Example:
    validgpios
)",
  _handleValidGpiosCommand,
};
static const SerialCmdHandler kRfTxPinCmdHandler = {
  "rftxpin"sv,
  R"(rftxpin
  Get the GPIO pin used for the radio transmitter.

rftxpin [<pin>]
  Set the GPIO pin used for the radio transmitter.
  Arguments:
    <pin> must be a number.
  Example:
    rftxpin 15
)",
  _handleRfTxPinCommand,
};
static const SerialCmdHandler kDomainCmdHandler = {
  "domain"sv,
  R"(domain
  Get the backend domain.

domain [<domain>]
  Set the backend domain.
  Arguments:
    <domain> must be a string.
  Example:
    domain api.openshock.app
)",
  _handleDomainCommand,
};
static const SerialCmdHandler kAuthTokenCmdHandler = {
  "authtoken"sv,
  R"(authtoken
  Get the backend auth token.

authtoken [<token>]
  Set the auth token.
  Arguments:
    <token> must be a string.
  Example:
    authtoken mytoken
)",
  _handleAuthtokenCommand,
};
static const SerialCmdHandler kLcgOverrideCmdHandler = {
  "lcgoverride",
  R"(lcgoverride
  Get the domain overridden for LCG endpoint (if any).

lcgoverride set <domain>
  Set a domain to override the LCG endpoint.
  Arguments:
    <domain> must be a string.
  Example:
    lcgoverride set eu1-gateway.openshock.app

lcgoverride clear
  Clear the overridden LCG endpoint.
  Example:
    lcgoverride clear
)",
  _handleLcgOverrideCommand,
};
static const SerialCmdHandler kHostnameCmdHandler = {
  "hostname"sv,
  R"(hostname
  Get the network hostname.

hostname [<hostname>]
  Set the network hostname.
  Arguments:
    <hostname> must be a string.
  Example:
    hostname OpenShock
)",
  _handleHostnameCommand,
};
static const SerialCmdHandler kNetworksCmdHandler = {
  "networks"sv,
  R"(networks
  Get all saved networks.

networks [<json>]
  Set all saved networks.
  Arguments:
    <json> must be a array of objects with the following fields:
      ssid     (string)  SSID of the network
      password (string)  Password of the network
      id       (number)  ID of the network (optional)
  Example:
    networks [{\"ssid\":\"myssid\",\"password\":\"mypassword\"}]
)",
  _handleNetworksCommand,
};
static const SerialCmdHandler kKeepAliveCmdHandler = {
  "keepalive"sv,
  R"(keepalive
  Get the shocker keep-alive status.

keepalive [<bool>]
  Enable/disable shocker keep-alive.
  Arguments:
    <bool> must be a boolean.
  Example:
    keepalive true
)",
  _handleKeepAliveCommand,
};
static const SerialCmdHandler kJsonConfigCmdHandler = {
  "jsonconfig"sv,
  R"(jsonconfig
  Get the configuration as JSON
  Example:
    jsonconfig

jsonconfig <json>
  Set the configuration from JSON, and restart
  Arguments:
    <json> must be a valid JSON object
  Example:
    jsonconfig { ... }
)",
  _handleJsonConfigCommand,
};
static const SerialCmdHandler kRawConfigCmdHandler = {
  "rawconfig"sv,
  R"(rawconfig
  Get the raw binary config
  Example:
    rawconfig

rawconfig <base64>
  Set the raw binary config, and restart
  Arguments:
    <base64> must be a base64 encoded string
  Example:
    rawconfig (base64 encoded binary data)
)",
  _handleRawConfigCommand,
};
static const SerialCmdHandler kRfTransmitCmdHandler = {
  "rftransmit"sv,
  R"(rftransmit <json>
  Transmit a RF command
  Arguments:
    <json> must be a JSON object with the following fields:
      model      (string) Model of the shocker                    ("caixianlin", "petrainer", "petrainer998dr")
      id         (number) ID of the shocker                       (0-65535)
      type       (string) Type of the command                     ("shock", "vibrate", "sound", "stop")
      intensity  (number) Intensity of the command                (0-255)
      durationMs (number) Duration of the command in milliseconds (0-65535)
  Example:
    rftransmit {"model":"caixianlin","id":12345,"type":"vibrate","intensity":99,"durationMs":500}
)",
  _handleRFTransmitCommand,
};
static const SerialCmdHandler kFactoryResetCmdHandler = {
  "factoryreset"sv,
  R"(factoryreset
  Reset the device to factory defaults and restart
  Example:
    factoryreset
)",
  _handleFactoryResetCommand,
};
static const SerialCmdHandler khelpCmdHandler = {
  "help"sv,
  R"(help [<command>]
  Print help information
  Arguments:
    <command> (optional) command to print help for
  Example:
    help
)",
  _handleHelpCommand,
};

void RegisterCommandHandler(const SerialCmdHandler& handler) {
  s_commandHandlers[handler.cmd] = handler;
}

#define CLEAR_LINE "\r\x1B[K"

enum class SerialReadResult {
  NoData,
  Data,
  LineEnd,
  AutoCompleteRequest,
};

SerialReadResult _tryReadSerialLine(std::string& buffer) {
  // Check if there's any data available
  int available  = ::Serial.available();
  if (available <= 0) {
    return SerialReadResult::NoData;
  }

  // Reserve space for the new data
  buffer.reserve(buffer.size() + available);

  // Read the data into the buffer
  while (available-- > 0) {
    char c = ::Serial.read();

    // Handle backspace
    if (c == '\b') {
      if (!buffer.empty()) {
        buffer.pop_back();
      }
      continue;
    }

    // Handle newline
    if (c == '\r' || c == '\n') {
      if (!buffer.empty()) {
        return SerialReadResult::LineEnd;
      }
      continue;
    }

    // Handle leading whitespace
    if (c == ' ' && buffer.empty()) {
      continue;
    }

    if (c == '\t') {
      return SerialReadResult::AutoCompleteRequest;
    }

    // Add the character to the buffer
    buffer.push_back(c);
  }

  return SerialReadResult::Data;
}

void _skipSerialWhitespaces(std::string& buffer) {
  int available = ::Serial.available();

  while (available-- > 0) {
    char c = ::Serial.read();

    if (c != ' ' && c != '\r' && c != '\n') {
      buffer.push_back(c);
      break;
    }
  }
}

void _echoBuffer(std::string_view buffer) {
  ::Serial.printf(CLEAR_LINE "> %.*s", buffer.size(), buffer.data());
}

void _echoHandleSerialInput(std::string_view buffer, bool hasData) {
  static int64_t lastActivity = 0;
  static bool hasChanges      = false;

  // If serial echo is disabled, don't do anything past this point
  if (!s_echoEnabled) {
    return;
  }

  // If the command starts with a $, it's a automated command, don't echo it
  if (!buffer.empty() && buffer[0] == '$') {
    return;
  }

  // Update activity state
  if (hasData) {
    hasChanges   = true;
    lastActivity = OpenShock::millis();
  }

  // If theres has been received data, but no new data for a while, echo the buffer
  if (hasChanges && OpenShock::millis() - lastActivity > PASTE_INTERVAL_THRESHOLD_MS) {
    _echoBuffer(buffer);
    hasChanges   = false;
    lastActivity = OpenShock::millis();
  }
}

void _processSerialLine(std::string_view line) {
  line = OpenShock::StringTrim(line);
  if (line.empty()) {
    return;
  }

  bool isAutomated = line[0] == '$';

  // If automated, remove the $ prefix
  // If it's not automated, we can echo the command if echo is enabled
  if (isAutomated) {
    line = line.substr(1);
  } else if (s_echoEnabled) {
    _echoBuffer(line);
    ::Serial.println();
  }

  auto parts                 = OpenShock::StringSplit(line, ' ', 1);
  std::string_view command   = parts[0];
  std::string_view arguments = parts.size() > 1 ? parts[1] : std::string_view();

  auto it = s_commandHandlers.find(command);
  if (it == s_commandHandlers.end()) {
    SERPR_ERROR("Command \"%.*s\" not found", command.size(), command.data());
    return;
  }

  it->second.commandHandler(arguments, isAutomated);
}

bool SerialInputHandler::Init() {
  static bool s_initialized = false;
  if (s_initialized) {
    OS_LOGW(TAG, "Serial input handler already initialized");
    return false;
  }
  s_initialized = true;

  // Register command handlers
  RegisterCommandHandler(kVersionCmdHandler);
  RegisterCommandHandler(kRestartCmdHandler);
  RegisterCommandHandler(kSystemInfoCmdHandler);
  RegisterCommandHandler(kSerialEchoCmdHandler);
  RegisterCommandHandler(kValidGpiosCmdHandler);
  RegisterCommandHandler(kRfTxPinCmdHandler);
  RegisterCommandHandler(kDomainCmdHandler);
  RegisterCommandHandler(kAuthTokenCmdHandler);
  RegisterCommandHandler(kLcgOverrideCmdHandler);
  RegisterCommandHandler(kHostnameCmdHandler);
  RegisterCommandHandler(kNetworksCmdHandler);
  RegisterCommandHandler(kKeepAliveCmdHandler);
  RegisterCommandHandler(kJsonConfigCmdHandler);
  RegisterCommandHandler(kRawConfigCmdHandler);
  RegisterCommandHandler(kRfTransmitCmdHandler);
  RegisterCommandHandler(kFactoryResetCmdHandler);
  RegisterCommandHandler(khelpCmdHandler);

  SerialInputHandler::PrintWelcomeHeader();
  SerialInputHandler::PrintVersionInfo();
  Serial.println();

  if (!Config::GetSerialInputConfigEchoEnabled(s_echoEnabled)) {
    OS_LOGE(TAG, "Failed to get serial echo status from config");
    return false;
  }

  return true;
}

void SerialInputHandler::Update() {
  static std::string buffer = "";

  switch (_tryReadSerialLine(buffer)) {
  case SerialReadResult::LineEnd:
    _processSerialLine(buffer);

    // Deallocate memory if the buffer is too large
    if (buffer.capacity() > SERIAL_BUFFER_CLEAR_THRESHOLD) {
      buffer.clear();
      buffer.shrink_to_fit();
    } else {
      buffer.resize(0); // Hopefully doesn't deallocate memory
    }

    // Skip any remaining trailing whitespaces
    _skipSerialWhitespaces(buffer);
    break;
  case SerialReadResult::AutoCompleteRequest:
    ::Serial.printf(CLEAR_LINE "> %.*s [AutoComplete is not implemented]", buffer.size(), buffer.data());
    break;
  case SerialReadResult::Data:
    _echoHandleSerialInput(buffer, true);
    break;
  default:
    _echoHandleSerialInput(buffer, false);
    break;
  }
}

void SerialInputHandler::PrintWelcomeHeader() {
  Serial.print(R"(
============== OPENSHOCK ==============
  Contribute @ github.com/OpenShock
  Discuss    @ discord.gg/OpenShock
  Type 'help' for available commands
=======================================
)");
}

void SerialInputHandler::PrintVersionInfo() {
  Serial.print("\
  Version:  " OPENSHOCK_FW_VERSION "\n\
    Build:  " OPENSHOCK_FW_MODE "\n\
   Commit:  " OPENSHOCK_FW_GIT_COMMIT "\n\
    Board:  " OPENSHOCK_FW_BOARD "\n\
     Chip:  " OPENSHOCK_FW_CHIP "\n\
");
}
