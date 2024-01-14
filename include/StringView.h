#pragma once

#include <Arduino.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace OpenShock {
  struct StringView {
    using value_type             = char;
    using const_iterator         = const value_type*;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    static const std::size_t npos = std::numeric_limits<std::size_t>::max();

    static constexpr StringView Null() { return StringView(nullptr); }
    static constexpr StringView Empty() { return StringView(""); }

    constexpr StringView() : _ptrBeg(nullptr), _ptrEnd(nullptr) { }
    constexpr StringView(const char* const ptr) : _ptrBeg(ptr), _ptrEnd(_getStringEnd(ptr)) { }
    constexpr StringView(const char* const ptr, std::size_t len) : _ptrBeg(ptr), _ptrEnd(ptr + len) { }
    constexpr StringView(const char* const ptrBeg, const char* const ptrEnd) : _ptrBeg(ptrBeg), _ptrEnd(ptrEnd) { }
    constexpr StringView(const StringView& str) : _ptrBeg(str._ptrBeg), _ptrEnd(str._ptrEnd) { }
    StringView(const String& str) : _ptrBeg(str.c_str()), _ptrEnd(str.c_str() + str.length()) { }
    StringView(const std::string& str) : _ptrBeg(str.c_str()), _ptrEnd(str.c_str() + str.size()) { }

    constexpr bool isNull() const { return _ptrBeg == nullptr || _ptrEnd == nullptr; }

    constexpr const char* data() const { return _ptrBeg; }

    constexpr const_iterator begin() const { return _ptrBeg; }
    const_reverse_iterator rbegin() const { return std::make_reverse_iterator(end()); }

    constexpr const_iterator end() const { return _ptrEnd; }
    const_reverse_iterator rend() const { return std::make_reverse_iterator(begin()); }

    constexpr char front() const { return *_ptrBeg; }
    constexpr char back() const { return *(_ptrEnd - 1); }

    constexpr std::size_t size() const {
      if (isNull()) return 0;

      return _ptrEnd - _ptrBeg;
    }
    constexpr std::size_t length() const { return size(); }

    constexpr bool isNullOrEmpty() const { return size() == 0; }

    constexpr StringView substr(std::size_t pos, std::size_t count = StringView::npos) const {
      if (isNullOrEmpty()) {
        return *this;
      }

      if (pos > size()) {
        return Null();
      }

      if (count == StringView::npos) {
        count = size() - pos;
      } else if (pos + count > size()) {
        return Null();
      }

      return StringView(_ptrBeg + pos, _ptrBeg + pos + count);
    }

    constexpr std::size_t find(char needle, std::size_t pos = 0) const {
      std::size_t _size = this->size();

      for (std::size_t i = pos; i < _size; ++i) {
        if (_ptrBeg[i] == needle) {
          return i;
        }
      }

      return StringView::npos;
    }
    std::size_t find(StringView needle, std::size_t pos = 0) const {
      if (isNullOrEmpty() || pos + needle.size() >= size()) {
        return StringView::npos;
      }

      const char* ptr = std::search(_ptrBeg + pos, _ptrEnd, needle._ptrBeg, needle._ptrEnd);
      if (ptr == _ptrEnd) {
        return StringView::npos;
      }

      return ptr - _ptrBeg;
    }

    std::size_t rfind(char needle, std::size_t pos = StringView::npos) const {
      std::size_t _size = this->size();

      if (pos == StringView::npos) {
        pos = _size - 1;
      } else if (pos >= _size) {
        return StringView::npos;
      }

      for (std::size_t i = pos; i > 0; --i) {
        if (_ptrBeg[i] == needle) {
          return i;
        }
      }

      return StringView::npos;
    }
    std::size_t rfind(StringView needle, std::size_t pos = StringView::npos) const {
      if (isNullOrEmpty()) {
        return StringView::npos;
      }

      if (pos == StringView::npos) {
        pos = size() - 1;
      } else if (pos + needle.size() >= size()) {
        return StringView::npos;
      }

      const char* ptr = std::find_end(_ptrBeg, _ptrBeg + pos, needle._ptrBeg, needle._ptrEnd);
      if (ptr == _ptrBeg + pos) {
        return StringView::npos;
      }

      return ptr - _ptrBeg;
    }

    StringView beforeDelimiter(char delimiter) const {
      std::size_t pos = find(delimiter);
      if (pos != StringView::npos) {
        return substr(0, pos);
      }

      return *this;
    }
    StringView beforeDelimiter(StringView delimiter) const {
      std::size_t pos = find(delimiter);
      if (pos != StringView::npos) {
        return substr(0, pos);
      }

      return *this;
    }

    StringView afterDelimiter(char delimiter) const {
      std::size_t pos = find(delimiter);
      if (pos != StringView::npos) {
        return substr(pos + 1);
      }

      return *this;
    }
    StringView afterDelimiter(StringView delimiter) const {
      std::size_t pos = find(delimiter);
      if (pos != StringView::npos) {
        return substr(pos + delimiter.size());
      }

      return *this;
    }

    std::vector<StringView> split(char delimiter) const {
      std::vector<StringView> result;

      std::size_t pos = 0;
      while (pos < size()) {
        std::size_t nextPos = find(delimiter, pos);
        if (nextPos == StringView::npos) {
          nextPos = size();
        }

        result.push_back(substr(pos, nextPos - pos));
        pos = nextPos + 1;
      }

      return result;
    }
    std::vector<StringView> split(StringView delimiter) const {
      std::vector<StringView> result;

      std::size_t pos = 0;
      while (pos < size()) {
        std::size_t nextPos = find(delimiter, pos);
        if (nextPos == StringView::npos) {
          nextPos = size();
        }

        result.push_back(substr(pos, nextPos - pos));
        pos = nextPos + delimiter.size();
      }

      return result;
    }
    std::vector<StringView> split(std::function<bool(char)> predicate) const {
      std::vector<StringView> result;

      const char* start = nullptr;
      for (const char* ptr = _ptrBeg; ptr < _ptrEnd; ++ptr) {
        if (predicate(*ptr)) {
          if (start != nullptr) {
            result.push_back(StringView(start, ptr));
            start = nullptr;
          }
        } else if (start == nullptr) {
          start = ptr;
        }
      }

      if (start != nullptr) {
        result.push_back(StringView(start, _ptrEnd));
      }

      return result;
    }

    std::vector<StringView> splitLines() const {
      return split([](char c) { return c == '\r' || c == '\n'; });
    }
    std::vector<StringView> splitWhitespace() const { return split(isspace); }

    constexpr bool startsWith(char needle) const {
      if (isNull()) {
        return false;
      }

      return _ptrBeg[0] == needle;
    }
    constexpr bool startsWith(StringView needle) const {
      if (isNull()) {
        return false;
      }

      return _strEquals(_ptrBeg, _ptrBeg + needle.size(), needle._ptrBeg, needle._ptrEnd);
    }

    constexpr bool endsWith(char needle) const {
      if (isNull()) {
        return false;
      }

      return _ptrEnd[-1] == needle;
    }
    constexpr bool endsWith(StringView needle) const {
      if (isNull()) {
        return false;
      }

      return _strEquals(_ptrEnd - needle.size(), _ptrEnd, needle._ptrBeg, needle._ptrEnd);
    }

    constexpr StringView& trimLeft() {
      if (isNull()) {
        return *this;
      }

      while (_ptrBeg < _ptrEnd && isspace(*_ptrBeg)) {
        ++_ptrBeg;
      }

      return *this;
    }

    constexpr StringView& trimRight() {
      if (isNull()) {
        return *this;
      }

      while (_ptrBeg < _ptrEnd && isspace(_ptrEnd[-1])) {
        --_ptrEnd;
      }

      return *this;
    }

    constexpr StringView& trim() {
      trimLeft();
      trimRight();
      return *this;
    }

    constexpr void clear() {
      _ptrBeg = nullptr;
      _ptrEnd = nullptr;
    }

    String toArduinoString() const {
      if (isNull()) {
        return String();
      }

      return String(_ptrBeg, size());
    }

    std::string toString() const {
      if (isNull()) {
        return std::string();
      }

      return std::string(_ptrBeg, _ptrEnd);
    }

    constexpr operator const char*() const { return _ptrBeg; }

    explicit operator String() const { return toArduinoString(); }

    explicit operator std::string() const { return toString(); }

    /// Returns a reference to the character at the specified index, Going out of bounds is undefined behavior
    constexpr char const& operator[](int index) const {
      return _ptrBeg[index];
    }
    /// Returns a const reference to the character at the specified index, Going out of bounds is undefined behavior
    constexpr char const& operator[](std::size_t index) const {
      return _ptrBeg[index];
    }

    constexpr bool operator==(const StringView& other) {
      if (this == &other) return true;

      return _strEquals(_ptrBeg, _ptrEnd, other._ptrBeg, other._ptrEnd);
    }
    constexpr bool operator!=(const StringView& other) { return !(*this == other); }
    constexpr bool operator==(const char* const other) { return *this == StringView(other); }
    constexpr bool operator!=(const char* const other) { return !(*this == other); }

    bool operator<(const StringView& other) const {
      if (this == &other) return false;

      return std::lexicographical_compare(_ptrBeg, _ptrEnd, other._ptrBeg, other._ptrEnd);
    }
    bool operator<=(const StringView& other) const { return *this < other || *this == other; }
    bool operator>(const StringView& other) const { return !(*this <= other); }
    bool operator>=(const StringView& other) const { return !(*this < other); }

    constexpr StringView& operator=(const char* const ptr) {
      _ptrBeg = ptr;
      _ptrEnd = _getStringEnd(ptr);
      return *this;
    }
    constexpr StringView& operator=(const StringView& str) {
      _ptrBeg = str._ptrBeg;
      _ptrEnd = str._ptrEnd;
      return *this;
    }
    StringView& operator=(const std::string& str) {
      _ptrBeg = str.c_str();
      _ptrEnd = str.c_str() + str.size();
      return *this;
    }

  private:
    static constexpr bool _strEquals(const char* aStart, const char* aEnd, const char* bStart, const char* bEnd) {
      if (aStart == bStart && aEnd == bEnd) {
        return true;
      }
      if (aStart == nullptr || aEnd == nullptr || bStart == nullptr || bEnd == nullptr) {
        return false;
      }

      std::size_t aLen = aEnd - aStart;
      std::size_t bLen = bEnd - bStart;
      if (aLen != bLen) {
        return false;
      }

      while (aStart < aEnd) {
        if (*aStart != *bStart) {
          return false;
        }
        ++aStart;
        ++bStart;
      }

      return true;
    }
    static constexpr const char* _getStringEnd(const char* ptr) {
      if (ptr == nullptr) {
        return nullptr;
      }

      while (*ptr != '\0') {
        ++ptr;
      }

      return ptr;
    }

    const char* _ptrBeg;
    const char* _ptrEnd;
  };
}  // namespace OpenShock
