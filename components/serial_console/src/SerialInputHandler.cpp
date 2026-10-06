#include "serial_console/SerialInputHandler.h"

const char* const TAG = "SerialInputHandler";

#include "Base64.h"
#include "Chipset.h"
#include "CommandHandler.h"
#include "config/Config.h"
#include "config/SerialInputConfig.h"
#include "Convert.h"
#include "estop/EStopManager.h"
#include "FormatHelpers.h"
#include "http/HTTPRequestManager.h"
#include "Logging.h"
#include "serial/Serial.h"
#include "serial_console/command_handlers/CommandEntry.h"
#include "serial_console/command_handlers/common.h"
#include "serial_console/command_handlers/index.h"
#include "serialization/JsonAPI.h"
#include "serialization/JsonSerial.h"
#include "StringHelpers.h"
#include "Temporal.h"
#include "util/TaskUtils.h"
#include "wifi/WiFiManager.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <unordered_map>

namespace {
  // Case-insensitive hash/equality for the command lookup table.
  struct hash_ci {
    std::size_t operator()(std::string_view str) const noexcept
    {
      std::size_t hash = 7;

      for (char c : str) {
        hash = hash * 31 + tolower(static_cast<unsigned char>(c));
      }

      return hash;
    }
  };

  struct equals_ci {
    bool operator()(std::string_view a, std::string_view b) const { return OpenShock::StringIEquals(a, b); }
  };
}  // namespace

using namespace std::string_view_literals;

using namespace OpenShock;

const int64_t PASTE_INTERVAL_THRESHOLD_MS    = 20;
const std::size_t SERIAL_BUFFER_MAX_CAPACITY = 4096;

static bool s_echoEnabled = true;
static std::vector<OpenShock::SerialCmds::CommandGroup> s_commandGroups;
static std::unordered_map<std::string_view, OpenShock::SerialCmds::CommandGroup, hash_ci, equals_ci> s_commandHandlers;

static void printCompleteHelp()
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

  OS_SERIAL_PRINT(buffer);
}

static void printCommandHelp(SerialCmds::CommandGroup& group)
{
  std::size_t size = 0;
  for (const auto& command : group.commands()) {
    size += 2;  // +2 for newline
    size += group.name().size();
    size++;     // +1 for space

    if (command.name().size() > 0) {
      size += command.name().size() + 1;  // +1 for space
    }

    for (const auto& arg : command.arguments()) {
      size += arg.name.size() + 3;  // +1 for space, +2 for <>
    }

    size += 2;  // +2 for newline

    if (command.description().size() > 0) {
      size += command.description().size() + 4;  // +2 for indent, +2 for newline
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

  OS_SERIAL_PRINT(buffer);
}

static void handleHelpCommand(std::string_view arg, bool isAutomated)
{
  arg = OpenShock::StringTrim(arg);
  if (arg.empty()) {
    printCompleteHelp();
    return;
  }

  // Get help for a specific command
  auto it = s_commandHandlers.find(arg);
  if (it != s_commandHandlers.end()) {
    printCommandHelp(it->second);
    return;
  }

  SERPR_ERROR("Command \"%.*s\" not found", static_cast<int>(arg.length()), arg.data());
}

void RegisterCommandHandler(const OpenShock::SerialCmds::CommandGroup& handler)
{
  s_commandHandlers[handler.name()] = handler;
}

#define CLEAR_LINE "\r\x1B[K"

class SerialBuffer {
  DISABLE_COPY(SerialBuffer);
  DISABLE_MOVE(SerialBuffer);

public:
  SerialBuffer()
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

  constexpr const char* data() const noexcept { return m_data == nullptr ? "" : m_data; }
  constexpr std::size_t size() const noexcept { return m_size; }
  constexpr std::size_t capacity() const noexcept { return m_capacity; }
  constexpr bool empty() const noexcept { return m_size == 0; }

  // Set when a line outgrew SERIAL_BUFFER_MAX_CAPACITY; the rest of that line is discarded and it is never executed.
  constexpr bool overflowed() const noexcept { return m_overflowed; }

  constexpr void clear() noexcept
  {
    m_size       = 0;
    m_overflowed = false;
  }

  inline void reserve(std::size_t size)
  {
    size = (size + 31) & ~31;  // Align to 32 bytes

    if (size > SERIAL_BUFFER_MAX_CAPACITY) {
      OS_LOGE(TAG, "Line exceeds %zu bytes, discarding it", SERIAL_BUFFER_MAX_CAPACITY);
      m_size       = 0;
      m_overflowed = true;
      return;
    }
    if (size <= m_capacity) {
      return;
    }

    char* newData = new char[size];
    if (m_data != nullptr) {
      std::memcpy(newData, m_data, m_size);
      delete[] m_data;
    }

    m_data                 = newData;
    m_capacity             = size;
    m_data[m_capacity - 1] = 0;
  }

  inline void append(const char* data, std::size_t len)
  {
    if (len == 0 || m_overflowed) {
      return;
    }

    if (m_size + len > m_capacity) {
      reserve(m_size + len);
      if (m_overflowed) {
        return;
      }
    }

    std::memcpy(m_data + m_size, data, len);
    m_size += len;
  }

  constexpr void pop_back() noexcept
  {
    if (m_size > 0) {
      --m_size;
    }
  }

  constexpr operator std::string_view() const { return std::string_view(data(), m_size); }

private:
  char* m_data;
  std::size_t m_size;
  std::size_t m_capacity;
  bool m_overflowed = false;
};

enum class SerialReadResult {
  NoData,
  Data,
  LineEnd,
  AutoCompleteRequest,
};

// Staging buffer: the console is read in chunks (Serial::Read returns the
// number of bytes read) into this buffer, and the parser scans the chunk in
// place. Bytes left over after an early return (line end / autocomplete) are
// carried across polls via s_rxStagingHead.
static uint8_t s_rxStaging[128];
static std::size_t s_rxStagingHead = 0;
static std::size_t s_rxStagingLen  = 0;

// Ensures the staging buffer holds unconsumed bytes, refilling from the console
// when it has been drained. Returns false if no bytes are available.
static bool fillRxStaging()
{
  if (s_rxStagingHead < s_rxStagingLen) {
    return true;
  }

  int read = Serial::Read(s_rxStaging, sizeof(s_rxStaging));
  if (read <= 0) {
    return false;
  }

  s_rxStagingHead = 0;
  s_rxStagingLen  = static_cast<std::size_t>(read);
  return true;
}

static SerialReadResult tryReadSerialLine(SerialBuffer& buffer)
{
  bool gotData = false;

  while (fillRxStaging()) {
    gotData = true;

    const char* chunk     = reinterpret_cast<const char*>(s_rxStaging);
    const std::size_t end = s_rxStagingLen;
    std::size_t i         = s_rxStagingHead;

    while (i < end) {
      char c = chunk[i];

      // Backspace (BS, or DEL as most terminals send it): erase the last buffered character.
      if (c == '\b' || c == '\x7F') {
        buffer.pop_back();
        ++i;
        continue;
      }

      // Line end: dispatch the line, or swallow blank lines.
      if (c == '\r' || c == '\n') {
        ++i;
        if (!buffer.empty() || buffer.overflowed()) {
          s_rxStagingHead = i;
          return SerialReadResult::LineEnd;
        }
        continue;
      }

      // Tab: autocomplete request.
      if (c == '\t') {
        ++i;
        s_rxStagingHead = i;
        return SerialReadResult::AutoCompleteRequest;
      }

      // Strip leading whitespace.
      if (c == ' ' && buffer.empty()) {
        ++i;
        continue;
      }

      // Bulk-append the maximal run of printable characters. Bytes we don't
      // handle above (control codes, >= 0x7F) are dropped one at a time.
      std::size_t runStart = i;
      while (i < end) {
        char d = chunk[i];
        if (d <= 31 || d >= 127) {
          break;
        }
        ++i;
      }

      if (i > runStart) {
        buffer.append(chunk + runStart, i - runStart);
      } else {
        ++i;  // drop the unhandled byte
      }
    }

    s_rxStagingHead = i;  // chunk fully consumed
  }

  return gotData ? SerialReadResult::Data : SerialReadResult::NoData;
}

// Skips whitespace between lines; the first other byte is left in staging for tryReadSerialLine to filter.
static void skipSerialWhitespaces()
{
  while (fillRxStaging()) {
    const char* chunk     = reinterpret_cast<const char*>(s_rxStaging);
    const std::size_t end = s_rxStagingLen;
    std::size_t i         = s_rxStagingHead;

    while (i < end) {
      char c = chunk[i];
      if (c != ' ' && c != '\r' && c != '\n') {
        s_rxStagingHead = i;
        return;
      }
      ++i;
    }

    s_rxStagingHead = i;  // drained; loop refills
  }
}

static void echoBuffer(std::string_view buffer)
{
  OS_SERIAL_PRINTF(CLEAR_LINE "> %.*s", static_cast<int>(buffer.size()), buffer.data());
}

static void echoHandleSerialInput(std::string_view buffer, bool hasData)
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
    echoBuffer(buffer);
    hasChanges   = false;
    lastActivity = OpenShock::millis();
  }
}

static void processSerialLine(std::string_view line)
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
    echoBuffer(line);
    OS_SERIAL_PRINTLN();
  }

  // Line may be just "$" (or "$" plus whitespace)
  line = OpenShock::StringTrim(line);
  if (line.empty()) {
    return;
  }

  auto parts                 = OpenShock::StringSplit(line, ' ', 1);
  std::string_view command   = OpenShock::StringTrim(parts[0]);
  std::string_view arguments = parts.size() > 1 ? OpenShock::StringTrim(parts[1]) : std::string_view();

  if (command == "help"sv) {
    handleHelpCommand(arguments, isAutomated);
    return;
  }

  auto it = s_commandHandlers.find(command);
  if (it == s_commandHandlers.end()) {
    SERPR_ERROR("Command \"%.*s\" not found", static_cast<int>(command.size()), command.data());
    return;
  }

  // Get potential subcommand (arguments are trimmed, so the first token is never empty)
  std::string_view firstArg;
  std::string_view subArguments;
  if (!arguments.empty()) {
    parts        = OpenShock::StringSplit(arguments, ' ', 1);
    firstArg     = parts[0];
    subArguments = parts.size() > 1 ? OpenShock::StringTrim(parts[1]) : std::string_view();
  }

  // If the first argument is not empty, try to find a subcommand that matches
  if (!firstArg.empty()) {
    bool nameMatched = false;
    for (SerialCmds::CommandEntry& cmd : it->second.commands()) {
      // Check subcommand name
      if (cmd.name() != firstArg) {
        continue;
      }
      nameMatched = true;

      // A getter and a setter can share a name; skip variants that need arguments when none were given
      if (cmd.arguments().size() > 0 && subArguments.empty()) {
        continue;
      }

      // Execute the subcommand
      cmd.commandHandler()(subArguments, isAutomated);
      return;
    }

    if (nameMatched) {
      printCommandHelp(it->second);
      return;
    }
  }

  // If no subcommand was found, try to find a default command
  for (SerialCmds::CommandEntry& cmd : it->second.commands()) {
    // Skip subcommands
    if (!cmd.name().empty()) {
      continue;
    }

    // Check if the command requires arguments
    if (cmd.arguments().size() > 0 && arguments.empty()) {
      printCommandHelp(it->second);
      return;
    }

    // Execute the default command
    cmd.commandHandler()(arguments, isAutomated);
    return;
  }

  SERPR_ERROR("Command \"%.*s\" not found", static_cast<int>(command.size()), command.data());
}

static void serialRxTask(void*)
{
  SerialBuffer buffer(32);

  while (true) {
    switch (tryReadSerialLine(buffer)) {
      case SerialReadResult::LineEnd:
        if (buffer.overflowed()) {
          SERPR_ERROR("Line too long (max %zu bytes), ignored", SERIAL_BUFFER_MAX_CAPACITY);
        } else {
          processSerialLine(buffer);
        }

        buffer.clear();

        // Skip any remaining trailing whitespaces
        skipSerialWhitespaces();
        break;
      case SerialReadResult::AutoCompleteRequest:
        OS_SERIAL_PRINTF(CLEAR_LINE "> %.*s [AutoComplete is not implemented]", static_cast<int>(buffer.size()), buffer.data());
        break;
      case SerialReadResult::Data:
        echoHandleSerialInput(buffer, true);
        break;
      default:
        echoHandleSerialInput(buffer, false);
        break;
    }

    vTaskDelay(pdMS_TO_TICKS(20));  // 50 Hz update rate
  }
}

bool SerialInputHandler::Init()
{
  static bool s_initialized = false;
  if (s_initialized) {
    OS_LOGW(TAG, "SerialCmds input handler already initialized");
    return false;
  }
  s_initialized = true;

  // Install the console driver (RX here, and the raw TX path used by OS_SERIAL_*).
  if (!Serial::Init()) {
    OS_LOGE(TAG, "Failed to initialize serial console");
    return false;
  }

  // Register command handlers
  s_commandGroups = OpenShock::SerialCmds::CommandHandlers::AllCommandHandlers();
  for (const auto& handler : s_commandGroups) {
    OS_LOGV(TAG, "Registering command handler: %.*s", static_cast<int>(handler.name().size()), handler.name().data());
    RegisterCommandHandler(handler);
  }

  SerialInputHandler::PrintWelcomeHeader();
  SerialInputHandler::PrintVersionInfo();
  OS_SERIAL_PRINTLN();

  if (!Config::GetSerialInputConfigEchoEnabled(s_echoEnabled)) {
    OS_LOGE(TAG, "Failed to get serial echo status from config");
    return false;
  }

  if (TaskUtils::TaskCreateExpensive(serialRxTask, "SerialRX", 10'000, nullptr, 1, nullptr) != pdPASS) {  // TODO: Profile stack size
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

void SerialInputHandler::Print(std::string_view str)
{
  Serial::Write(reinterpret_cast<const uint8_t*>(str.data()), str.size());
}

// Formats the whole message first and hands it to Serial::Write in one call (as
// the logger does), so it is not interleaved with log output mid-line.
void SerialInputHandler::Printf(const char* format, ...)
{
  va_list args;
  va_start(args, format);
  Serial::VWritef(format, args);
  va_end(args);
}

void SerialInputHandler::PrintWelcomeHeader()
{
  OS_SERIAL_PRINTLN("\
============== OPENSHOCK ==============\r\n\
  Contribute @ github.com/OpenShock\r\n\
  Discuss    @ discord.gg/OpenShock\r\n\
  Type 'help' for available commands\r\n\
=======================================\r\n\
");
}

void SerialInputHandler::PrintVersionInfo()
{
  OS_SERIAL_PRINTF(
    "  Version:  %s\r\n"
    "    Build:  " OPENSHOCK_FW_MODE "\r\n"
    "   Commit:  %s\r\n"
    "    Board:  " OPENSHOCK_FW_BOARD "\r\n"
    "     Chip:  " OPENSHOCK_FW_CHIP "\r\n",
    OpenShock::Constants::FW_VERSION,
    OpenShock::Constants::FW_GIT_COMMIT
  );
}
