#include "SerialInputHandler.h"

#include "CommandHandler.h"
#include "config/Config.h"
#include "config/SerialInputConfig.h"
#include "Logging.h"
#include "serialization/JsonSerial.h"
#include "util/Base64Utils.h"
#include "wifi/WiFiManager.h"

#include <cJSON.h>
#include <Esp.h>

#include <unordered_map>

const char* const TAG = "SerialInputHandler";

#define SERPR_SYS(format, ...)      Serial.printf("$SYS$|" format "\n", ##__VA_ARGS__)
#define SERPR_RESPONSE(format, ...) SERPR_SYS("Response|" format, ##__VA_ARGS__)
#define SERPR_SUCCESS(format, ...)  SERPR_SYS("Success|" format, ##__VA_ARGS__)
#define SERPR_ERROR(format, ...)    SERPR_SYS("Error|" format, ##__VA_ARGS__)

using namespace OpenShock;

constexpr std::int64_t PASTE_INTERVAL_THRESHOLD_MS  = 20;
constexpr std::size_t SERIAL_BUFFER_CLEAR_THRESHOLD = 512;

static bool s_echoEnabled = true;

#define kCommandHelp         "help"
#define kCommandVersion      "version"
#define kCommandRestart      "restart"
#define kCommandRestartAlias "reboot"
#define kCommandRTOSInfo     "rtosinfo"
#define kCommandRTOSAlias    "rtos"
#define kCommandSerialEcho   "echo"
#define kCommandRfTxPin      "txpin"
#define kCommandAuthToken    "authtoken"
#define kCommandNetworks     "networks"
#define kCommandKeepAlive    "keepalive"
#define kCommandRawConfig    "rawconfig"
#define kCommandRFTransmit   "rftransmit"
#define kCommandFactoryReset "factoryreset"

void _handleHelpCommand(char* arg, std::size_t argLength) {
  if (arg == nullptr || argLength <= 0) {
    SerialInputHandler::PrintWelcomeHeader();
    // Raw string literal (1+ to remove the first newline)
    Serial.print(1 + R"(
help                   print this menu
help         <command> print help for a command
version                print version information
restart | reboot       restart the board
rtosinfo | rtos        print FreeRTOS debug information
echo                   get serial echo enabled
echo         <bool>    set serial echo enabled
txpin                  get radio transmit pin
txpin        <pin>     set radio transmit pin
authtoken              get auth token
authtoken    <token>   set auth token
networks               get all saved networks
networks     <json>    set all saved networks
keepalive              get shocker keep-alive enabled
keepalive    <bool>    set shocker keep-alive enabled
rawconfig              get raw binary config
rawconfig    <base64>  set raw binary config
rftransmit   <json>    transmit a RF command
factoryreset           reset device to factory defaults and reboot
)");
    return;
  }

  if (strcasecmp(arg, kCommandRfTxPin) == 0) {
    Serial.print(kCommandRfTxPin R"(
  Get the GPIO pin used for the radio transmitter.

rmtpin [<pin>]
  Set the GPIO pin used for the radio transmitter.
  Arguments:
    <pin> must be a number.
  Example:
    rmtpin 15
)");
    return;
  }

  if (strcasecmp(arg, kCommandAuthToken) == 0) {
    Serial.print(kCommandAuthToken R"( 
  Get the backend auth token.

authtoken [<token>]
  Set the auth token.
  Arguments:
    <token> must be a string.
  Example:
    authtoken mytoken
)");
    return;
  }

  if (strcasecmp(arg, kCommandRTOSInfo) == 0 || strcasecmp(arg, kCommandRTOSAlias) == 0) {
    Serial.print(kCommandRTOSInfo R"(
  Get FreeRTOS debug information.
  Example:
    rtosinfo
)");
    return;
  }

  if (strcasecmp(arg, kCommandSerialEcho) == 0) {
    Serial.print(kCommandSerialEcho R"(
  Get the serial echo status.

echo [<bool>]
  Enable/disable serial echo.
  Arguments:
    <bool> must be a boolean.
  Example:
    echo true
)");
    return;
  }

  if (strcasecmp(arg, kCommandNetworks) == 0) {
    Serial.print(kCommandNetworks R"(
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
)");
    return;
  }

  if (strcasecmp(arg, kCommandKeepAlive) == 0) {
    Serial.print(kCommandKeepAlive R"(
  Get the shocker keep-alive status.

keepalive [<bool>]
  Enable/disable shocker keep-alive.
  Arguments:
    <bool> must be a boolean.
  Example:
    keepalive true
)");
    return;
  }

  if (strcasecmp(arg, kCommandRestart) == 0 || strcasecmp(arg, kCommandRestartAlias) == 0) {
    Serial.print(kCommandRestart R"(
  Restart the board
  Example:
    restart
)");
    return;
  }

  if (strcasecmp(arg, kCommandRawConfig) == 0) {
    Serial.print(kCommandRawConfig R"(
  Get the raw binary config
  Example:
    rawconfig

rawconfig <base64>
  Set the raw binary config, and reboot
  Arguments:
    <base64> must be a base64 encoded string
  Example:
    rawconfig (base64 encoded binary data)
)");
    return;
  }

  if (strcasecmp(arg, kCommandFactoryReset) == 0) {
    Serial.print(kCommandFactoryReset R"(
  Reset the device to factory defaults and reboot
  Example:
    factoryreset
)");
    return;
  }

  if (strcasecmp(arg, kCommandVersion) == 0) {
    Serial.print(kCommandVersion R"(
  Print version information
  Example:
    version
)");
    return;
  }

  if (strcasecmp(arg, kCommandHelp) == 0) {
    Serial.print(kCommandHelp R"( [<command>]
  Print help information
  Arguments:
    <command> (optional) command to print help for
  Example:
    help
)");
    return;
  }

  if (strcasecmp(arg, kCommandRFTransmit) == 0) {
    Serial.print(kCommandRFTransmit R"( <json>
  Transmit a RF command
  Arguments:
    <json> must be a JSON object with the following fields:
      model      (string) Model of the shocker                    ("caixianlin", "petrainer")
      id         (number) ID of the shocker                       (0-65535)
      type       (string) Type of the command                     ("shock", "vibrate", "sound", "stop")
      intensity  (number) Intensity of the command                (0-255)
      durationMs (number) Duration of the command in milliseconds (0-65535)
  Example:
    rftransmit {"model":"caixianlin","id":12345,"type":"vibrate","intensity":99,"durationMs":500}
)");
    return;
  }

  Serial.println("Command not found");
}

// Checks if the given argument is a boolean
// Returns 0 if false, 1 if true, 255 if invalid
// Valid inputs: true, false, 1, 0, yes, no, y, n
// Case-insensitive
std::uint8_t _argToBool(char* arg, std::size_t argLength) {
  if (arg == nullptr || argLength <= 0) {
    return 255;
  }

  if (strcasecmp(arg, "true") == 0 || strcasecmp(arg, "1") == 0 || strcasecmp(arg, "yes") == 0 || strcasecmp(arg, "y") == 0) {
    return 1;
  } else if (strcasecmp(arg, "false") == 0 || strcasecmp(arg, "0") == 0 || strcasecmp(arg, "no") == 0 || strcasecmp(arg, "n") == 0) {
    return 0;
  } else {
    return 255;
  }
}

void _handleVersionCommand(char* arg, std::size_t argLength) {
  Serial.print("\n");
  SerialInputHandler::PrintVersionInfo();
}

void _handleRestartCommand(char* arg, std::size_t argLength) {
  Serial.println("Restarting ESP...");
  ESP.restart();
}

void _handleFactoryResetCommand(char* arg, std::size_t argLength) {
  Serial.println("Resetting to factory defaults...");
  Config::FactoryReset();
  Serial.println("Rebooting...");
  ESP.restart();
}

void _handleRfTxPinCommand(char* arg, std::size_t argLength) {
  if (arg == nullptr || argLength <= 0) {
    // Get rmt pin
    SERPR_RESPONSE("RfTxPin|%u", Config::GetRFConfig().txPin);
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

void _handleAuthtokenCommand(char* arg, std::size_t argLength) {
  if (arg == nullptr || argLength <= 0) {
    // Get auth token
    SERPR_RESPONSE("AuthToken|%s", Config::GetBackendAuthToken().c_str());
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
  cJSON* network = nullptr;
  cJSON* root;

  if (arg == nullptr || argLength <= 0) {
    root = cJSON_CreateArray();
    if (root == nullptr) {
      SERPR_ERROR("Failed to create JSON array");
      return;
    }

    for (auto& creds : Config::GetWiFiCredentials()) {
      network = creds.ToJSON();

      cJSON_AddItemToArray(root, network);
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

  if (!cJSON_IsArray(root)) {
    SERPR_ERROR("Invalid argument (not an array)");
    return;
  }

  std::uint8_t id = 1;
  std::vector<Config::WiFiCredentials> creds;
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

    creds.push_back(std::move(cred));
  }

  if (!OpenShock::Config::SetWiFiCredentials(creds)) {
    SERPR_ERROR("Failed to save config");
    return;
  }

  SERPR_SUCCESS("Saved config");

  OpenShock::WiFiManager::RefreshNetworkCredentials();
}

void _handleKeepAliveCommand(char* arg, std::size_t argLength) {
  if (arg == nullptr || argLength <= 0) {
    // Get current keep-alive status
    SERPR_RESPONSE("KeepAlive|%s", Config::GetRFConfig().keepAliveEnabled ? "true" : "false");
    return;
  }

  std::uint8_t enabled = _argToBool(arg, argLength);

  if (enabled == 255) {
    SERPR_ERROR("Invalid argument (not a boolean)");
    return;
  } else {
    bool result = OpenShock::CommandHandler::SetKeepAliveEnabled(enabled);

    if (result) {
      SERPR_SUCCESS("Saved config");
    } else {
      SERPR_ERROR("Failed to save config");
    }
  }
}

void _handleSerialEchoCommand(char* arg, std::size_t argLength) {
  if (arg == nullptr || argLength <= 0) {
    // Get current serial echo status
    SERPR_RESPONSE("SerialEcho|%s", s_echoEnabled ? "true" : "false");
    return;
  }

  std::uint8_t enabled = _argToBool(arg, argLength);

  if (enabled == 255) {
    SERPR_ERROR("Invalid argument (not a boolean)");
    return;
  } else {
    bool result   = Config::SetSerialInputConfigEchoEnabled(enabled);
    s_echoEnabled = enabled;

    if (result) {
      SERPR_SUCCESS("Saved config");
    } else {
      SERPR_ERROR("Failed to save config");
    }
  }
}

void _handleRawConfigCommand(char* arg, std::size_t argLength) {
  if (arg == nullptr || argLength <= 0) {
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

  SERPR_SUCCESS("Saved config, rebooting...");

  ESP.restart();
}

void _handleRTOSInfoCommand(char* arg, std::size_t argLength) {
  SERPR_RESPONSE("RTOSInfo");
  Serial.printf("Free heap: %u\n", xPortGetFreeHeapSize());
  Serial.printf("Min free heap: %u\n", xPortGetMinimumEverFreeHeapSize());
  Serial.printf("Free stack (serial/loop): %u\n", uxTaskGetStackHighWaterMark(nullptr));
}

void _handleRFTransmitCommand(char* arg, std::size_t argLength) {
  if (arg == nullptr || argLength <= 0) {
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

static std::unordered_map<std::string, void (*)(char*, std::size_t)> s_commandHandlers = {
  {        kCommandHelp,         _handleHelpCommand},
  {     kCommandVersion,      _handleVersionCommand},
  {     kCommandRestart,      _handleRestartCommand},
  {kCommandRestartAlias,      _handleRestartCommand},
  {    kCommandRTOSInfo,     _handleRTOSInfoCommand},
  {   kCommandRTOSAlias,     _handleRTOSInfoCommand},
  {  kCommandSerialEcho,   _handleSerialEchoCommand},
  {     kCommandRfTxPin,      _handleRfTxPinCommand},
  {   kCommandAuthToken,    _handleAuthtokenCommand},
  {    kCommandNetworks,     _handleNetworksCommand},
  {   kCommandKeepAlive,    _handleKeepAliveCommand},
  {   kCommandRawConfig,    _handleRawConfigCommand},
  {  kCommandRFTransmit,   _handleRFTransmitCommand},
  {kCommandFactoryReset, _handleFactoryResetCommand},
};

int findChar(const char* buffer, std::size_t bufferSize, char c) {
  for (int i = 0; i < bufferSize; i++) {
    if (buffer[i] == c) {
      return i;
    }
  }

  return -1;
}

int findLineEnd(const char* buffer, int bufferSize) {
  if (bufferSize <= 0) return -1;

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
    it->second(arg, argLength);
    return;
  }

  if (commandLength > 0) {
    SERPR_ERROR("Command \"%.*s\" not found", commandLength, command);
  } else {
    SERPR_ERROR("No command");
  }
}

bool SerialInputHandler::Init() {
  SerialInputHandler::PrintWelcomeHeader();
  SerialInputHandler::PrintVersionInfo();
  Serial.println();

  s_echoEnabled = Config::GetSerialInputConfig().echoEnabled;

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
    if (available <= 0 && findLineEnd(buffer, bufferIndex) == -1) {
      if (s_echoEnabled && suppressingPaste && OpenShock::millis() - lastEcho > PASTE_INTERVAL_THRESHOLD_MS) {
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
      buffer     = static_cast<char*>(realloc(buffer, bufferSize));
    }

    // bufferIndex += Serial.readBytes(buffer + bufferIndex, available);

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
    if (lineEnd == -1) {
      if (s_echoEnabled) {
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
    // Don't print finished command unless we were pasting or echo was off
    if (s_echoEnabled && !suppressingPaste) {
      Serial.println();
    } else {
      Serial.printf("> %s\n", buffer);
    }

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
   Commit:  " OPENSHOCK_FW_COMMIT "\n\
    Board:  " OPENSHOCK_FW_BOARD "\n\
     Chip:  " OPENSHOCK_FW_CHIP "\n\
");
}
