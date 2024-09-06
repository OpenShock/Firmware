#include "serial/SerialInputHandler.h"

const char* const TAG = "SerialInputHandler";

#include "Chipset.h"
#include "CommandHandler.h"
#include "config/Config.h"
#include "config/SerialInputConfig.h"
#include "FormatHelpers.h"
#include "http/HTTPRequestManager.h"
#include "intconv.h"
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

using namespace OpenShock;

const int64_t PASTE_INTERVAL_THRESHOLD_MS       = 20;
const std::size_t SERIAL_BUFFER_CLEAR_THRESHOLD = 512;

static bool s_echoEnabled = true;
static std::unordered_map<std::string_view, SerialCmdHandler, std::hash_ci, std::equals_ci> s_commandHandlers;

/// @brief Tries to parse a boolean from a string (case-insensitive)
/// @param str Input string
/// @param strLen Length of input string
/// @param out Output boolean
/// @return True if the argument is a boolean, false otherwise
bool _tryParseBool(std::string_view str, bool& out) {
  if (str.empty()) {
    return false;
  }

  str = OpenShock::StringTrim(str);

  if (str.length() > 5) {
    return false;
  }

  if (strncasecmp(str.data(), "true", str.length()) == 0) {
    out = true;
    return true;
  }

  if (strncasecmp(str.data(), "false", str.length()) == 0) {
    out = false;
    return true;
  }

  return false;
}

void _handleHelpCommand(OpenShock::StringView arg) {
  arg = arg.trim();
  if (arg.isNullOrEmpty()) {
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

static const OpenShock::SerialCmdHandler khelpCmdHandler = {
  "help"_sv,
  R"(help [<command>]
  Print help information
  Arguments:
    <command> (optional) command to print help for
  Example:
    help
)",
  _handleHelpCommand,
};

void RegisterCommandHandler(const OpenShock::SerialCmdHandler& handler) {
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

void processSerialLine(std::string_view line) {
  line = OpenShock::StringTrim(line);
  if (line.empty()) {
    SERPR_ERROR("No command");
    return;
  }

  auto parts                 = OpenShock::StringSplit(line, ' ', 1);
  std::string_view command   = parts[0];
  std::string_view arguments = parts.size() > 1 ? parts[1] : std::string_view();

  auto it = s_commandHandlers.find(command);
  if (it == s_commandHandlers.end()) {
    SERPR_ERROR("Command \"%.*s\" not found", command.size(), command.data());
    return;
  }

  it->second.commandHandler(arguments);
}

bool SerialInputHandler::Init() {
  static bool s_initialized = false;
  if (s_initialized) {
    OS_LOGW(TAG, "Serial input handler already initialized");
    return false;
  }
  s_initialized = true;

  // Register command handlers
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
  static char* buffer            = nullptr;  // TODO: Clean up this buffer every once in a while
  static std::size_t bufferSize  = 0;
  static std::size_t bufferIndex = 0;
  static int64_t lastEcho        = 0;
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

    std::string_view line = OpenShock::StringTrim(std::string_view(buffer, lineEnd));

    Serial.printf("\r> %.*s\n", line.size(), line.data());

    processSerialLine(line);

    int nextLine = findLineStart(buffer, bufferSize, lineEnd + 1);
    if (nextLine < 0) {
      bufferIndex = 0;
      // Free buffer if it's too big
      if (bufferSize > SERIAL_BUFFER_CLEAR_THRESHOLD) {
        OS_LOGV(TAG, "Clearing serial input buffer");
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
        OS_LOGV(TAG, "Clearing serial input buffer");
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
