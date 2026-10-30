# OpenShock captive portal: the setup SoftAP, its HTTP/WebSocket server (static UI,
# JSON setup endpoints, local flatbuffer messages), the wildcard DNS responder and the
# RFC 8908 handlers, plus the manager task that decides when the portal is open.
#
# Extracted out of `core` and layered above it: the portal drives wifi, the gateway
# link and the local message handlers in core, while core reaches back only through
# the AppHooks callbacks registered in CaptivePortal::Init(). The public surface is just
# captiveportal/Manager.h (std types only); the other headers stay in src/ so
# esp_http_server and friends do not leak into consumers.
idf_component_register(
  SRCS              "src/captiveportal/ApiHandlers.cpp"
                    "src/captiveportal/CaptivePortalInstance.cpp"
                    "src/captiveportal/HttpUtils.cpp"
                    "src/captiveportal/Manager.cpp"
                    "src/captiveportal/StaticFileServer.cpp"
                    "src/captiveportal/WebSocketEndpoint.cpp"
  INCLUDE_DIRS      "include"
  PRIV_INCLUDE_DIRS "src"
  PRIV_REQUIRES     core config device_control                        # wifi/gateway/message handlers/AppHooks; Config::; CommandHandler/EStopManager
                    common chipset logging temporal osjson serialization   # foundation utilities, json, HubToLocalMessage fbs
                    dns_server fs http hwutil rfc8908 www                # DNSServer, LfsPartition (1.x layout), ContentTypes, PartitionUtils, RFC8908Handler, the www image
                    esp_http_server esp_netif esp_timer esp_wifi
)

# Match core: IDF compiles application components with -Werror, the legacy build did not.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)
