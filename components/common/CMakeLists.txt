# OpenShock foundation headers — pure language-level utilities.
#
# Header-only. No SRCS. Arduino-free. Consumers pick these up by `REQUIRES common`.
#
# Public surface:
#   Common.h         — basic defines, DISABLE_DEFAULT/COPY/MOVE macros
#   FormatHelpers.h  — format string helpers
#   enums/*.h        — shared enums (consumers include with the enums/ prefix)

idf_component_register(
  SRCS         "src/LanguageVersionCheck.cpp"
               "src/ArduinoExistanceCheck.cpp"
  INCLUDE_DIRS "include"
)
