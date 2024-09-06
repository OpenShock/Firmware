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
#include "serial/command_handlers/CommandEntry.h"
#include "serial/command_handlers/common.h"
#include "serial/command_handlers/index.h"
#include "Time.h"
#include "util/Base64Utils.h"
#include "util/StringUtils.h"
#include "wifi/WiFiManager.h"

#include <Arduino.h>

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
static std::vector<OpenShock::Serial::CommandGroup> s_commandGroups;
static std::unordered_map<std::string_view, OpenShock::Serial::CommandGroup, std::hash_ci, std::equals_ci> s_commandHandlers;

void _handleHelpCommand(std::string_view arg) {
  arg = OpenShock::StringTrim(arg);
  if (arg.empty()) {
    SerialInputHandler::PrintWelcomeHeader();

    std::size_t longestCommand  = 0;
    std::size_t longestArgument = 0;
    std::size_t descriptionSize = 0;
    for (const auto& group : s_commandGroups) {
      longestCommand = std::max(longestCommand, group.name().size());
      for (const auto& command : group.commands()) {
        std::size_t argumentSize = 0;
        for (const auto& arg : command.arguments()) {
          argumentSize += arg.name.size() + 1; // +1 for space
        }
        longestArgument = std::max(longestArgument, argumentSize);
        descriptionSize += command.description().size();
      }
    }

    std::size_t paddedLength = longestCommand + 1 + longestArgument + 1; // +1 for space, +1 for newline

    std::string buffer;
    buffer.reserve((paddedLength * s_commandGroups.size()) + descriptionSize); // Approximate size

    OS_LOGV(TAG, "Longest command: %zu, longest argument: %zu, padded length: %zu", longestCommand, longestArgument, paddedLength);
    OS_LOGV(TAG, "Buffer size: %zu", buffer.capacity());
    
    for (const auto& cmd : s_commandGroups) {
      buffer.append(cmd.name());
      buffer.append(paddedLength - cmd.name().size(), ' ');
      buffer.append("\n");

      for (const auto& command : cmd.commands()) {
        buffer.append("  ");
        buffer.append(command.description());
        buffer.append(paddedLength - command.description().size(), ' ');

        for (const auto& arg : command.arguments()) {
          buffer.append(arg.name);
          buffer.append(" ");
        }

        buffer.append("\n");
      }
    }

    OS_LOGV(TAG, "Buffer size: %zu", buffer.size());

    ::Serial.print(buffer.data());
    return;
  }

  // Get help for a specific command
  auto it = s_commandHandlers.find(arg);
  if (it != s_commandHandlers.end()) {
    std::string buffer;
    for (const auto& command : it->second.commands()) {
      buffer.append(command.description());
      buffer.append("\n");
    }

    ::Serial.print(buffer.data());
    return;
  }

  SERPR_ERROR("Command \"%.*s\" not found", arg.length(), arg.data());
}

void RegisterCommandHandler(const OpenShock::Serial::CommandGroup& handler) {
  s_commandHandlers[handler.name()] = handler;
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

  for (const auto& cmd : it->second.commands()) {
    if (cmd.arguments().empty()) {
      cmd.commandHandler()(arguments);
      return;
    }
  }
}

bool SerialInputHandler::Init() {
  static bool s_initialized = false;
  if (s_initialized) {
    OS_LOGW(TAG, "Serial input handler already initialized");
    return false;
  }
  s_initialized = true;

  // Register command handlers
  for (const auto& handler : OpenShock::Serial::CommandHandlers::AllCommandHandlers()) {
    RegisterCommandHandler(handler);
  }

  SerialInputHandler::PrintWelcomeHeader();
  SerialInputHandler::PrintVersionInfo();
  ::Serial.println();

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
    int available = ::Serial.available();
    if (available <= 0 && findLineEnd(buffer, bufferIndex) < 0) {
      // If we're suppressing paste, and we haven't printed anything in a while, print the buffer and stop suppressing
      if (buffer != nullptr && s_echoEnabled && suppressingPaste && OpenShock::millis() - lastEcho > PASTE_INTERVAL_THRESHOLD_MS) {
        // \r - carriage return, moves to start of line
        // \x1B[K - clears rest of line
        ::Serial.printf("\r\x1B[K> %.*s", bufferIndex, buffer);
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
      char c = ::Serial.read();
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
          ::Serial.printf("\r\x1B[K> %.*s", bufferIndex, buffer);
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

    ::Serial.printf("\r> %.*s\n", line.size(), line.data());

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

bool SerialInputHandler::SerialEchoEnabled() {
  return s_echoEnabled;
}

void SerialInputHandler::SetSerialEchoEnabled(bool enabled) {
  s_echoEnabled = enabled;
}

void SerialInputHandler::PrintWelcomeHeader() {
  ::Serial.print(R"(
============== OPENSHOCK ==============
  Contribute @ github.com/OpenShock
  Discuss    @ discord.gg/OpenShock
  Type 'help' for available commands
=======================================
)");
}

void SerialInputHandler::PrintVersionInfo() {
  ::Serial.print("\
  Version:  " OPENSHOCK_FW_VERSION "\n\
    Build:  " OPENSHOCK_FW_MODE "\n\
   Commit:  " OPENSHOCK_FW_GIT_COMMIT "\n\
    Board:  " OPENSHOCK_FW_BOARD "\n\
     Chip:  " OPENSHOCK_FW_CHIP "\n\
");
}
