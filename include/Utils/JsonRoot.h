#pragma once

#include <Arduino.h>

#include <cJSON.h>

#include <string>
#include <cstdint>

namespace OpenShock {
  class JsonRoot {
    JsonRoot(cJSON* root) : _root(root) { }
  public:
    static JsonRoot CreateObject() {
      return JsonRoot(cJSON_CreateObject());
    }
    static JsonRoot CreateArray() {
      return JsonRoot(cJSON_CreateArray());
    }
    JsonRoot() : _root(nullptr) { }
    JsonRoot(JsonRoot&& other) : _root(other._root) {
      other._root = nullptr;
    }
    JsonRoot(const JsonRoot& other) : _root(other._root == nullptr ? nullptr : cJSON_Duplicate(other._root, true)) { }
    JsonRoot(const char* data) : _root(cJSON_Parse(data)) { }
    JsonRoot(const char* data, std::size_t length) : _root(cJSON_ParseWithLength(data, length)) { }
    JsonRoot(const std::string& data) : JsonRoot(data.c_str(), data.length()) { }
    JsonRoot(const String& data) : JsonRoot(data.c_str(), data.length()) { }
    ~JsonRoot() {
      if (_root != nullptr) {
        cJSON_Delete(_root);
      }
    }

    bool isValid() const {
      return _root != nullptr;
    }
    bool isObject() const {
      return isValid() && cJSON_IsObject(_root);
    }
    bool isArray() const {
      return isValid() && cJSON_IsArray(_root);
    }

    const char* GetErrorMessage() const {
      const char* error = cJSON_GetErrorPtr();
      if (error == nullptr) {
        return "Unknown error";
      }

      return error;
    }

    JsonRoot& operator=(const JsonRoot& other) {
      if (this != &other) {
        if (_root) {
          cJSON_Delete(_root);
        }
        _root = cJSON_Duplicate(other._root, true);
      }
      return *this;
    }

    cJSON* operator->() const {
      return _root;
    }
    operator cJSON*() const {
      return _root;
    }
  private:
    cJSON* _root;
  };
}
