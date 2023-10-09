#include "SerialInputHandler.h"

#include "Config.h"

#include <ArduinoJson.h>
#include <Esp.h>
#include <HardwareSerial.h>

#include <unordered_map>

using namespace OpenShock;

void _handleRestartCommand(char* arg, std::size_t argLength) {
  Serial.println("Restarting ESP...");
  ESP.restart();
}

void _handleHelpCommand(char* arg, std::size_t argLength) {
  SerialInputHandler::PrintWelcomeHeader();
  Serial.println("help          print this menu");
  Serial.println("version       print version information");
  Serial.println("restart       restart the board");
  Serial.println("rmtpin <pin>  set radio pin to <pin>\n");
}

void _handleVersionCommand(char* arg, std::size_t argLength) {
  Serial.print("\n");
  SerialInputHandler::PrintVersionInfo();
}

void _handleAuthtokenCommand(char* arg, std::size_t argLength) {
  if (arg == nullptr || argLength <= 0) {
    Serial.println("SYS|Error|Invalid argument");
    return;
  }

  OpenShock::Config::SetBackendAuthToken(std::string(arg, argLength));

  Serial.println("SYS|Success|Saved config");
}

void _handleRmtpinCommand(char* arg, std::size_t argLength) {
  if (arg == nullptr || argLength <= 0) {
    Serial.println("SYS|Error|Invalid argument");
    return;
  }

  std::uint32_t pin;
  if (sscanf(arg, "%u", &pin) != 1) {
    Serial.println("SYS|Error|Invalid argument (not a number)");
    return;
  }

  OpenShock::Config::SetRFConfig({.txPin = pin});

  Serial.println("SYS|Success|Saved config");
}

void _handleNetworksCommand(char* arg, std::size_t argLength) {
  if (arg == nullptr || argLength <= 0) {
    Serial.println("SYS|Error|Invalid argument");
    return;
  }

  DynamicJsonDocument doc(1024);
  deserializeJson(doc, arg, argLength);

  JsonArray networks = doc["networks"];

  std::vector<Config::WiFiCredentials> creds;
  for (auto it = networks.begin(); it != networks.end(); ++it) {
    JsonObject network = *it;

    std::string ssid     = network["ssid"].as<std::string>();
    std::string password = network["password"].as<std::string>();

    if (ssid.empty() || password.empty()) {
      Serial.println("SYS|Error|Invalid argument (missing ssid or password)");
      return;
    }

    Config::WiFiCredentials cred {
      .id       = 0,
      .ssid     = ssid,
      .bssid    = {0, 0, 0, 0, 0, 0},
      .password = password,
    };

    creds.push_back(std::move(cred));
  }

  OpenShock::Config::SetWiFiCredentials(creds);

  Serial.println("SYS|Success|Saved config");
}

static std::unordered_map<std::string, void (*)(char*, std::size_t)> s_commandHandlers = {
  {"authtoken", _handleAuthtokenCommand},
  {   "rmtpin",    _handleRmtpinCommand},
  { "networks",  _handleNetworksCommand},
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
    Serial.println("SYS|Error|Command cannot start with a space");
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

  Serial.println("SYS|Error|Command not found");
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
