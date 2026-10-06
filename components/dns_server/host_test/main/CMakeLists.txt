# Compile dns_server's PURE packet parse/build source directly (not via the
# dns_server component) so we avoid its socket/task deps (lwip, freertos tasks).
# Logging.h comes from test/host_support.
idf_component_register(
  SRCS         "test_main.cpp"
               "test_dnspacket.cpp"
               "../../src/DNSPacket.cpp"
  INCLUDE_DIRS "."
               "../../src"  # DNSPacket.h (private to the component)
  REQUIRES     unity
               host_support # Logging.h (test/host_support)
  WHOLE_ARCHIVE  # keep the auto-registered TEST_CASEs from being stripped
)
