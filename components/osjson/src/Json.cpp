// Compile the jsmn implementation here (and only here): include <jsmn.h>
// WITHOUT JSMN_HEADER, before any header that pulls it in declaration-only.
#include <jsmn.h>

#include "json/Json.h"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace OpenShock;

// JSON-escape a string per RFC 8259 section 7. The grammar requires escaping
// exactly the quotation mark (U+0022), reverse solidus (U+005C) and every
// control character U+0000-U+001F; everything else is `unescaped`
// (%x20-21 / %x23-5B / %x5D-10FFFF) and passes through byte-for-byte.
//
//   quote (") and backslash -> \" and \\ (two-char escapes)
//   0x08 0x0C 0x0A 0x0D 0x09 -> \b \f \n \r \t   (the RFC's short forms)
//   other 0x00-0x1F          -> \u00XX
//   >= 0x20 (incl. 0x7F/DEL and UTF-8 multibyte) -> passthrough
//
// The solidus '/' is NOT escaped (the RFC lists \/ as permitted, not required).
// Non-ASCII is emitted as raw UTF-8, so no \u surrogate pairs are ever produced.
// Output validity therefore assumes valid UTF-8 input - the escaper escapes, it
// does not transcode (same contract cJSON had).
static std::string jsonEscape(std::string_view in)
{
  std::string out;
  out.reserve(in.size() + 8);

  for (unsigned char c : in) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20) {
          char buf[7];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
        break;
    }
  }

  return out;
}

int JSON::objSetString(json_gen_str_t* gen, const char* name, std::string_view value)
{
  std::string escaped = jsonEscape(value);
  return json_gen_obj_set_string(gen, name, escaped.c_str());
}

int JSON::arrSetString(json_gen_str_t* gen, std::string_view value)
{
  std::string escaped = jsonEscape(value);
  return json_gen_arr_set_string(gen, escaped.c_str());
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

int JSON::JsonView::skip(int index) const noexcept
{
  const jsmntok_t& tok = m_tokens[index];

  if (tok.type == JSMN_OBJECT) {
    int j = index + 1;
    for (int m = 0; m < tok.size; ++m) {
      j = skip(j);  // key
      j = skip(j);  // value
    }
    return j;
  }

  if (tok.type == JSMN_ARRAY) {
    int j = index + 1;
    for (int e = 0; e < tok.size; ++e) {
      j = skip(j);
    }
    return j;
  }

  return index + 1;  // string / primitive leaf
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

bool JSON::JsonView::tryGetStr(std::string_view& out) const noexcept
{
  if (!isString()) {
    return false;
  }
  out = raw();
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

bool JSON::JsonView::tryGetI64(int64_t& out) const noexcept
{
  if (!isNumber()) {
    return false;
  }
  std::string_view s = raw();
  int64_t value      = 0;
  auto [ptr, ec]     = std::from_chars(s.data(), s.data() + s.size(), value);
  if (ec != std::errc() || ptr != s.data() + s.size()) {
    return false;
  }
  out = value;
  return true;
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

JSON::JsonView JSON::JsonView::operator[](std::string_view key) const noexcept
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
