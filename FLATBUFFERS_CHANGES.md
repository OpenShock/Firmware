# FlatBuffers Schema Changes

**Repository:** https://github.com/OpenShock/flatbuffers-schemas
**Branch:** `local-comms-revamp`

These schema changes have been applied to the submodule and are tracked by this branch. They are breaking changes that accompany the WS-to-REST migration.

## Applied Changes

### `HubToLocalMessage.fbs`
- Removed all command result types (`AccountLinkCommandResult`, `SetRfTxPinCommandResult`, `SetEstopPinCommandResult`, `SetEstopEnabledCommandResult`, `SetGPIOResultCode`, `AccountLinkResultCode`)
- Removed all WS command types from `HubToLocalMessagePayload` union that were migrated to REST
- Added `AccountLinkStatusEvent` — broadcast when auth token is validated on gateway connect
- Added `has_standardized_pins` field to `ReadyMessage`

### `LocalToHubMessage.fbs`
- Removed all command types except `Common_ShockerCommandList` (WiFi, OTA, Account, GPIO commands moved to REST)
- `ShockerCommandList` moved to `Common` namespace, shared between gateway and local

### `HubConfig.fbs`
- Added `MacAddress` struct (6-byte fixed-size array)
- Added `auth_mode` (WifiAuthMode enum) and `bssid` (MacAddress) fields to `WiFiCredentials` for security pinning

### `Common/ShockerCommand.fbs` (new)
- Extracted `ShockerCommand` and `ShockerCommandList` tables into shared Common namespace

### `GatewayToHubMessage.fbs`
- Updated `ShockerCommandList` reference to `Common_ShockerCommandList`

## Pending
- Push `local-comms-revamp` branch to schemas repo and create PR
- After merge, update submodule pointer in firmware repo
