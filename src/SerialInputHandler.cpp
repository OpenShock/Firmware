#include "SerialInputHandler.h"

#include "CommandHandler.h"
#include "config/Config.h"
#include "Logging.h"
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

#define kCommandHelp         "help"
#define kCommandVersion      "version"
#define kCommandRestart      "restart"
#define kCommandRmtpin       "rmtpin"
#define kCommandAuthToken    "authtoken"
#define kCommandNetworks     "networks"
#define kCommandKeepAlive    "keepalive"
#define kCommandRawConfig    "rawconfig"
#define kCommandFactoryReset "factoryreset"

void _handleHelpCommand(char* arg, std::size_t argLength) {
  SerialInputHandler::PrintWelcomeHeader();
  if (arg == nullptr || argLength <= 0) {
    // Raw string literal (1+ to remove the first newline)
    Serial.print(1 + R"(
help                   print this menu
help         <command> print help for a command
version                print version information
restart                restart the board
rmtpin                 get radio pin
rmtpin       <pin>     set radio pin
authtoken    <token>   set auth token
networks               get all saved networks
networks     <json>    set all saved networks
keepalive              get shocker keep-alive status
keepalive    <bool>    enable/disable shocker keep-alive
rawconfig              get raw binary config
rawconfig    <base64>  set raw binary config
factoryreset           reset device to factory defaults and reboot
)");
    return;
  }

  if (strcmp(arg, kCommandRmtpin) == 0) {
    Serial.print(kCommandRmtpin R"(
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

  if (strcmp(arg, kCommandAuthToken) == 0) {
    Serial.print(kCommandAuthToken R"( <token>
  Set the auth token.
  Arguments:
    <token> must be a string.
  Example:
    authtoken mytoken
)");
    return;
  }

  if (strcmp(arg, kCommandNetworks) == 0) {
    Serial.print(kCommandNetworks R"(
  Get all saved networks.

networks [<json>]
  Set all saved networks.
  Arguments:
    <json> must be a array of objects with the following fields:
      ssid     (string)  SSID of the network
      password (string)  Password of the network
  Example:
    networks [{\"ssid\":\"myssid\",\"password\":\"mypassword\"}]
)");
    return;
  }

  if (strcmp(arg, kCommandKeepAlive) == 0) {
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

  if (strcmp(arg, kCommandRestart) == 0) {
    Serial.print(kCommandRestart R"(
  Restart the board
  Example:
    restart
)");
    return;
  }

  if (strcmp(arg, kCommandRawConfig) == 0) {
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

  if (strcmp(arg, kCommandFactoryReset) == 0) {
    Serial.print(kCommandFactoryReset R"(
  Reset the device to factory defaults and reboot
  Example:
    factoryreset
)");
    return;
  }

  if (strcmp(arg, kCommandVersion) == 0) {
    Serial.print(kCommandVersion R"(
  Print version information
  Example:
    version
)");
    return;
  }

  if (strcmp(arg, kCommandHelp) == 0) {
    Serial.print(kCommandHelp R"( [<command>]
  Print help information
  Arguments:
    <command> (optional) command to print help for
  Example:
    help
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

  // Convert to lowercase
  std::transform(arg, arg + argLength, arg, ::tolower);

  if (strcmp(arg, "true") == 0 || strcmp(arg, "1") == 0 || strcmp(arg, "yes") == 0 || strcmp(arg, "y") == 0) {
    return 1;
  } else if (strcmp(arg, "false") == 0 || strcmp(arg, "0") == 0 || strcmp(arg, "no") == 0 || strcmp(arg, "n") == 0) {
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

void _handleRmtpinCommand(char* arg, std::size_t argLength) {
  if (arg == nullptr || argLength <= 0) {
    // Get rmt pin
    SERPR_RESPONSE("RmtPin|%u", Config::GetRFConfig().txPin);
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

  OpenShock::CommandHandler::SetRfTxPin(static_cast<std::uint8_t>(pin));

  SERPR_SUCCESS("Saved config");
}

void _handleAuthtokenCommand(char* arg, std::size_t argLength) {
  if (arg == nullptr || argLength <= 0) {
    SERPR_ERROR("Invalid argument");
    return;
  }

  OpenShock::Config::SetBackendAuthToken(std::string(arg, argLength));

  SERPR_SUCCESS("Saved config");
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

    ESP_LOGI(TAG, "Adding network to config %s", cred.ssid.c_str());

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
    // Get keep alive status
    SERPR_RESPONSE("KeepAlive|%s", Config::GetRFConfig().keepAliveEnabled ? "true" : "false");
    return;
  }

  std::uint8_t enabled = _argToBool(arg, argLength);

  if (enabled == 255) {
    SERPR_ERROR("Invalid argument (not a boolean)");
    return;
  } else {
    OpenShock::CommandHandler::SetKeepAliveEnabled(enabled);
  }

  SERPR_SUCCESS("Saved config");
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

  SERPR_SUCCESS("Saved config");

  ESP.restart();
}

static std::unordered_map<std::string, void (*)(char*, std::size_t)> s_commandHandlers = {
  {        kCommandHelp,         _handleHelpCommand},
  {     kCommandVersion,      _handleVersionCommand},
  {     kCommandRestart,      _handleRestartCommand},
  {      kCommandRmtpin,       _handleRmtpinCommand},
  {   kCommandAuthToken,    _handleAuthtokenCommand},
  {    kCommandNetworks,     _handleNetworksCommand},
  {   kCommandKeepAlive,    _handleKeepAliveCommand},
  {   kCommandRawConfig,    _handleRawConfigCommand},
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
  for (int i = 0; i < bufferSize; i++) {
    if (buffer[i] == '\r' || buffer[i] == '\n' || buffer[i] == '\0') {
      return i;
    }
  }

  return -1;
}

int findLineStart(const char* buffer, int bufferSize, int lineEnd) {
  if (lineEnd < 0) return -1;

  for (int i = lineEnd; i < bufferSize - lineEnd; i++) {
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

  // Handle arg-less commands
  if (delimiter > 0) {
    data[delimiter] = '\0';
    commandLength   = delimiter;
    arg             = data + delimiter + 1;
    argLength       = length - delimiter - 1;
  }

  // TODO: Clean this up, test this
  auto it = s_commandHandlers.find(std::string(command, commandLength));
  if (it != s_commandHandlers.end()) {
    it->second(arg, argLength);
    return;
  }

  Serial.println("$SYS$|Error|Command not found");
}

void SerialInputHandler::Update() {
  static char* buffer            = nullptr;  // TODO: Clean up this buffer every once in a while
  static std::size_t bufferSize  = 0;
  static std::size_t bufferIndex = 0;

  while (true) {
    int available = Serial.available();
    if (available <= 0) {
      break;
    }

    if (bufferIndex + available > bufferSize) {
      bufferSize = bufferIndex + available;
      buffer     = static_cast<char*>(realloc(buffer, bufferSize));
    }

    bufferIndex += Serial.readBytes(buffer + bufferIndex, available);

    int lineEnd = findLineEnd(buffer, bufferIndex);
    if (lineEnd == -1) {
      break;
    }

    buffer[lineEnd] = '\0';
    Serial.printf("> %s\n", buffer);

    processSerialLine(buffer, lineEnd);

    int nextLine = findLineStart(buffer, bufferIndex, lineEnd + 1);
    if (nextLine < 0) {
      bufferIndex = 0;
      break;
    }

    int remaining = bufferIndex - nextLine;
    if (remaining > 0) {
      memmove(buffer, buffer + nextLine, remaining);
      bufferIndex = remaining;
    } else {
      bufferIndex = 0;
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
