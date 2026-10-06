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

static void appendUtf8(std::string& out, uint32_t cp)
{
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
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
// mode does not reject.
static bool jsonUnescape(std::string_view in, std::string& out)
{
  out.clear();
  out.reserve(in.size());

  std::size_t i = 0;
  while (i < in.size()) {
    char c = in[i++];
    if (c != '\\') {
      out += c;
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
        out += c;
        break;
      case 'b':
        out += '\b';
        break;
      case 'f':
        out += '\f';
        break;
      case 'n':
        out += '\n';
        break;
      case 'r':
        out += '\r';
        break;
      case 't':
        out += '\t';
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

        appendUtf8(out, cp);
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

bool JSON::JsonView::tryGetStr(std::string& out) const
{
  if (!isString()) {
    return false;
  }

  std::string_view s = raw();
  if (s.find('\\') == std::string_view::npos) {
    out.assign(s);  // nothing to decode
    return true;
  }

  std::string decoded;
  if (!jsonUnescape(s, decoded)) {
    return false;
  }
  out = std::move(decoded);
  return true;
}

bool JSON::JsonView::tryGetBool(bool& out) const noexcept
{
  if (!isPrimitive()) {
    return false;
  }
  std::string_view s = raw();
  if (s == "true") {
    out = true;
    return true;
  }
  if (s == "false") {
    out = false;
    return true;
  }
  return false;
}

bool JSON::JsonView::tryGetU8(uint8_t& out) const noexcept
{
  if (!isNumber()) {
    return false;
  }
  return Convert::ToUint8(raw(), out);
}

bool JSON::JsonView::tryGetU16(uint16_t& out) const noexcept
{
  if (!isNumber()) {
    return false;
  }
  return Convert::ToUint16(raw(), out);
}

bool JSON::JsonView::tryGetI32(int32_t& out) const noexcept
{
  if (!isNumber()) {
    return false;
  }
  return Convert::ToInt32(raw(), out);
}

bool JSON::JsonView::tryGetI64(int64_t& out) const noexcept
{
  if (!isNumber()) {
    return false;
  }
  return Convert::ToInt64(raw(), out);
}

bool JSON::JsonView::tryGetDouble(double& out) const noexcept
{
  if (!isNumber()) {
    return false;
  }
  std::string_view s = raw();

  // strtod needs a NUL-terminated string; JSON numbers are short, so a small
  // stack copy is fine and avoids touching the (non-terminated) source buffer.
  char buf[64];
  if (s.size() >= sizeof(buf)) {
    return false;
  }
  std::memcpy(buf, s.data(), s.size());
  buf[s.size()] = '\0';

  char* end    = nullptr;
  double value = std::strtod(buf, &end);
  if (end != buf + s.size()) {
    return false;
  }
  out = value;
  return true;
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

      // A key written with escapes ("id" for "id") is still the same key.
      std::string decodedKey;
      if (keyView.find('\\') != std::string_view::npos && jsonUnescape(keyView, decodedKey) && decodedKey == key) {
        return JsonView(m_json, m_tokens, m_count, valIdx);
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

  // jsmn needs a token buffer sized up-front; grow-and-retry on NOMEM rather
  // than relying on a separate counting pass.
  size_t capacity = 32;
  for (;;) {
    m_tokens.resize(capacity);

    jsmn_parser parser;
    jsmn_init(&parser);

    int result = jsmn_parse(&parser, json.data(), json.size(), m_tokens.data(), static_cast<unsigned int>(capacity));
    if (result >= 0) {
      m_tokens.resize(static_cast<size_t>(result));
      m_ok = result > 0;
      return m_ok;
    }

    if (result == JSMN_ERROR_NOMEM) {
      capacity *= 2;
      if (capacity > 16384) {
        return false;  // unreasonably large / malformed
      }
      continue;
    }

    return false;  // JSMN_ERROR_INVAL / JSMN_ERROR_PART
  }
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
  static_cast<std::string*>(priv)->append(buf);
}

JSON::StringWriter::StringWriter()
{
  json_gen_str_start(&m_gen, m_buf, sizeof(m_buf), &StringWriter::flushCb, &m_out);
}

std::string JSON::StringWriter::finish()
{
  json_gen_str_end(&m_gen);
  return std::move(m_out);
}
