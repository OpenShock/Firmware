#include "util/DomainUtils.h"

std::string_view OpenShock::DomainUtils::GetDomainFromUrl(std::string_view url) {
  if (url.empty()) {
    return {};
  }

  // Remove the protocol eg. "https://api.example.com:443/path" -> "api.example.com:443/path"
  auto seperator = url.find("://");
  if (seperator != std::string_view::npos) {
    url.substr(seperator + 3);
  }

  // Remove the path eg. "api.example.com:443/path" -> "api.example.com:443"
  seperator = url.find('/');
  if (seperator != std::string_view::npos) {
    url = url.substr(0, seperator);
  }

  // Remove the port eg. "api.example.com:443" -> "api.example.com"
  seperator = url.rfind(':');
  if (seperator != std::string_view::npos) {
    url = url.substr(0, seperator);
  }

  // Remove all subdomains eg. "api.example.com" -> "example.com"
  seperator = url.rfind('.');
  if (seperator == std::string_view::npos) {
    return url;  // E.g. "localhost"
  }
  seperator = url.rfind('.', seperator - 1);
  if (seperator != std::string_view::npos) {
    url = url.substr(seperator + 1);
  }

  return url;
}
