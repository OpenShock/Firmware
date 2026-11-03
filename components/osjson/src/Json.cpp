// Compile the jsmn implementation here (and only here): include <jsmn.h>
// WITHOUT JSMN_HEADER, before any header that pulls it in declaration-only.
#include <jsmn.h>

#include "json/Json.h"

#include "Convert.h"

#include <cstdlib>
#include <cstring>
#include <utility>

using namespace OpenShock;

// json_generator 2.x escapes string values itself (RFC 8259 section 7: quote,
// backslash and U+0000-U+001F) and rejects malformed UTF-8, so these only hand the
// value over with an explicit length. Escaping here as well would escape twice.
// The length also lets an embedded NUL through (emitted as \u0000).
int JSON::objSetString(json_gen_str_t* gen, const char* name, std::string_view value)
{
  return json_gen_obj_set_string_len(gen, name, value.data(), value.size());
}

int JSON::arrSetString(json_gen_str_t* gen, std::string_view value)
{
  return json_gen_arr_set_string_len(gen, value.data(), value.size());
}

// Parses the 4 hex digits of a \uXXXX escape starting at in[pos].
static bool readHex4(std::string_view in, std::size_t pos, uint32_t& out)
{
  if (pos > in.size() || in.size() - pos < 4) {
    return false;
  }

  uint32_t value = 0;
  for (std::size_t i = pos; i < pos + 4; ++i) {
    const char c = in[i];
    value <<= 4;
    if (c >= '0' && c <= '9') {
      value |= static_cast<uint32_t>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      value |= static_cast<uint32_t>(c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
      value |= static_cast<uint32_t>(c - 'A' + 10);
    } else {
      return false;
    }
  }

  out = value;
  return true;
}

namespace {
  // Where jsonUnescape writes: a growing std::string, or a fixed caller-owned buffer that refuses to overflow.
  struct StringSink {
    std::string& out;
    bool put(char c)
    {
      out += c;
      return true;
    }
  };
  struct SpanSink {
    std::span<char> out;
    std::size_t len = 0;
    bool put(char c)
    {
      if (len == out.size()) {
        return false;
      }
      out[len++] = c;
      return true;
    }
  };
}  // namespace

template<typename Sink>
static bool appendUtf8(Sink& out, uint32_t cp)
{
  if (cp < 0x80) {
    return out.put(static_cast<char>(cp));
  }
  if (cp < 0x800) {
    return out.put(static_cast<char>(0xC0 | (cp >> 6))) && out.put(static_cast<char>(0x80 | (cp & 0x3F)));
  }
  if (cp < 0x10000) {
    return out.put(static_cast<char>(0xE0 | (cp >> 12))) && out.put(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))) && out.put(static_cast<char>(0x80 | (cp & 0x3F)));
  }
  return out.put(static_cast<char>(0xF0 | (cp >> 18))) && out.put(static_cast<char>(0x80 | ((cp >> 12) & 0x3F))) && out.put(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))) && out.put(static_cast<char>(0x80 | (cp & 0x3F)));
}

// Decodes the body of a JSON string token (RFC 8259 section 7) to UTF-8:
//
//   \" \\ \/ \b \f \n \r \t -> the character they stand for
//   \uXXXX                  -> that code point, UTF-8 encoded (\u0000 gives a NUL byte)
//   \uD8xx\uDCxx            -> one supplementary code point (surrogate pair)
//   unpaired surrogate      -> U+FFFD (a lone surrogate has no UTF-8 encoding)
//
// All other bytes are copied unchanged. Returns false on a malformed escape (unknown
// escape character, truncated or non-hex \u), which jsmn's default (non-strict)
// mode does not reject, and when the sink is full.
template<typename Sink>
static bool jsonUnescape(std::string_view in, Sink& out)
{
  std::size_t i = 0;
  while (i < in.size()) {
    char c = in[i++];
    if (c != '\\') {
      if (!out.put(c)) {
        return false;
      }
      continue;
    }

    if (i >= in.size()) {
      return false;
    }

    c = in[i++];
    switch (c) {
      case '"':
      case '\\':
      case '/':
        if (!out.put(c)) {
          return false;
        }
        break;
      case 'b':
        if (!out.put('\b')) {
          return false;
        }
        break;
      case 'f':
        if (!out.put('\f')) {
          return false;
        }
        break;
      case 'n':
        if (!out.put('\n')) {
          return false;
        }
        break;
      case 'r':
        if (!out.put('\r')) {
          return false;
        }
        break;
      case 't':
        if (!out.put('\t')) {
          return false;
        }
        break;
      case 'u':
      {
        uint32_t cp;
        if (!readHex4(in, i, cp)) {
          return false;
        }
        i += 4;

        if (cp >= 0xD800 && cp <= 0xDBFF) {
          // High surrogate: forms a code point only together with a following \uDC00-\uDFFF.
          uint32_t low;
          if (in.size() - i >= 6 && in[i] == '\\' && in[i + 1] == 'u' && readHex4(in, i + 2, low) && low >= 0xDC00 && low <= 0xDFFF) {
            i += 6;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
          } else {
            cp = 0xFFFD;
          }
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
          cp = 0xFFFD;
        }

        if (!appendUtf8(out, cp)) {
          return false;
        }
        break;
      }
      default:
        return false;
    }
  }

  return true;
}

void JSON::objBegin(json_gen_str_t* gen, const char* name)
{
  if (name != nullptr) {
    json_gen_push_object(gen, name);
  } else {
    json_gen_start_object(gen);
  }
}

void JSON::objEnd(json_gen_str_t* gen, const char* name)
{
  if (name != nullptr) {
    json_gen_pop_object(gen);
  } else {
    json_gen_end_object(gen);
  }
}

// Iterative rather than recursive: input can nest thousands of levels deep (parse
// accepts up to 16384 tokens), which would overflow a task stack one frame per level.
int JSON::JsonView::skip(int index) const noexcept
{
  // Tokens still to consume in this subtree: an object owes a key and a value per
  // member, an array one value per element, a leaf nothing.
  long pending = 1;
  int j        = index;
  while (pending > 0 && j < m_count) {
    const jsmntok_t& tok = m_tokens[j++];
    --pending;
    if (tok.type == JSMN_OBJECT) {
      pending += 2L * tok.size;
    } else if (tok.type == JSMN_ARRAY) {
      pending += tok.size;
    }
  }
  return j;
}

std::string_view JSON::JsonView::raw() const noexcept
{
  if (!valid()) {
    return {};
  }
  const jsmntok_t& tok = m_tokens[m_index];
  if (tok.end < tok.start) {
    return {};
  }
  return std::string_view(m_json + tok.start, static_cast<size_t>(tok.end - tok.start));
}

bool JSON::JsonView::isNull() const noexcept
{
  return isPrimitive() && raw() == "null";
}

bool JSON::JsonView::isNumber() const noexcept
{
  if (!isPrimitive()) {
    return false;
  }
  std::string_view s = raw();
  return s != "true" && s != "false" && s != "null";
}

std::optional<std::string> JSON::JsonView::tryGetStr() const
{
  if (!isString()) {
    return std::nullopt;
  }

  std::string_view s = raw();
  if (s.find('\\') == std::string_view::npos) {
    return std::string(s);  // nothing to decode
  }

  std::string decoded;
  decoded.reserve(s.size());  // decoding never grows the text
  StringSink sink {decoded};
  if (!jsonUnescape(s, sink)) {
    return std::nullopt;
  }
  return decoded;
}

std::optional<std::string_view> JSON::JsonView::tryGetStrView(std::span<char> scratch) const noexcept
{
  if (!isString()) {
    return std::nullopt;
  }

  std::string_view s = raw();
  if (s.find('\\') == std::string_view::npos) {
    return s;  // nothing to decode: a view straight into the source
  }

  SpanSink sink {scratch};
  if (!jsonUnescape(s, sink)) {
    return std::nullopt;
  }
  return std::string_view(scratch.data(), sink.len);
}

std::optional<bool> JSON::JsonView::tryGetBool() const noexcept
{
  if (!isPrimitive()) {
    return std::nullopt;
  }
  std::string_view s = raw();
  if (s == "true") {
    return true;
  }
  if (s == "false") {
    return false;
  }
  return std::nullopt;
}

std::optional<uint8_t> JSON::JsonView::tryGetU8() const noexcept
{
  if (!isNumber()) {
    return std::nullopt;
  }
  return Convert::ToUint8(raw());
}

std::optional<uint16_t> JSON::JsonView::tryGetU16() const noexcept
{
  if (!isNumber()) {
    return std::nullopt;
  }
  return Convert::ToUint16(raw());
}

std::optional<int32_t> JSON::JsonView::tryGetI32() const noexcept
{
  if (!isNumber()) {
    return std::nullopt;
  }
  return Convert::ToInt32(raw());
}

std::optional<int64_t> JSON::JsonView::tryGetI64() const noexcept
{
  if (!isNumber()) {
    return std::nullopt;
  }
  return Convert::ToInt64(raw());
}

std::optional<double> JSON::JsonView::tryGetDouble() const noexcept
{
  if (!isNumber()) {
    return std::nullopt;
  }
  std::string_view s = raw();

  // strtod needs a NUL-terminated string; JSON numbers are short, so a small
  // stack copy is fine and avoids touching the (non-terminated) source buffer.
  char buf[64];
  if (s.size() >= sizeof(buf)) {
    return std::nullopt;
  }
  std::memcpy(buf, s.data(), s.size());
  buf[s.size()] = '\0';

  char* end    = nullptr;
  double value = std::strtod(buf, &end);
  if (end != buf + s.size()) {
    return std::nullopt;
  }
  return value;
}

JSON::JsonView JSON::JsonView::operator[](std::string_view key) const
{
  if (!isObject()) {
    return {};
  }

  const int members = m_tokens[m_index].size;
  int j             = m_index + 1;
  for (int m = 0; m < members; ++m) {
    const int keyIdx = j;
    const int valIdx = keyIdx + 1;

    const jsmntok_t& k = m_tokens[keyIdx];
    if (k.type == JSMN_STRING) {
      std::string_view keyView(m_json + k.start, static_cast<size_t>(k.end - k.start));
      if (keyView == key) {
        return JsonView(m_json, m_tokens, m_count, valIdx);
      }

      // A key written with escapes ("\u0069d" for "id") is still the same key. Escaped keys are rare enough that
      // decoding one into a string is fine.
      if (keyView.find('\\') != std::string_view::npos) {
        std::string decodedKey;
        StringSink sink {decodedKey};
        if (jsonUnescape(keyView, sink) && decodedKey == key) {
          return JsonView(m_json, m_tokens, m_count, valIdx);
        }
      }
    }

    j = skip(valIdx);  // advance past this member's value to the next key
  }

  return {};
}

int JSON::JsonView::count() const noexcept
{
  if (!isObject() && !isArray()) {
    return 0;
  }
  return m_tokens[m_index].size;
}

JSON::JsonView JSON::JsonView::at(int index) const noexcept
{
  if (!isArray()) {
    return {};
  }

  const int elements = m_tokens[m_index].size;
  if (index < 0 || index >= elements) {
    return {};
  }

  int j = m_index + 1;
  for (int e = 0; e < index; ++e) {
    j = skip(j);
  }
  return JsonView(m_json, m_tokens, m_count, j);
}

bool JSON::JsonDocument::parse(std::string_view json)
{
  m_json = json;
  m_ok   = false;

  // Count tokens first (jsmn does this without a buffer), then allocate exactly once. Growing by doubling briefly
  // held both the old and the new buffer, which for a large untrusted body meant ~384 KiB with exceptions disabled.
  static constexpr int kMaxTokens = 16384;

  jsmn_parser parser;
  jsmn_init(&parser);
  int count = jsmn_parse(&parser, json.data(), json.size(), nullptr, 0);
  if (count <= 0 || count > kMaxTokens) {
    m_tokens.clear();
    return false;  // malformed, empty, or unreasonably large
  }

  m_tokens.resize(static_cast<size_t>(count));

  jsmn_init(&parser);
  int result = jsmn_parse(&parser, json.data(), json.size(), m_tokens.data(), static_cast<unsigned int>(count));
  if (result != count) {
    m_tokens.clear();
    return false;
  }

  m_ok = true;
  return true;
}

JSON::JsonView JSON::JsonDocument::root() const noexcept
{
  if (!m_ok || m_tokens.empty()) {
    return {};
  }
  return JsonView(m_json.data(), m_tokens.data(), static_cast<int>(m_tokens.size()), 0);
}

void JSON::StringWriter::flushCb(char* buf, void* priv)
{
  // json_generator flushes with free_ptr on the terminator it just wrote: the chunk's length, without a strlen
  auto* self = static_cast<StringWriter*>(priv);
  self->m_out.append(buf, static_cast<std::size_t>(self->m_gen.free_ptr - buf));
}

JSON::StringWriter::StringWriter()
{
  json_gen_str_start(&m_gen, m_buf, sizeof(m_buf), &StringWriter::flushCb, this);
}

std::string JSON::StringWriter::finish()
{
  // On error (e.g. malformed UTF-8 in a value) json_generator stops generating, but chunks flushed before the error
  // are already in m_out; never hand out that truncated, invalid document.
  int result = json_gen_str_end(&m_gen);
  if (result < 0) {
    m_failed = true;
    m_out.clear();
    return {};
  }

  return std::move(m_out);
}
