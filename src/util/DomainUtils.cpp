#include "util/DomainUtils.h"

std::string_view OpenShock::DomainUtils::GetDomainFromUrl(std::string_view url) {
  if (url.empty()) {
    return {};
  }

  // Remove the protocol eg. "https://api.example.com:443/path" -> "api.example.com:443/path"
  auto separator = url.find("://");
  if (separator != std::string_view::npos) {
    url.substr(separator + 3);
  }

  // Remove the path eg. "api.example.com:443/path" -> "api.example.com:443"
  separator = url.find('/');
  if (separator != std::string_view::npos) {
    url = url.substr(0, separator);
  }

  // Remove the port eg. "api.example.com:443" -> "api.example.com"
  separator = url.rfind(':');
  if (separator != std::string_view::npos) {
    url = url.substr(0, separator);
  }

  // Remove all subdomains eg. "api.example.com" -> "example.com"
  separator = url.rfind('.');
  if (separator == std::string_view::npos) {
    return url;  // E.g. "localhost"
  }
  separator = url.rfind('.', separator - 1);
  if (separator != std::string_view::npos) {
    url = url.substr(separator + 1);
  }

  return url;
}
