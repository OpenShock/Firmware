# FlatBuffers Schema Changes Required

**Repository:** https://github.com/OpenShock/flatbuffers-schemas

Apply these as a PR to the schemas repo, then bump the submodule pointer in the firmware repo and regenerate bindings. **Breaking changes are intentional** — these clean up commands that have been superseded by HTTP REST endpoints in the captive portal.

---

## `HubToLocalMessage.fbs`

### 1. Add `has_predefined_pins` to `ReadyMessage`

New field indicating whether the firmware was compiled with a hardcoded RF TX pin (`OPENSHOCK_RF_TX_GPIO != OPENSHOCK_GPIO_INVALID`). The frontend uses this to decide whether to show the Pin configuration step in the setup wizard.

```diff
 table ReadyMessage {
   poggies:bool;
   connected_wifi:Types.WifiNetwork;
   account_linked:bool;
   config:Configuration.HubConfig;
   gpio_valid_inputs:[int8];
   gpio_valid_outputs:[int8];
+  has_predefined_pins:bool;
 }
```

Adding a field at the end is **backwards compatible** — no wire format break.

> **Workaround until merged:** The firmware exposes `GET /api/board` → `{"has_predefined_pins": true/false}` which the frontend fetches on startup.

### 2. Remove result types for commands moved to REST

The following result messages are no longer sent over WebSocket. Their results are returned directly in HTTP responses.

```diff
-enum AccountLinkResultCode : uint8 {
-  Success = 0,
-  CodeRequired = 1,
-  InvalidCodeLength = 2,
-  NoInternetConnection = 3,
-  InvalidCode = 4,
-  RateLimited = 5,
-  InternalError = 6
-}
-table AccountLinkCommandResult {
-  result:AccountLinkResultCode;
-}
-
-enum SetGPIOResultCode : uint8 {
-  Success = 0,
-  InvalidPin = 1,
-  InternalError = 2
-}
-table SetRfTxPinCommandResult {
-  pin:int8;
-  result:SetGPIOResultCode;
-}
-table SetEstopEnabledCommandResult {
-  enabled:bool;
-  success:bool;
-}
-table SetEstopPinCommandResult {
-  gpio_pin:int8;
-  result:SetGPIOResultCode;
-}

 union HubToLocalMessagePayload {
   ReadyMessage,
   ErrorMessage,
   WifiScanStatusMessage,
   WifiNetworkEvent,
   WifiGotIpEvent,
   WifiLostIpEvent,
-  AccountLinkCommandResult,
-  SetRfTxPinCommandResult,
-  SetEstopEnabledCommandResult,
-  SetEstopPinCommandResult
 }
```

---

## `LocalToHubMessage.fbs`

### Remove commands moved to REST

The following commands are now handled via HTTP REST endpoints. The WebSocket handlers in `src/message_handlers/websocket/local/` for these can be deleted from the firmware after the schema update.

```diff
-table WifiScanCommand {
-  run:bool;
-}
-
-table WifiNetworkSaveCommand {
-  ssid:string;
-  password:string;
-  connect:bool;
-}
-
-table WifiNetworkForgetCommand {
-  ssid:string;
-}
-
-table WifiNetworkConnectCommand {
-  ssid:string;
-}
-
-table WifiNetworkDisconnectCommand {
-  placeholder:bool;
-}
-
-table OtaUpdateSetIsEnabledCommand {
-  is_enabled:bool;
-}
-
-table OtaUpdateSetDomainCommand {
-  domain:string;
-}
-
-table OtaUpdateSetUpdateChannelCommand {
-  channel:string;
-}
-
-table OtaUpdateSetCheckIntervalCommand {
-  check_interval:uint16;
-}
-
-table OtaUpdateSetAllowBackendManagementCommand {
-  allow_backend_management:bool;
-}
-
-table OtaUpdateSetRequireManualApprovalCommand {
-  require_manual_approval:bool;
-}
-
-table OtaUpdateHandleUpdateRequestCommand {
-  accept:bool;
-}
-
-table OtaUpdateCheckForUpdatesCommand {
-  channel:string;
-}
-
-table OtaUpdateStartUpdateCommand {
-  semver:string;
-}
-
-table AccountLinkCommand {
-  code:string;
-}
-table AccountUnlinkCommand {
-  placeholder:bool;
-}
-table SetRfTxPinCommand {
-  pin:int8;
-}
-table SetEstopEnabledCommand {
-  enabled:bool;
-}
-table SetEstopPinCommand {
-  pin:int8;
-}

 union LocalToHubMessagePayload {
-  WifiScanCommand,
-  WifiNetworkSaveCommand,
-  WifiNetworkForgetCommand,
-  WifiNetworkConnectCommand,
-  WifiNetworkDisconnectCommand,
-  OtaUpdateSetIsEnabledCommand,
-  OtaUpdateSetDomainCommand,
-  OtaUpdateSetUpdateChannelCommand,
-  OtaUpdateSetCheckIntervalCommand,
-  OtaUpdateSetAllowBackendManagementCommand,
-  OtaUpdateSetRequireManualApprovalCommand,
-  OtaUpdateHandleUpdateRequestCommand,
-  OtaUpdateCheckForUpdatesCommand,
-  OtaUpdateStartUpdateCommand,
-  AccountLinkCommand,
-  AccountUnlinkCommand,
-  SetRfTxPinCommand,
-  SetEstopEnabledCommand,
-  SetEstopPinCommand
 }
```

---

## REST endpoints replacing the removed commands

| Removed WS command                          | HTTP replacement                                             | Notes                                   |
|---------------------------------------------|--------------------------------------------------------------|-----------------------------------------|
| `WifiScanCommand`                           | `POST /api/wifi/scan?run=1`                                  | `run=0` aborts scan                     |
| `WifiNetworkSaveCommand`                    | `POST /api/wifi/networks` (form body: ssid, password, connect) |                                       |
| `WifiNetworkForgetCommand`                  | `DELETE /api/wifi/networks?ssid=<ssid>`                      |                                         |
| `WifiNetworkConnectCommand`                 | `POST /api/wifi/connect` (form body: ssid)                   |                                         |
| `WifiNetworkDisconnectCommand`              | `POST /api/wifi/disconnect`                                  |                                         |
| `OtaUpdateSetIsEnabledCommand`              | `PUT /api/ota/enabled?enabled=1`                             |                                         |
| `OtaUpdateSetDomainCommand`                 | `PUT /api/ota/domain?domain=<domain>`                        |                                         |
| `OtaUpdateSetUpdateChannelCommand`          | `PUT /api/ota/channel?channel=<channel>`                     |                                         |
| `OtaUpdateSetCheckIntervalCommand`          | `PUT /api/ota/check-interval?interval=<n>`                   |                                         |
| `OtaUpdateSetAllowBackendManagementCommand` | `PUT /api/ota/allow-backend-management?allow=1`              |                                         |
| `OtaUpdateSetRequireManualApprovalCommand`  | `PUT /api/ota/require-manual-approval?require=1`             |                                         |
| `OtaUpdateCheckForUpdatesCommand`           | `POST /api/ota/check?channel=<channel>`                      | Handler is a TODO stub                  |
| `AccountLinkCommand`                        | `POST /api/account/link?code=<code>`                         | Returns `{"ok":true}` or error string   |
| `AccountUnlinkCommand`                      | `DELETE /api/account`                                        |                                         |
| `SetRfTxPinCommand`                         | `PUT /api/config/rf/pin?pin=<n>`                             | Returns `{"ok":true,"pin":N}` or error  |
| `SetEstopPinCommand`                        | `PUT /api/config/estop/pin?pin=<n>`                          | Returns `{"ok":true,"pin":N}` or error  |
| `SetEstopEnabledCommand`                    | `PUT /api/config/estop/enabled?enabled=1`                    |                                         |
| *(new)*                                     | `POST /api/portal/close`                                     | Replaces proposed CloseCaptivePortalCommand |

---

## Notes

- **Breaking:** Removing union entries shifts discriminator values for subsequent entries. Regenerate all FlatBuffers bindings (`include/serialization/_fbs/` and `frontend/src/lib/_fbs/`) via `flatc` after applying — do **not** edit generated files by hand.
- The firmware currently works without these schema changes using workarounds: `poggies` field for `has_predefined_pins` (set in `src/serialization/WSLocal.cpp`), and HTTP endpoints for all removed commands.
- Once merged, update the submodule pointer in the firmware repo.
