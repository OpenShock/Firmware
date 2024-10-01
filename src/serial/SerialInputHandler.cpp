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

void _handleHelpCommand(std::string_view arg, bool isAutomated) {
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
      ESP_LOGV(TAG, "Collecting help for command group: %.*s", cmd.name().size(), cmd.name().data());
      buffer.append(cmd.name());
      //buffer.append(paddedLength - cmd.name().size(), ' ');
      buffer.append("\n");

      for (const auto& command : cmd.commands()) {
        ESP_LOGV(TAG, "Collecting help for command: %.*s", command.description().size(), command.description().data());
        buffer.append("  ");
        buffer.append(command.description());
        //buffer.append(paddedLength - command.description().size(), ' ');

        for (const auto& arg : command.arguments()) {
          ESP_LOGV(TAG, "Collecting help for argument: %.*s", arg.name.size(), arg.name.data());
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
  std::string_view command   = OpenShock::StringTrim(parts[0]);
  std::string_view arguments = parts.size() > 1 ? parts[1] : std::string_view();

  if (command == "help"sv) {
    _handleHelpCommand(arguments, isAutomated);
    return;
  }

  auto it = s_commandHandlers.find(command);
  if (it == s_commandHandlers.end()) {
    SERPR_ERROR("Command \"%.*s\" not found", command.size(), command.data());
    return;
  }

  for (const auto& cmd : it->second.commands()) {
    if (cmd.arguments().empty()) {
      cmd.commandHandler()(arguments, isAutomated);
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
  s_commandGroups = OpenShock::Serial::CommandHandlers::AllCommandHandlers();
  for (const auto& handler : s_commandGroups) {
    ESP_LOGV(TAG, "Registering command handler: %.*s", handler.name().size(), handler.name().data());
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
