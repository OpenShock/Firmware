#include "SerialInputHandler.h"

#include "CommandHandler.h"
#include "Config.h"
#include "Logging.h"
#include "util/JsonRoot.h"
#include "wifi/WiFiManager.h"

#include <cJSON.h>
#include <Esp.h>

#include <unordered_map>

const char* const TAG = "SerialInputHandler";

using namespace OpenShock;

const char* const kCommandHelp         = "help";
const char* const kCommandVersion      = "version";
const char* const kCommandRestart      = "restart";
const char* const kCommandRmtpin       = "rmtpin";
const char* const kCommandAuthToken    = "authtoken";
const char* const kCommandNetworks     = "networks";
const char* const kCommandKeepAlive    = "keepalive";
const char* const kCommandFactoryReset = "factoryreset";

void _handleHelpCommand(char* arg, std::size_t argLength) {
  SerialInputHandler::PrintWelcomeHeader();
  if (arg == nullptr || argLength <= 0) {
    Serial.println("help                   print this menu");
    Serial.println("help         <command> print help for a command");
    Serial.println("version                print version information");
    Serial.println("restart                restart the board");
    Serial.println("rmtpin                 get radio pin");
    Serial.println("rmtpin       <pin>     set radio pin");
    Serial.println("authtoken    <token>   set auth token");
    Serial.println("networks               get all saved networks");
    Serial.println("networks     <json>    set all saved networks");
    Serial.println("keepalive              get shocker keep-alive status");
    Serial.println("keepalive    <bool>    enable/disable shocker keep-alive");
    Serial.println("factoryreset           reset device to factory defaults and reboot");
    return;
  }

  if (strcmp(arg, kCommandRmtpin) == 0) {
    Serial.println("rmtpin");
    Serial.println("  Get the GPIO pin used for the radio transmitter.");
    Serial.println();
    Serial.println("rmtpin [<pin>]");
    Serial.println("  Set the GPIO pin used for the radio transmitter.");
    Serial.println("  Arguments:");
    Serial.println("    <pin> must be a number.");
    Serial.println("  Example:");
    Serial.println("    rmtpin 15");
    return;
  }

  if (strcmp(arg, kCommandAuthToken) == 0) {
    Serial.println("authtoken <token>");
    Serial.println("  Set the auth token.");
    Serial.println("  Arguments:");
    Serial.println("    <token> must be a string.");
    Serial.println("  Example:");
    Serial.println("    authtoken mytoken");
    return;
  }

  if (strcmp(arg, kCommandNetworks) == 0) {
    Serial.println("networks");
    Serial.println("  Get all saved networks.");
    Serial.println();
    Serial.println("networks [<json>]");
    Serial.println("  Set all saved networks.");
    Serial.println("  Arguments:");
    Serial.println("    <json> must be a array of objects with the following fields:");
    Serial.println("      ssid     (string)  SSID of the network");
    Serial.println("      password (string)  Password of the network");
    Serial.println("  Example:");
    Serial.println("    networks [{\"ssid\":\"myssid\",\"password\":\"mypassword\"}]");
    return;
  }

  if (strcmp(arg, kCommandKeepAlive) == 0) {
    Serial.println("keepalive");
    Serial.println("  Get the shocker keep-alive status.");
    Serial.println();
    Serial.println("keepalive [<bool>]");
    Serial.println("  Enable/disable shocker keep-alive.");
    Serial.println("  Arguments:");
    Serial.println("    <bool> must be a boolean.");
    Serial.println("  Example:");
    Serial.println("    keepalive true");
    return;
  }

  if (strcmp(arg, kCommandRestart) == 0) {
    Serial.println(kCommandRestart);
    Serial.println("  Restart the board");
    Serial.println("  Example:");
    Serial.println("    restart");
    return;
  }

  if (strcmp(arg, kCommandFactoryReset) == 0) {
    Serial.println(kCommandFactoryReset);
    Serial.println("  Reset the device to factory defaults and reboot");
    Serial.println("  Example:");
    Serial.println("    factoryreset");
    return;
  }

  if (strcmp(arg, kCommandVersion) == 0) {
    Serial.println(kCommandVersion);
    Serial.println("  Print version information");
    Serial.println("  Example:");
    Serial.println("    version");
    return;
  }

  if (strcmp(arg, kCommandHelp) == 0) {
    Serial.println(kCommandHelp);
    Serial.println("  Print help information");
    Serial.println("  Arguments:");
    Serial.println("    <command> (optional) command to print help for");
    Serial.println("  Example:");
    Serial.println("    help");
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
    Serial.print("$SYS$|Response|RmtPin|");
    Serial.println(Config::GetRFConfig().txPin);
    return;
  }

  unsigned int pin;
  if (sscanf(arg, "%u", &pin) != 1) {
    Serial.println("$SYS$|Error|Invalid argument (not a number)");
    return;
  }

  if (pin > UINT8_MAX) {
    Serial.println("$SYS$|Error|Invalid argument (out of range)");
    return;
  }

  OpenShock::CommandHandler::SetRfTxPin(static_cast<std::uint8_t>(pin));

  Serial.println("$SYS$|Success|Saved config");
}

void _handleAuthtokenCommand(char* arg, std::size_t argLength) {
  if (arg == nullptr || argLength <= 0) {
    Serial.println("$SYS$|Error|Invalid argument");
    return;
  }

  OpenShock::Config::SetBackendAuthToken(std::string(arg, argLength));

  Serial.println("$SYS$|Success|Saved config");
}

void _handleNetworksCommand(char* arg, std::size_t argLength) {
  cJSON* network = nullptr;
  OpenShock::JsonRoot root;

  if (arg == nullptr || argLength <= 0) {
    root = OpenShock::JsonRoot::CreateArray();
    if (!root.isValid()) {
      Serial.println("$SYS$|Error|Failed to create JSON array");
      return;
    }

    for (auto& creds : Config::GetWiFiCredentials()) {
      network = cJSON_CreateObject();
      if (network == nullptr) {
        Serial.println("$SYS$|Error|Failed to create JSON object");
        return;
      }

      cJSON_AddStringToObject(network, "ssid", creds.ssid.c_str());
      cJSON_AddStringToObject(network, "password", creds.password.c_str());

      cJSON_AddItemToArray(root, network);
    }

    char* out = cJSON_PrintUnformatted(root);
    if (out == nullptr) {
      Serial.println("$SYS$|Error|Failed to print JSON");
      return;
    }

    Serial.print("$SYS$|Response|Networks|");
    Serial.println(out);

    cJSON_free(out);
    return;
  }

  root = OpenShock::JsonRoot::Parse(arg, argLength);
  if (!root.isValid()) {
    Serial.print("$SYS$|Error|Failed to parse JSON: ");
    Serial.println(root.GetErrorMessage());
    return;
  }
  if (!root.isArray()) {
    Serial.println("$SYS$|Error|Invalid argument (not an array)");
    return;
  }

  std::uint8_t id = 1;
  std::vector<Config::WiFiCredentials> creds;
  cJSON_ArrayForEach(network, root) {
    if (!cJSON_IsObject(network)) {
      Serial.println("$SYS$|Error|Invalid argument (array entry is not an object)");
      return;
    }

    const cJSON* ssid     = cJSON_GetObjectItemCaseSensitive(network, "ssid");
    const cJSON* password = cJSON_GetObjectItemCaseSensitive(network, "password");

    if (!cJSON_IsString(ssid) || !cJSON_IsString(password)) {
      Serial.println("$SYS$|Error|Invalid argument (ssid or password is not a string)");
      return;
    }

    const char* ssidStr     = ssid->valuestring;
    const char* passwordStr = password->valuestring;

    if (ssidStr == nullptr || passwordStr == nullptr) {
      Serial.println("$SYS$|Error|Invalid argument (ssid or password is null)");
      return;
    }
    if (ssidStr[0] == '\0' || passwordStr[0] == '\0') {
      Serial.println("$SYS$|Error|Invalid argument (ssid or password is empty)");
      return;
    }

    Config::WiFiCredentials cred {
      .id       = id++,
      .ssid     = ssidStr,
      .bssid    = {0, 0, 0, 0, 0, 0},
      .password = passwordStr,
    };
    ESP_LOGI(TAG, "Adding network to config %s", ssidStr);

    creds.push_back(std::move(cred));
  }

  if (!OpenShock::Config::SetWiFiCredentials(creds)) {
    Serial.println("$SYS$|Error|Failed to save config");
    return;
  }

  Serial.println("$SYS$|Success|Saved config");

  OpenShock::WiFiManager::RefreshNetworkCredentials();
}

void _handleKeepAliveCommand(char* arg, std::size_t argLength) {
  if (arg == nullptr || argLength <= 0) {
    // Get keep-alive status
    Serial.print("$SYS$|Response|KeepAlive|");
    Serial.println(Config::GetRFConfig().keepAliveEnabled ? "true" : "false");
    return;
  }

  std::uint8_t enabled = _argToBool(arg, argLength);

  if (enabled == 255) {
    Serial.println("$SYS$|Error|Invalid argument (not a boolean)");
    return;
  } else {
    OpenShock::CommandHandler::SetKeepAliveEnabled(enabled);
  }

  Serial.println("$SYS$|Success|Saved config");
}

static std::unordered_map<std::string, void (*)(char*, std::size_t)> s_commandHandlers = {
  {        kCommandHelp,         _handleHelpCommand},
  {     kCommandVersion,      _handleVersionCommand},
  {     kCommandRestart,      _handleRestartCommand},
  {      kCommandRmtpin,       _handleRmtpinCommand},
  {   kCommandAuthToken,    _handleAuthtokenCommand},
  {    kCommandNetworks,     _handleNetworksCommand},
  {   kCommandKeepAlive,    _handleKeepAliveCommand},
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
    Serial.println("$SYS$|Error|Command cannot start with a space");
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
  Serial.println("\n============== OPENSHOCK ==============");
  Serial.println("  Contribute @ github.com/OpenShock");
  Serial.println("  Discuss @ discord.gg/AHcCbXbEcF");
  Serial.println("  Type 'help' for available commands");
  Serial.println("=======================================\n");
}

void SerialInputHandler::PrintVersionInfo() {
  Serial.println("  Version:  " OPENSHOCK_FW_VERSION);
  Serial.println("    Build:  " OPENSHOCK_FW_MODE);
  Serial.println("   Commit:  " OPENSHOCK_FW_COMMIT);
  Serial.println("    Board:  " OPENSHOCK_FW_BOARD);
  Serial.println("     Chip:  " OPENSHOCK_FW_CHIP);
}
