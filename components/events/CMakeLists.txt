# OpenShock application event bus: the OPENSHOCK_EVENTS esp_event base, the event
# id enum, and the POD payload structs posted with each event. Kept as its own
# component so any layer can depend on the event contract without pulling in the
# emitters/consumers.
#
# Public surface:
#   include/events/Events.h — OPENSHOCK_EVENTS base, event ids, payload structs, Init()

idf_component_register(
  SRCS          "src/Events.cpp"
  INCLUDE_DIRS  "include"
  REQUIRES      esp_event   # OPENSHOCK_EVENTS base + default event loop (public header)
  PRIV_REQUIRES logging     # Logging.h
)
