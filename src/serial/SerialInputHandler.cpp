#include "serial/SerialInputHandler.h"

const char* const TAG = "SerialInputHandler";

#include "Chipset.h"
#include "CommandHandler.h"
#include "config/Config.h"
#include "config/SerialInputConfig.h"
#include "Convert.h"
#include "Core.h"
#include "estop/EStopManager.h"
#include "FormatHelpers.h"
#include "http/HTTPRequestManager.h"
#include "Logging.h"
#include "serial/command_handlers/CommandEntry.h"
#include "serial/command_handlers/common.h"
#include "serial/command_handlers/index.h"
#include "serialization/JsonAPI.h"
#include "serialization/JsonSerial.h"
#include "util/Base64Utils.h"
#include "util/StringUtils.h"
#include "util/TaskUtils.h"
#include "wifi/WiFiManager.h"

#include <Arduino.h>

#include <cJSON.h>
#include <Esp.h>

#include <cstdio>
#include <cstring>
#include <string_view>
#include <unordered_map>

namespace std {
  struct hash_ci {
    std::size_t operator()(std::string_view str) const
    {
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
    bool operator()(std::string_view a, std::string_view b) const { return OpenShock::StringIEquals(a, b); }
  };
}  // namespace std

using namespace std::string_view_literals;

using namespace OpenShock;

const int64_t PASTE_INTERVAL_THRESHOLD_MS       = 20;
const std::size_t SERIAL_BUFFER_CLEAR_THRESHOLD = 512;

static bool s_echoEnabled = true;
static std::vector<OpenShock::Serial::CommandGroup> s_commandGroups;
static std::unordered_map<std::string_view, OpenShock::Serial::CommandGroup, std::hash_ci, std::equals_ci> s_commandHandlers;

void _printCompleteHelp()
{
  std::size_t commandCount    = 0;
  std::size_t longestCommand  = 0;
  std::size_t longestArgument = 0;
  std::size_t descriptionSize = 0;
  for (const auto& group : s_commandGroups) {
    longestCommand = std::max(longestCommand, group.name().size());
    for (const auto& command : group.commands()) {
      commandCount++;

      std::size_t argumentSize = 0;
      if (command.name().size() > 0) {
        argumentSize += command.name().size() + 1;  // +1 for space
      }
      for (const auto& arg : command.arguments()) {
        argumentSize += arg.name.size() + 3;  // +1 for space, +2 for <>
      }
      longestArgument = std::max(longestArgument, argumentSize);
      descriptionSize += command.description().size();
    }
  }

  std::size_t paddedLength = longestCommand + 1 + longestArgument + 2;  // +1 for space, +2 for newline

  std::string buffer;
  buffer.reserve((paddedLength * commandCount) + descriptionSize);  // Approximate size

  for (const auto& group : s_commandGroups) {
    for (const auto& command : group.commands()) {
      buffer.append(group.name());
      buffer.append((longestCommand - group.name().size()) + 1, ' ');

      std::size_t startSize = buffer.size();

      if (command.name().size() > 0) {
        buffer.append(command.name());
        buffer.push_back(' ');
      }

      for (const auto& arg : command.arguments()) {
        buffer.push_back('<');
        buffer.append(arg.name);
        buffer.push_back('>');
        buffer.push_back(' ');
      }

      buffer.append(longestArgument - (buffer.size() - startSize), ' ');

      buffer.append(command.description());

      buffer.push_back('\r');
      buffer.push_back('\n');
    }
  }

  SerialInputHandler::PrintWelcomeHeader();
  ::Serial.print(buffer.data());
}

void _printCommandHelp(Serial::CommandGroup& group)
{
  std::size_t size = 0;
  for (const auto& command : group.commands()) {
    size += 2;  // +2 for newline
    size += group.name().size();
    size++;  // +1 for space

    if (command.name().size() > 0) {
      size += command.name().size() + 1;  // +1 for space
    }

    for (const auto& arg : command.arguments()) {
      size += arg.name.size() + 3;  // +1 for space, +2 for <>
    }

    size += 2;  // +2 for newline

    if (command.description().size() > 0) {
      size = command.description().size() + 4;  // +2 for indent, +2 for newline
    }

    if (command.arguments().size() > 0) {
      size += 14;                     // +14 for "  Arguments:\r\n"
      for (const auto& arg : command.arguments()) {
        size += arg.name.size() + 7;  // +4 for indent, +2 for <>, +1 for space
        size += arg.constraint.size();
        if (arg.constraintExtensions.size() > 0) {
          size += 3;                 // +1 for ':', +2 for newline
          for (const auto& ext : arg.constraintExtensions) {
            size += ext.size() + 8;  // +2 for newline, +6 for indent
          }
        } else {
          size += 2;  // +2 for newline
        }
      }
    }

    size += 16;                       // +16 for "  Example:    \r\n"
    size += group.name().size() + 1;  // +1 for space

    if (command.name().size() > 0) {
      size += command.name().size() + 1;  // +1 for space
    }

    for (const auto& arg : command.arguments()) {
      size += arg.exampleValue.size() + 1;  // +1 for space
    }

    size += 2;  // +2 for newline
  }

  size += 2;  // +2 for newline

  std::string buffer;
  buffer.reserve(size);  // TODO: Should be exact size, is 20 bytes off, figure out why

  for (const auto& command : group.commands()) {
    buffer.push_back('\r');
    buffer.push_back('\n');
    buffer.append(group.name());
    buffer.push_back(' ');

    if (command.name().size() > 0) {
      buffer.append(command.name());
      buffer.push_back(' ');
    }

    for (const auto& arg : command.arguments()) {
      buffer.push_back('<');
      buffer.append(arg.name);
      buffer.push_back('>');
      buffer.push_back(' ');
    }

    buffer.push_back('\r');
    buffer.push_back('\n');

    if (command.description().size() > 0) {
      buffer.append(2, ' ');
      buffer.append(command.description());
      buffer.push_back('\r');
      buffer.push_back('\n');
    }

    if (command.arguments().size() > 0) {
      buffer.append("  Arguments:\r\n"sv);
      for (const auto& arg : command.arguments()) {
        buffer.append(4, ' ');
        buffer.push_back('<');
        buffer.append(arg.name);
        buffer.push_back('>');
        buffer.push_back(' ');
        buffer.append(arg.constraint);
        if (arg.constraintExtensions.size() > 0) {
          buffer.push_back('\r');
          buffer.push_back('\n');
          for (const auto& ext : arg.constraintExtensions) {
            buffer.append(6, ' ');
            buffer.append(ext);
            buffer.push_back('\r');
            buffer.push_back('\n');
          }
        } else {
          buffer.push_back('\r');
          buffer.push_back('\n');
        }
      }
    }

    buffer.append("  Example:\r\n    "sv);
    buffer.append(group.name());
    buffer.push_back(' ');

    if (command.name().size() > 0) {
      buffer.append(command.name());
      buffer.push_back(' ');
    }

    for (const auto& arg : command.arguments()) {
      buffer.append(arg.exampleValue);
      buffer.push_back(' ');
    }

    buffer.push_back('\r');
    buffer.push_back('\n');
  }
  buffer.push_back('\r');
  buffer.push_back('\n');

  ::Serial.print(buffer.data());
}

void _handleHelpCommand(std::string_view arg, bool isAutomated)
{
  arg = OpenShock::StringTrim(arg);
  if (arg.empty()) {
    _printCompleteHelp();
    return;
  }

  // Get help for a specific command
  auto it = s_commandHandlers.find(arg);
  if (it != s_commandHandlers.end()) {
    _printCommandHelp(it->second);
    return;
  }

  SERPR_ERROR("Command \"%.*s\" not found", arg.length(), arg.data());
}

void RegisterCommandHandler(const OpenShock::Serial::CommandGroup& handler)
{
  s_commandHandlers[handler.name()] = handler;
}

#define CLEAR_LINE "\r\x1B[K"

class SerialBuffer {
  DISABLE_COPY(SerialBuffer);
  DISABLE_MOVE(SerialBuffer);

public:
  constexpr SerialBuffer()
    : m_data(nullptr)
    , m_size(0)
    , m_capacity(0)
  {
  }
  inline SerialBuffer(std::size_t capacity)
    : m_data(new char[capacity])
    , m_size(0)
    , m_capacity(capacity)
  {
  }
  inline ~SerialBuffer() { delete[] m_data; }

  constexpr char* data() { return m_data; }
  constexpr std::size_t size() const { return m_size; }
  constexpr std::size_t capacity() const { return m_capacity; }
  constexpr bool empty() const { return m_size == 0; }

  constexpr void clear() { m_size = 0; }
  inline void destroy()
  {
    delete[] m_data;
    m_data     = nullptr;
    m_size     = 0;
    m_capacity = 0;
  }

  inline void reserve(std::size_t size)
  {
    size = (size + 31) & ~31;  // Align to 32 bytes

    if (size <= m_capacity) {
      return;
    }

    char* newData = new char[size];
    if (m_data != nullptr) {
      std::memcpy(newData, m_data, m_size);
      delete[] m_data;
    }

    m_data     = newData;
    m_capacity = size;
  }

  inline void push_back(char c)
  {
    if (m_size >= m_capacity) {
      reserve(m_capacity + 16);
    }

    m_data[m_size++] = c;
  }

  constexpr void pop_back()
  {
    if (m_size > 0) {
      --m_size;
    }
  }

  constexpr operator std::string_view() const { return std::string_view(m_data, m_size); }

private:
  char* m_data;
  std::size_t m_size;
  std::size_t m_capacity;
};

enum class SerialReadResult {
  NoData,
  Data,
  LineEnd,
  AutoCompleteRequest,
};

SerialReadResult _tryReadSerialLine(SerialBuffer& buffer)
{
  // Check if there's any data available
  int available = ::Serial.available();
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
      buffer.pop_back();  // Remove the last character from the buffer if it exists
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

    // If character is printable, add it to the buffer
    if (c > 31 && c < 127) {
      buffer.push_back(c);
    }
  }

  return SerialReadResult::Data;
}

void _skipSerialWhitespaces(SerialBuffer& buffer)
{
  int available = ::Serial.available();

  while (available-- > 0) {
    char c = ::Serial.read();

    if (c != ' ' && c != '\r' && c != '\n') {
      buffer.push_back(c);
      break;
    }
  }
}

void _echoBuffer(std::string_view buffer)
{
  printf(CLEAR_LINE "> %.*s", buffer.size(), buffer.data());
}

void _echoHandleSerialInput(std::string_view buffer, bool hasData)
{
  static int64_t lastActivity = 0;
  static bool hasChanges      = false;

  // If serial echo is disabled, don't do anything past this point
  if (!s_echoEnabled) {
    return;
  }

  // If the command starts with a $, it's a automated command, don't echo it
  if (OpenShock::StringHasPrefix(buffer, '$')) {
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

void _processSerialLine(std::string_view line)
{
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
    putchar('\n');
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

  // Get potential subcommand
  std::string_view firstArg;
  parts = OpenShock::StringSplit(arguments, ' ');
  if (parts.size() > 1) {
    firstArg = OpenShock::StringTrim(parts[0]);
  } else {
    firstArg = arguments;
  }

  // If the first argument is not empty, try to find a subcommand that matches
  if (!firstArg.empty()) {
    for (const auto& cmd : it->second.commands()) {
      // Check subcommand name
      if (cmd.name() != firstArg) {
        continue;
      }

      // Check if the subcommand requires arguments
      if (cmd.arguments().size() > 1 && parts.size() < 2) {
        _printCommandHelp(it->second);
        return;
      }

      // Command found, remove the subcommand from the arguments
      arguments = OpenShock::StringTrim(arguments.substr(firstArg.size()));

      // Execute the subcommand
      cmd.commandHandler()(arguments, isAutomated);
      return;
    }
  }

  // If no subcommand was found, try to find a default command
  for (const auto& cmd : it->second.commands()) {
    // Skip subcommands
    if (!cmd.name().empty()) {
      continue;
    }

    // Check if the command requires arguments
    if (cmd.arguments().size() > 0 && arguments.empty()) {
      _printCommandHelp(it->second);
      return;
    }

    // Execute the default command
    cmd.commandHandler()(arguments, isAutomated);
    return;
  }

  SERPR_ERROR("Command \"%.*s\" not found", command.size(), command.data());
}

void _serialRxTask(void*)
{
  SerialBuffer buffer(32);

  while (true) {
    switch (_tryReadSerialLine(buffer)) {
      case SerialReadResult::LineEnd:
        _processSerialLine(buffer);

        // Deallocate memory if the buffer is too large
        if (buffer.capacity() > SERIAL_BUFFER_CLEAR_THRESHOLD) {
          buffer.destroy();
        } else {
          buffer.clear();
        }

        // Skip any remaining trailing whitespaces
        _skipSerialWhitespaces(buffer);
        break;
      case SerialReadResult::AutoCompleteRequest:
        printf(CLEAR_LINE "> %.*s [AutoComplete is not implemented]", buffer.size(), buffer.data());
        break;
      case SerialReadResult::Data:
        _echoHandleSerialInput(buffer, true);
        break;
      default:
        _echoHandleSerialInput(buffer, false);
        break;
    }

    vTaskDelay(pdMS_TO_TICKS(20));  // 50 Hz update rate
  }
}

bool SerialInputHandler::Init()
{
  static bool s_initialized = false;
  if (s_initialized) {
    OS_LOGW(TAG, "Serial input handler already initialized");
    return false;
  }
  s_initialized = true;

  // Register command handlers
  s_commandGroups = OpenShock::Serial::CommandHandlers::AllCommandHandlers();
  for (const auto& handler : s_commandGroups) {
    OS_LOGV(TAG, "Registering command handler: %.*s", handler.name().size(), handler.name().data());
    RegisterCommandHandler(handler);
  }

  SerialInputHandler::PrintWelcomeHeader();
  SerialInputHandler::PrintVersionInfo();
  putchar('\n');

  if (!Config::GetSerialInputConfigEchoEnabled(s_echoEnabled)) {
    OS_LOGE(TAG, "Failed to get serial echo status from config");
    return false;
  }

  if (TaskUtils::TaskCreateExpensive(_serialRxTask, "SerialRX", 10'000, nullptr, 1, nullptr) != pdPASS) {  // TODO: Profile stack size
    OS_LOGE(TAG, "Failed to create serial RX task");
    return false;
  }

  return true;
}
bool SerialInputHandler::SerialEchoEnabled()
{
  return s_echoEnabled;
}

void SerialInputHandler::SetSerialEchoEnabled(bool enabled)
{
  s_echoEnabled = enabled;
}

void SerialInputHandler::PrintWelcomeHeader()
{
  ::Serial.println("\
============== OPENSHOCK ==============\r\n\
  Contribute @ github.com/OpenShock\r\n\
  Discuss    @ discord.gg/OpenShock\r\n\
  Type 'help' for available commands\r\n\
=======================================\r\n\
");
}

void SerialInputHandler::PrintVersionInfo()
{
  ::Serial.print("\
  Version:  " OPENSHOCK_FW_VERSION "\r\n\
    Build:  " OPENSHOCK_FW_MODE "\r\n\
   Commit:  " OPENSHOCK_FW_GIT_COMMIT "\r\n\
    Board:  " OPENSHOCK_FW_BOARD "\r\n\
     Chip:  " OPENSHOCK_FW_CHIP "\r\n\
");
}
