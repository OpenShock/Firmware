package server

import (
	flatbuffers "github.com/google/flatbuffers/go"

	Configuration "openshock.dev/mock-portal/fbs/OpenShock/Serialization/Configuration"
	Local "openshock.dev/mock-portal/fbs/OpenShock/Serialization/Local"
	Types "openshock.dev/mock-portal/fbs/OpenShock/Serialization/Types"
)

func buildWifiNetwork(b *flatbuffers.Builder, n WifiNetwork) flatbuffers.UOffsetT {
	ssid := b.CreateString(n.SSID)
	bssid := b.CreateString(n.BSSID)
	Types.WifiNetworkStart(b)
	Types.WifiNetworkAddSsid(b, ssid)
	Types.WifiNetworkAddBssid(b, bssid)
	Types.WifiNetworkAddChannel(b, n.Channel)
	Types.WifiNetworkAddRssi(b, n.RSSI)
	Types.WifiNetworkAddAuthMode(b, n.AuthMode)
	Types.WifiNetworkAddSaved(b, n.Saved)
	return Types.WifiNetworkEnd(b)
}

func buildRFConfig(b *flatbuffers.Builder, rf RFConfig) flatbuffers.UOffsetT {
	Configuration.RFConfigStart(b)
	Configuration.RFConfigAddTxPin(b, rf.TxPin)
	Configuration.RFConfigAddKeepaliveEnabled(b, rf.KeepaliveEnabled)
	return Configuration.RFConfigEnd(b)
}

func buildEStopConfig(b *flatbuffers.Builder, e EStopConfig) flatbuffers.UOffsetT {
	Configuration.EStopConfigStart(b)
	Configuration.EStopConfigAddEnabled(b, e.Enabled)
	Configuration.EStopConfigAddGpioPin(b, e.GpioPin)
	Configuration.EStopConfigAddActive(b, e.Active)
	Configuration.EStopConfigAddLatching(b, e.Latching)
	return Configuration.EStopConfigEnd(b)
}

func buildOtaUpdateConfig(b *flatbuffers.Builder, ota OtaUpdateConfig) flatbuffers.UOffsetT {
	cdnDomain := b.CreateString(ota.CdnDomain)
	Configuration.OtaUpdateConfigStart(b)
	Configuration.OtaUpdateConfigAddIsEnabled(b, ota.IsEnabled)
	Configuration.OtaUpdateConfigAddCdnDomain(b, cdnDomain)
	Configuration.OtaUpdateConfigAddUpdateChannel(b, ota.UpdateChannel)
	Configuration.OtaUpdateConfigAddCheckOnStartup(b, ota.CheckOnStartup)
	Configuration.OtaUpdateConfigAddCheckPeriodically(b, ota.CheckPeriodically)
	Configuration.OtaUpdateConfigAddCheckInterval(b, ota.CheckInterval)
	Configuration.OtaUpdateConfigAddAllowBackendManagement(b, ota.AllowBackendManagement)
	Configuration.OtaUpdateConfigAddRequireManualApproval(b, ota.RequireManualApproval)
	Configuration.OtaUpdateConfigAddUpdateId(b, ota.UpdateID)
	Configuration.OtaUpdateConfigAddUpdateStep(b, ota.UpdateStep)
	return Configuration.OtaUpdateConfigEnd(b)
}

func buildBackendConfig(b *flatbuffers.Builder, bc BackendConfig) flatbuffers.UOffsetT {
	domain := b.CreateString(bc.Domain)
	authToken := b.CreateString(bc.AuthToken)
	Configuration.BackendConfigStart(b)
	Configuration.BackendConfigAddDomain(b, domain)
	Configuration.BackendConfigAddAuthToken(b, authToken)
	return Configuration.BackendConfigEnd(b)
}

func buildSerialInputConfig(b *flatbuffers.Builder, sc SerialInputConfig) flatbuffers.UOffsetT {
	Configuration.SerialInputConfigStart(b)
	Configuration.SerialInputConfigAddEchoEnabled(b, sc.EchoEnabled)
	return Configuration.SerialInputConfigEnd(b)
}

func buildCaptivePortalConfig(b *flatbuffers.Builder, cp CaptivePortalConfig) flatbuffers.UOffsetT {
	Configuration.CaptivePortalConfigStart(b)
	Configuration.CaptivePortalConfigAddAlwaysEnabled(b, cp.AlwaysEnabled)
	return Configuration.CaptivePortalConfigEnd(b)
}

func buildWiFiCredentials(b *flatbuffers.Builder, creds []WifiCredential) flatbuffers.UOffsetT {
	offsets := make([]flatbuffers.UOffsetT, len(creds))
	for i, c := range creds {
		ssid := b.CreateString(c.SSID)
		password := b.CreateString(c.Password)
		Configuration.WiFiCredentialsStart(b)
		Configuration.WiFiCredentialsAddId(b, c.ID)
		Configuration.WiFiCredentialsAddSsid(b, ssid)
		Configuration.WiFiCredentialsAddPassword(b, password)
		offsets[i] = Configuration.WiFiCredentialsEnd(b)
	}
	Configuration.WiFiConfigStartCredentialsVector(b, len(offsets))
	for i := len(offsets) - 1; i >= 0; i-- {
		b.PrependUOffsetT(offsets[i])
	}
	return b.EndVector(len(offsets))
}

func buildWiFiConfig(b *flatbuffers.Builder, wf WiFiConfig) flatbuffers.UOffsetT {
	credsVec := buildWiFiCredentials(b, wf.Credentials)
	apSSID := b.CreateString(wf.ApSSID)
	hostname := b.CreateString(wf.Hostname)
	Configuration.WiFiConfigStart(b)
	Configuration.WiFiConfigAddApSsid(b, apSSID)
	Configuration.WiFiConfigAddHostname(b, hostname)
	Configuration.WiFiConfigAddCredentials(b, credsVec)
	return Configuration.WiFiConfigEnd(b)
}

func buildHubConfig(b *flatbuffers.Builder, s *State) flatbuffers.UOffsetT {
	rf := buildRFConfig(b, s.RF)
	wifi := buildWiFiConfig(b, s.WiFi)
	cp := buildCaptivePortalConfig(b, s.CaptivePortal)
	backend := buildBackendConfig(b, s.Backend)
	serial := buildSerialInputConfig(b, s.SerialInput)
	ota := buildOtaUpdateConfig(b, s.OtaUpdate)
	estop := buildEStopConfig(b, s.EStop)
	Configuration.HubConfigStart(b)
	Configuration.HubConfigAddRf(b, rf)
	Configuration.HubConfigAddWifi(b, wifi)
	Configuration.HubConfigAddCaptivePortal(b, cp)
	Configuration.HubConfigAddBackend(b, backend)
	Configuration.HubConfigAddSerialInput(b, serial)
	Configuration.HubConfigAddOtaUpdate(b, ota)
	Configuration.HubConfigAddEstop(b, estop)
	return Configuration.HubConfigEnd(b)
}

func buildInt8Vector(b *flatbuffers.Builder, pins []int8) flatbuffers.UOffsetT {
	b.StartVector(1, len(pins), 1)
	for i := len(pins) - 1; i >= 0; i-- {
		b.PrependInt8(pins[i])
	}
	return b.EndVector(len(pins))
}

func buildWifiNetworksVector(b *flatbuffers.Builder, networks []WifiNetwork) flatbuffers.UOffsetT {
	offsets := make([]flatbuffers.UOffsetT, len(networks))
	for i, n := range networks {
		offsets[i] = buildWifiNetwork(b, n)
	}
	Local.WifiNetworkEventStartNetworksVector(b, len(offsets))
	for i := len(offsets) - 1; i >= 0; i-- {
		b.PrependUOffsetT(offsets[i])
	}
	return b.EndVector(len(offsets))
}

func wrapHubToLocal(b *flatbuffers.Builder, payloadType Local.HubToLocalMessagePayload, payloadOffset flatbuffers.UOffsetT) []byte {
	Local.HubToLocalMessageStart(b)
	Local.HubToLocalMessageAddPayloadType(b, payloadType)
	Local.HubToLocalMessageAddPayload(b, payloadOffset)
	root := Local.HubToLocalMessageEnd(b)
	b.Finish(root)
	return b.FinishedBytes()
}

func BuildReadyMessage(s *State) []byte {
	s.mu.RLock()
	defer s.mu.RUnlock()

	b := flatbuffers.NewBuilder(1024)

	var connectedWifiOffset flatbuffers.UOffsetT
	if s.ConnectedSSID != "" {
		for _, n := range s.AvailableNetworks {
			if n.SSID == s.ConnectedSSID {
				connectedWifiOffset = buildWifiNetwork(b, n)
				break
			}
		}
	}

	config := buildHubConfig(b, s)
	inputsVec := buildInt8Vector(b, s.ValidInputPins)
	outputsVec := buildInt8Vector(b, s.ValidOutputPins)

	Local.ReadyMessageStart(b)
	Local.ReadyMessageAddPoggies(b, true)
	if connectedWifiOffset != 0 {
		Local.ReadyMessageAddConnectedWifi(b, connectedWifiOffset)
	}
	Local.ReadyMessageAddAccountLinked(b, s.AccountLinked)
	Local.ReadyMessageAddConfig(b, config)
	Local.ReadyMessageAddGpioValidInputs(b, inputsVec)
	Local.ReadyMessageAddGpioValidOutputs(b, outputsVec)
	payload := Local.ReadyMessageEnd(b)

	return wrapHubToLocal(b, Local.HubToLocalMessagePayloadReadyMessage, payload)
}

func BuildWifiNetworkEvent(eventType Types.WifiNetworkEventType, networks []WifiNetwork) []byte {
	b := flatbuffers.NewBuilder(512)
	networksVec := buildWifiNetworksVector(b, networks)
	Local.WifiNetworkEventStart(b)
	Local.WifiNetworkEventAddEventType(b, eventType)
	Local.WifiNetworkEventAddNetworks(b, networksVec)
	payload := Local.WifiNetworkEventEnd(b)
	return wrapHubToLocal(b, Local.HubToLocalMessagePayloadWifiNetworkEvent, payload)
}

func BuildWifiScanStatus(status Types.WifiScanStatus) []byte {
	b := flatbuffers.NewBuilder(64)
	Local.WifiScanStatusMessageStart(b)
	Local.WifiScanStatusMessageAddStatus(b, status)
	payload := Local.WifiScanStatusMessageEnd(b)
	return wrapHubToLocal(b, Local.HubToLocalMessagePayloadWifiScanStatusMessage, payload)
}

func BuildWifiGotIpEvent(ip string) []byte {
	b := flatbuffers.NewBuilder(64)
	ipOffset := b.CreateString(ip)
	Local.WifiGotIpEventStart(b)
	Local.WifiGotIpEventAddIp(b, ipOffset)
	payload := Local.WifiGotIpEventEnd(b)
	return wrapHubToLocal(b, Local.HubToLocalMessagePayloadWifiGotIpEvent, payload)
}

func BuildWifiLostIpEvent(ip string) []byte {
	b := flatbuffers.NewBuilder(64)
	ipOffset := b.CreateString(ip)
	Local.WifiLostIpEventStart(b)
	Local.WifiLostIpEventAddIp(b, ipOffset)
	payload := Local.WifiLostIpEventEnd(b)
	return wrapHubToLocal(b, Local.HubToLocalMessagePayloadWifiLostIpEvent, payload)
}

func BuildAccountLinkResult(result Local.AccountLinkResultCode) []byte {
	b := flatbuffers.NewBuilder(64)
	Local.AccountLinkCommandResultStart(b)
	Local.AccountLinkCommandResultAddResult(b, result)
	payload := Local.AccountLinkCommandResultEnd(b)
	return wrapHubToLocal(b, Local.HubToLocalMessagePayloadAccountLinkCommandResult, payload)
}

func BuildSetRfTxPinResult(pin int8, result Local.SetGPIOResultCode) []byte {
	b := flatbuffers.NewBuilder(64)
	Local.SetRfTxPinCommandResultStart(b)
	Local.SetRfTxPinCommandResultAddPin(b, pin)
	Local.SetRfTxPinCommandResultAddResult(b, result)
	payload := Local.SetRfTxPinCommandResultEnd(b)
	return wrapHubToLocal(b, Local.HubToLocalMessagePayloadSetRfTxPinCommandResult, payload)
}

func BuildSetEstopEnabledResult(enabled bool, success bool) []byte {
	b := flatbuffers.NewBuilder(64)
	Local.SetEstopEnabledCommandResultStart(b)
	Local.SetEstopEnabledCommandResultAddEnabled(b, enabled)
	Local.SetEstopEnabledCommandResultAddSuccess(b, success)
	payload := Local.SetEstopEnabledCommandResultEnd(b)
	return wrapHubToLocal(b, Local.HubToLocalMessagePayloadSetEstopEnabledCommandResult, payload)
}

func BuildSetEstopPinResult(pin int8, result Local.SetGPIOResultCode) []byte {
	b := flatbuffers.NewBuilder(64)
	Local.SetEstopPinCommandResultStart(b)
	Local.SetEstopPinCommandResultAddGpioPin(b, pin)
	Local.SetEstopPinCommandResultAddResult(b, result)
	payload := Local.SetEstopPinCommandResultEnd(b)
	return wrapHubToLocal(b, Local.HubToLocalMessagePayloadSetEstopPinCommandResult, payload)
}

func BuildErrorMessage(msg string) []byte {
	b := flatbuffers.NewBuilder(128)
	msgOffset := b.CreateString(msg)
	Local.ErrorMessageStart(b)
	Local.ErrorMessageAddMessage(b, msgOffset)
	payload := Local.ErrorMessageEnd(b)
	return wrapHubToLocal(b, Local.HubToLocalMessagePayloadErrorMessage, payload)
}
