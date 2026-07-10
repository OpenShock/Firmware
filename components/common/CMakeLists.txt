# OpenShock foundation headers — pure language-level utilities.
#
# Headers plus a single compile-time language-version guard. Consumers pick these
# up by `REQUIRES common`.
#
# Public surface:
#   Common.h         — basic defines, DISABLE_DEFAULT/COPY/MOVE macros
#   FormatHelpers.h  — format string helpers
#   enums/*.h        — shared enums (consumers include with the enums/ prefix)

idf_component_register(
  INCLUDE_DIRS "include"
)
