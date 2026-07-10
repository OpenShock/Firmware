# Minimal wildcard DNS responder used by the captive portal (all A queries → a fixed
# IPv4 address). Standalone component so it can be reused independently of the portal.
idf_component_register(
  SRCS "src/DNSServer.cpp"
  INCLUDE_DIRS "include"
  REQUIRES common freertos          # OpenShock.h + freertos/* in public header
  PRIV_REQUIRES util logging lwip   # all .cpp-only
)
