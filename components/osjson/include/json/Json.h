#pragma once

// jsmn is header-only: the parser implementation is compiled exactly once, in
// Json.cpp (which includes <jsmn.h> *without* JSMN_HEADER before this header).
// Everywhere else we only want the type/enum declarations.
#ifndef JSMN_HEADER
#define JSMN_HEADER
#endif
#include <jsmn.h>

#include <json_generator.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace OpenShock::JSON {
  // Zero-copy, read-only view over a single node of a parsed jsmn token tree.
  //
  // Strings and numbers are exposed as std::string_view directly into the
  // source buffer (no NUL-termination, no strlen). The source buffer must
  // outlive every JsonView derived from it.
  class JsonView {
  public:
    JsonView() noexcept = default;
    JsonView(const char* json, const jsmntok_t* tokens, int count, int index) noexcept
      : m_json(json)
      , m_tokens(tokens)
      , m_count(count)
      , m_index(index)
    {
    }

    [[nodiscard]] bool valid() const noexcept { return m_tokens != nullptr && m_index >= 0 && m_index < m_count; }
    [[nodiscard]] bool isObject() const noexcept { return valid() && m_tokens[m_index].type == JSMN_OBJECT; }
    [[nodiscard]] bool isArray() const noexcept { return valid() && m_tokens[m_index].type == JSMN_ARRAY; }
    [[nodiscard]] bool isString() const noexcept { return valid() && m_tokens[m_index].type == JSMN_STRING; }
    [[nodiscard]] bool isPrimitive() const noexcept { return valid() && m_tokens[m_index].type == JSMN_PRIMITIVE; }
    [[nodiscard]] bool isNull() const noexcept;
    [[nodiscard]] bool isNumber() const noexcept;  // primitive that is not true/false/null

    // Raw token text, zero-copy into the source buffer.
    [[nodiscard]] std::string_view raw() const noexcept;

    // Typed getters. Return false if this node is not of the requested kind.
    [[nodiscard]] bool tryGetStr(std::string_view& out) const noexcept;
    [[nodiscard]] bool tryGetBool(bool& out) const noexcept;
    [[nodiscard]] bool tryGetI64(int64_t& out) const noexcept;
    [[nodiscard]] bool tryGetDouble(double& out) const noexcept;

    // Object member lookup. Returns an invalid view if not found or not an object.
    [[nodiscard]] JsonView operator[](std::string_view key) const noexcept;

    // Array access (also reports object member count).
    [[nodiscard]] int count() const noexcept;
    [[nodiscard]] JsonView at(int index) const noexcept;

  private:
    [[nodiscard]] int skip(int index) const noexcept;  // index just past the subtree rooted at `index`

    const char* m_json        = nullptr;
    const jsmntok_t* m_tokens = nullptr;
    int m_count               = 0;
    int m_index               = -1;
  };

  // Owns the jsmn token array for a parsed document. Does NOT own the JSON text;
  // the buffer passed to parse() must outlive the document and its views.
  class JsonDocument {
  public:
    [[nodiscard]] bool parse(std::string_view json);
    [[nodiscard]] bool ok() const noexcept { return m_ok; }
    [[nodiscard]] JsonView root() const noexcept;

  private:
    std::string_view m_json;
    std::vector<jsmntok_t> m_tokens;
    bool m_ok = false;
  };

  // json_generator wrapper that accumulates output into a std::string.
  // Non-copyable/non-movable (the generator holds a pointer to the inline buffer).
  class StringWriter {
  public:
    StringWriter();
    StringWriter(const StringWriter&)            = delete;
    StringWriter& operator=(const StringWriter&) = delete;

    [[nodiscard]] json_gen_str_t* gen() noexcept { return &m_gen; }

    // Finalizes the JSON and returns the accumulated string. Call once.
    [[nodiscard]] std::string finish();

  private:
    static void flushCb(char* buf, void* priv);

    std::string m_out;
    char m_buf[256];
    json_gen_str_t m_gen;
  };

  // json_generator writes string VALUES verbatim between quotes - it does NOT
  // JSON-escape them, so a value containing '"', '\\' or a control character
  // would emit invalid JSON. These wrappers escape the value first, then hand it
  // to json_gen_{obj,arr}_set_string (which only adds the surrounding quotes).
  //
  // Keys/names are NOT escaped here (all our keys are safe literals); pass a
  // pre-escaped name if that ever changes. Return value matches the underlying
  // json_gen_* call.
  int objSetString(json_gen_str_t* gen, const char* name, std::string_view value);
  int arrSetString(json_gen_str_t* gen, std::string_view value);

  // Opens/closes an object on the generator. When `name` is non-null the object
  // is added as a named member of the enclosing object (push/pop); when it is
  // null the object is anonymous (start/end), e.g. the document root or an array
  // element. Config ToJSON() implementations wrap their members in these so each
  // config owns its own object, mirroring the source firmware's cJSON approach.
  void objBegin(json_gen_str_t* gen, const char* name);
  void objEnd(json_gen_str_t* gen, const char* name);
}  // namespace OpenShock::JSON
