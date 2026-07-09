# OpenShock JSON handling.
#
# Zero-copy parsing built on raw jsmn (values are std::string_view into the
# source buffer — no NUL-termination, no strlen) plus streaming generation via
# json_generator. Deliberately not named "json" to avoid shadowing IDF's
# built-in cJSON component.
#
# Public surface:
#   json/Json.h — OpenShock::JSON::{JsonView, JsonDocument, StringWriter}

idf_component_register(
  SRCS         "src/Json.cpp"
  INCLUDE_DIRS "include"
  REQUIRES     espressif__jsmn            # jsmn.h types in public header
               espressif__json_generator  # json_generator.h in public header
)
