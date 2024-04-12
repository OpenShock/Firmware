#include "serial/SerialInputHandler.h"

#include "Chipset.h"
#include "CommandHandler.h"
#include "config/Config.h"
#include "config/SerialInputConfig.h"
#include "FormatHelpers.h"
#include "http/HTTPRequestManager.h"
#include "Logging.h"
#include "serialization/JsonAPI.h"
#include "serialization/JsonSerial.h"
#include "Time.h"
#include "util/Base64Utils.h"
#include "wifi/WiFiManager.h"

#include <cJSON.h>
#include <Esp.h>

#include <unordered_map>

#include <cstring>

const char* const TAG = "SerialInputHandler";

#define SERPR_SYS(format, ...)      Serial.printf("$SYS$|" format "\n", ##__VA_ARGS__)
#define SERPR_RESPONSE(format, ...) SERPR_SYS("Response|" format, ##__VA_ARGS__)
#define SERPR_SUCCESS(format, ...)  SERPR_SYS("Success|" format, ##__VA_ARGS__)
#define SERPR_ERROR(format, ...)    SERPR_SYS("Error|" format, ##__VA_ARGS__)

using namespace OpenShock;

constexpr std::int64_t PASTE_INTERVAL_THRESHOLD_MS  = 20;
constexpr std::size_t SERIAL_BUFFER_CLEAR_THRESHOLD = 512;

struct SerialCmdHandler {
  const char* cmd;
  const char* helpResponse;
  void (*commandHandler)(char*, std::size_t);
};

static bool s_echoEnabled = true;
static std::unordered_map<std::string, SerialCmdHandler> s_commandHandlers;

/// @brief Tries to parse a boolean from a string (case-insensitive)
/// @param str Input string
/// @param strLen Length of input string
/// @param out Output boolean
/// @return True if the argument is a boolean, false otherwise
bool _tryParseBool(const char* str, std::size_t strLen, bool& out) {
  if (str == nullptr || strLen == 0) {
    return false;
  }

  if (strcasecmp(str, "true") == 0) {
    out = true;
    return true;
  }

  if (strcasecmp(str, "false") == 0) {
    out = false;
    return true;
  }

  return false;
}

void _handleVersionCommand(char* arg, std::size_t argLength) {
  (void)arg;
  (void)argLength;

  Serial.print("\n");
  SerialInputHandler::PrintVersionInfo();
}

void _handleRestartCommand(char* arg, std::size_t argLength) {
  (void)arg;
  (void)argLength;

  Serial.println("Restarting ESP...");
  ESP.restart();
}

void _handleFactoryResetCommand(char* arg, std::size_t argLength) {
  (void)arg;
  (void)argLength;

  Serial.println("Resetting to factory defaults...");
  Config::FactoryReset();
  Serial.println("Restarting...");
  ESP.restart();
}

void _handleRfTxPinCommand(char* arg, std::size_t argLength) {
  if (arg == nullptr || argLength == 0) {
    std::uint8_t txPin;
    if (!Config::GetRFConfigTxPin(txPin)) {
      SERPR_ERROR("Failed to get RF TX pin from config");
      return;
    }

    // Get rmt pin
    SERPR_RESPONSE("RmtPin|%u", txPin);
    return;
  }

  unsigned int pin;
  if (sscanf(arg, "%u", &pin) != 1) {
    SERPR_ERROR("Invalid argument (not a number)");
    return;
  }

  if (pin > UINT8_MAX) {
    SERPR_ERROR("Invalid argument (out of range)");
    return;
  }

  OpenShock::SetRfPinResultCode result = OpenShock::CommandHandler::SetRfTxPin(static_cast<std::uint8_t>(pin));

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

void _handleDomainCommand(char* arg, std::size_t argLength) {
  if (arg == nullptr || argLength == 0) {
    std::string domain;
    if (!Config::GetBackendDomain(domain)) {
      SERPR_ERROR("Failed to get domain from config");
      return;
    }

    // Get domain
    SERPR_RESPONSE("Domain|%s", domain.c_str());
    return;
  }

  char uri[OPENSHOCK_URI_BUFFER_SIZE];
  int rv = snprintf(uri, sizeof(uri), "https://%.*s/1", static_cast<int>(argLength), arg);
  if (rv < 0 || static_cast<std::size_t>(rv) >= sizeof(uri)) {
    SERPR_ERROR("Domain name too long, please try increasing the \"OPENSHOCK_URI_BUFFER_SIZE\" constant in source code");
    return;
  }

  auto resp = HTTP::GetJSON<Serialization::JsonAPI::BackendVersionResponse>(
    uri,
    {
      {"Accept", "application/json"}
  },
    Serialization::JsonAPI::ParseBackendVersionJsonResponse,
    {200}
  );

  if (resp.result != HTTP::RequestResult::Success) {
    SERPR_ERROR("Tried to connect to \"%.*s\", but failed with status [%d], refusing to save domain to config", argLength, arg, resp.code);
    return;
  }

  ESP_LOGI(
    TAG,
    "Successfully connected to \"%.*s\", version: %.*s, commit: %.*s, current time: %.*s",
    argLength,
    arg,
    resp.data.version.size(),
    resp.data.version.data(),
    resp.data.commit.size(),
    resp.data.commit.data(),
    resp.data.currentTime.size(),
    resp.data.currentTime.data()
  );

  bool result = OpenShock::Config::SetBackendDomain(std::string(arg, argLength));

  if (!result) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config, restarting...");

  // Restart to use the new domain
  ESP.restart();
}

void _handleAuthtokenCommand(char* arg, std::size_t argLength) {
  if (arg == nullptr || argLength == 0) {
    std::string authToken;
    if (!Config::GetBackendAuthToken(authToken)) {
      SERPR_ERROR("Failed to get auth token from config");
      return;
    }

    // Get auth token
    SERPR_RESPONSE("AuthToken|%s", authToken.c_str());
    return;
  }

  bool result = OpenShock::Config::SetBackendAuthToken(std::string(arg, argLength));

  if (result) {
    SERPR_SUCCESS("Saved config");
  } else {
    SERPR_ERROR("Failed to save config");
  }
}

void _handleNetworksCommand(char* arg, std::size_t argLength) {
  cJSON* root;

  if (arg == nullptr || argLength == 0) {
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

  root = cJSON_ParseWithLength(arg, argLength);
  if (root == nullptr) {
    SERPR_ERROR("Failed to parse JSON: %s", cJSON_GetErrorPtr());
    return;
  }

  if (cJSON_IsArray(root) == 0) {
    SERPR_ERROR("Invalid argument (not an array)");
    return;
  }

  std::vector<Config::WiFiCredentials> creds;

  std::uint8_t id = 1;
  cJSON* network  = nullptr;
  cJSON_ArrayForEach(network, root) {
    Config::WiFiCredentials cred;

    if (!cred.FromJSON(network)) {
      SERPR_ERROR("Failed to parse network");
      return;
    }

    if (cred.id == 0) {
      cred.id = id++;
    }

    ESP_LOGI(TAG, "Adding network \"%s\" to config, id=%u", cred.ssid.c_str(), cred.id);

    creds.emplace_back(std::move(cred));
  }

  if (!OpenShock::Config::SetWiFiCredentials(creds)) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config");

  OpenShock::WiFiManager::RefreshNetworkCredentials();
}

void _handleKeepAliveCommand(char* arg, std::size_t argLength) {
  bool keepAliveEnabled;

  if (arg == nullptr || argLength == 0) {
    // Get keep alive status
    if (!Config::GetRFConfigKeepAliveEnabled(keepAliveEnabled)) {
      SERPR_ERROR("Failed to get keep-alive status from config");
      return;
    }

    SERPR_RESPONSE("KeepAlive|%s", keepAliveEnabled ? "true" : "false");
    return;
  }

  if (!_tryParseBool(arg, argLength, keepAliveEnabled)) {
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

void _handleSerialEchoCommand(char* arg, std::size_t argLength) {
  if (arg == nullptr || argLength == 0) {
    // Get current serial echo status
    SERPR_RESPONSE("SerialEcho|%s", s_echoEnabled ? "true" : "false");
    return;
  }

  bool enabled;
  if (!_tryParseBool(arg, argLength, enabled)) {
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

void _handleValidGpiosCommand(char* arg, std::size_t argLength) {
  if (arg != nullptr && argLength > 0) {
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

void _handleJsonConfigCommand(char* arg, std::size_t argLength) {
  if (arg == nullptr || argLength == 0) {
    // Get raw config
    std::string json = Config::GetAsJSON(true);

    SERPR_RESPONSE("JsonConfig|%s", json.c_str());
    return;
  }

  if (!Config::SaveFromJSON(std::string(arg, argLength))) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config, restarting...");

  ESP.restart();
}

void _handleRawConfigCommand(char* arg, std::size_t argLength) {
  if (arg == nullptr || argLength == 0) {
    std::vector<std::uint8_t> buffer;

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

  std::vector<std::uint8_t> buffer;
  if (!OpenShock::Base64Utils::Decode(arg, argLength, buffer)) {
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

void _handleDebugInfoCommand(char* arg, std::size_t argLength) {
  (void)arg;
  (void)argLength;

  SERPR_RESPONSE("RTOSInfo|Free Heap|%u", xPortGetFreeHeapSize());
  SERPR_RESPONSE("RTOSInfo|Min Free Heap|%u", xPortGetMinimumEverFreeHeapSize());

  const std::int64_t now = OpenShock::millis();
  SERPR_RESPONSE("RTOSInfo|UptimeMS|%lli", now);

  const std::int64_t seconds = now / 1000;
  const std::int64_t minutes = seconds / 60;
  const std::int64_t hours   = minutes / 60;
  const std::int64_t days    = hours / 24;
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

void _handleRFTransmitCommand(char* arg, std::size_t argLength) {
  if (arg == nullptr || argLength == 0) {
    SERPR_ERROR("No command");
    return;
  }
  cJSON* root = cJSON_ParseWithLength(arg, argLength);
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

void _handleHelpCommand(char* arg, std::size_t argLength) {
  if (arg != nullptr && argLength > 0) {
    // Convert argument to lowercase
    std::transform(arg, arg + argLength, arg, ::tolower);

    // Get help for a specific command
    auto it = s_commandHandlers.find(std::string(arg, argLength));
    if (it != s_commandHandlers.end()) {
      Serial.print(it->second.helpResponse);
      return;
    }

    SERPR_ERROR("Command \"%.*s\" not found", argLength, arg);

    return;
  }

  SerialInputHandler::PrintWelcomeHeader();

  // Raw string literal (1+ to remove the first newline)
  Serial.print(1 + R"(
help                   print this menu
help         <command> print help for a command
version                print version information
restart                restart the board
sysinfo                print debug information for various subsystems
echo                   get serial echo enabled
echo         <bool>    set serial echo enabled
validgpios             list all valid GPIO pins
rftxpin                get radio transmit pin
rftxpin      <pin>     set radio transmit pin
domain                 get backend domain
domain       <domain>  set backend domain
authtoken              get auth token
authtoken    <token>   set auth token
networks               get all saved networks
networks     <json>    set all saved networks
keepalive              get shocker keep-alive enabled
keepalive    <bool>    set shocker keep-alive enabled
jsonconfig             get configuration as JSON
jsonconfig   <json>    set configuration from JSON
rawconfig              get raw configuration as base64
rawconfig    <base64>  set raw configuration from base64
rftransmit   <json>    transmit a RF command
factoryreset           reset device to factory defaults and restart
)");
}

static const SerialCmdHandler kVersionCmdHandler = {
  "version",
  R"(version
  Print version information
  Example:
    version
)",
  _handleVersionCommand,
};
static const SerialCmdHandler kRestartCmdHandler = {
  "restart",
  R"(restart
  Restart the board
  Example:
    restart
)",
  _handleRestartCommand,
};
static const SerialCmdHandler kSystemInfoCmdHandler = {
  "sysinfo",
  R"(sysinfo
  Get system information from RTOS, WiFi, etc.
  Example:
    sysinfo
)",
  _handleDebugInfoCommand,
};
static const SerialCmdHandler kSerialEchoCmdHandler = {
  "echo",
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
  "validgpios",
  R"(validgpios
  List all valid GPIO pins
  Example:
    validgpios
)",
  _handleValidGpiosCommand,
};
static const SerialCmdHandler kRfTxPinCmdHandler = {
  "rftxpin",
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
  "domain",
  R"(domain
  Get the backend domain.

domain [<domain>]
  Set the backend domain.
  Arguments:
    <domain> must be a string.
  Example:
    domain api.shocklink.net
)",
  _handleDomainCommand,
};
static const SerialCmdHandler kAuthTokenCmdHandler = {
  "authtoken",
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
static const SerialCmdHandler kNetworksCmdHandler = {
  "networks",
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
  "keepalive",
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
  "jsonconfig",
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
  "rawconfig",
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
  "rftransmit",
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
  "factoryreset",
  R"(factoryreset
  Reset the device to factory defaults and restart
  Example:
    factoryreset
)",
  _handleFactoryResetCommand,
};
static const SerialCmdHandler khelpCmdHandler = {
  "help",
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
int findChar(const char* buffer, std::size_t bufferSize, char c) {
  for (int i = 0; i < bufferSize; i++) {
    if (buffer[i] == c) {
      return i;
    }
  }

  return -1;
}

int findLineEnd(const char* buffer, int bufferSize) {
  if (buffer == nullptr || bufferSize <= 0) return -1;

  for (int i = 0; i < bufferSize; i++) {
    if (buffer[i] == '\r' || buffer[i] == '\n' || buffer[i] == '\0') {
      return i;
    }
  }

  return -1;
}

int findLineStart(const char* buffer, int bufferSize, int lineEnd) {
  if (lineEnd < 0) return -1;
  if (lineEnd >= bufferSize) return -1;

  for (int i = lineEnd + 1; i < bufferSize; i++) {
    if (buffer[i] != '\r' && buffer[i] != '\n' && buffer[i] != '\0') {
      return i;
    }
  }

  return -1;
}

void processSerialLine(char* data, std::size_t length) {
  int delimiter = findChar(data, length, ' ');
  if (delimiter == 0) {
    SERPR_ERROR("Command cannot start with a space");
    return;
  }

  char* command             = data;
  std::size_t commandLength = length;
  char* arg                 = nullptr;
  std::size_t argLength     = 0;

  // If there is a delimiter, split the command and argument
  if (delimiter > 0) {
    data[delimiter] = '\0';
    commandLength   = delimiter;
    arg             = data + delimiter + 1;
    argLength       = length - delimiter - 1;
  }

  // Convert command to lowercase
  std::transform(command, command + commandLength, command, ::tolower);

  // TODO: Clean this up, test this
  auto it = s_commandHandlers.find(std::string(command, commandLength));
  if (it != s_commandHandlers.end()) {
    it->second.commandHandler(arg, argLength);
    return;
  }

  if (commandLength > 0) {
    SERPR_ERROR("Command \"%.*s\" not found", commandLength, command);
  } else {
    SERPR_ERROR("No command");
  }
}

bool SerialInputHandler::Init() {
  static bool s_initialized = false;
  if (s_initialized) {
    ESP_LOGW(TAG, "Serial input handler already initialized");
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
    ESP_LOGE(TAG, "Failed to get serial echo status from config");
    return false;
  }

  return true;
}

void SerialInputHandler::Update() {
  static char* buffer            = nullptr;  // TODO: Clean up this buffer every once in a while
  static std::size_t bufferSize  = 0;
  static std::size_t bufferIndex = 0;
  static std::int64_t lastEcho   = 0;
  static bool suppressingPaste   = false;

  while (true) {
    int available = Serial.available();
    if (available <= 0 && findLineEnd(buffer, bufferIndex) < 0) {
      // If we're suppressing paste, and we haven't printed anything in a while, print the buffer and stop suppressing
      if (buffer != nullptr && s_echoEnabled && suppressingPaste && OpenShock::millis() - lastEcho > PASTE_INTERVAL_THRESHOLD_MS) {
        // \r - carriage return, moves to start of line
        // \x1B[K - clears rest of line
        Serial.printf("\r\x1B[K> %.*s", bufferIndex, buffer);
        lastEcho         = OpenShock::millis();
        suppressingPaste = false;
      }
      break;
    }

    if (bufferIndex + available > bufferSize) {
      bufferSize = bufferIndex + available;

      void* newBuffer = realloc(buffer, bufferSize);
      if (newBuffer == nullptr) {
        free(buffer);
        buffer     = nullptr;
        bufferSize = 0;
        continue;
      }

      buffer = static_cast<char*>(newBuffer);
    }

    if (buffer == nullptr) {
      continue;
    }

    while (available-- > 0) {
      char c = Serial.read();
      // Handle backspace
      if (c == '\b') {
        if (bufferIndex > 0) {
          bufferIndex--;
        }
        continue;
      }
      buffer[bufferIndex++] = c;
    }

    int lineEnd = findLineEnd(buffer, bufferIndex);
    // No newline found, wait for more input
    if (lineEnd == -1) {
      if (s_echoEnabled) {
        // If we're typing without pasting, echo the buffer
        if (OpenShock::millis() - lastEcho > PASTE_INTERVAL_THRESHOLD_MS) {
          // \r - carriage return, moves to start of line
          // \x1B[K - clears rest of line
          Serial.printf("\r\x1B[K> %.*s", bufferIndex, buffer);
          lastEcho         = OpenShock::millis();
          suppressingPaste = false;
        } else {
          lastEcho         = OpenShock::millis();
          suppressingPaste = true;
        }
      }
      break;
    }

    buffer[lineEnd] = '\0';
    Serial.printf("\r> %s\n", buffer);

    processSerialLine(buffer, lineEnd);

    int nextLine = findLineStart(buffer, bufferSize, lineEnd + 1);
    if (nextLine < 0) {
      bufferIndex = 0;
      // Free buffer if it's too big
      if (bufferSize > SERIAL_BUFFER_CLEAR_THRESHOLD) {
        ESP_LOGV(TAG, "Clearing serial input buffer");
        bufferSize = 0;
        free(buffer);
        buffer = nullptr;
      }
      break;
    }

    int remaining = bufferIndex - nextLine;
    if (remaining > 0) {
      memmove(buffer, buffer + nextLine, remaining);
      bufferIndex = remaining;
    } else {
      bufferIndex = 0;
      // Free buffer if it's too big
      if (bufferSize > SERIAL_BUFFER_CLEAR_THRESHOLD) {
        ESP_LOGV(TAG, "Clearing serial input buffer");
        bufferSize = 0;
        free(buffer);
        buffer = nullptr;
      }
    }
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
