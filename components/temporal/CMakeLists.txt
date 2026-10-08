# OpenShock time helpers — OpenShock::millis(), OpenShock::micros().
#
# Thin header-only wrapper over esp_timer. Split from common so that logging
# and other components can depend on it without pulling everything else in.

idf_component_register(
  INCLUDE_DIRS "include"
  REQUIRES     esp_timer
)
