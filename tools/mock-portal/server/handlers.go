package server

import (
	"fmt"
	"log"
	"time"

	flatbuffers "github.com/google/flatbuffers/go"

	Configuration "openshock.dev/mock-portal/fbs/OpenShock/Serialization/Configuration"
	Local "openshock.dev/mock-portal/fbs/OpenShock/Serialization/Local"
	Types "openshock.dev/mock-portal/fbs/OpenShock/Serialization/Types"
)

func (srv *Server) handleMessage(c *client, data []byte) {
	msg := Local.GetRootAsLocalToHubMessage(data, 0)
	var tbl flatbuffers.Table
	if !msg.Payload(&tbl) {
		c.send(BuildErrorMessage("empty payload"))
		return
	}

	switch msg.PayloadType() {
	// ── WiFi ─────────────────────────────────────────────────────────────────
	case Local.LocalToHubMessagePayloadWifiScanCommand:
		cmd := &Local.WifiScanCommand{}
		cmd.Init(tbl.Bytes, tbl.Pos)
		srv.handleWifiScan(c, cmd.Run())

	case Local.LocalToHubMessagePayloadWifiNetworkSaveCommand:
		cmd := &Local.WifiNetworkSaveCommand{}
		cmd.Init(tbl.Bytes, tbl.Pos)
		srv.handleWifiSave(c, string(cmd.Ssid()), string(cmd.Password()), cmd.Connect())

	case Local.LocalToHubMessagePayloadWifiNetworkForgetCommand:
		cmd := &Local.WifiNetworkForgetCommand{}
		cmd.Init(tbl.Bytes, tbl.Pos)
		srv.handleWifiForget(c, string(cmd.Ssid()))

	case Local.LocalToHubMessagePayloadWifiNetworkConnectCommand:
		cmd := &Local.WifiNetworkConnectCommand{}
		cmd.Init(tbl.Bytes, tbl.Pos)
		srv.handleWifiConnect(c, string(cmd.Ssid()))

	case Local.LocalToHubMessagePayloadWifiNetworkDisconnectCommand:
		srv.handleWifiDisconnect(c)

	// ── OTA ──────────────────────────────────────────────────────────────────
	case Local.LocalToHubMessagePayloadOtaUpdateSetIsEnabledCommand:
		cmd := &Local.OtaUpdateSetIsEnabledCommand{}
		cmd.Init(tbl.Bytes, tbl.Pos)
		srv.state.mu.Lock()
		srv.state.OtaUpdate.IsEnabled = cmd.Enabled()
		srv.state.mu.Unlock()
		log.Printf("[OTA] IsEnabled = %v", cmd.Enabled())
		srv.autoSave()

	case Local.LocalToHubMessagePayloadOtaUpdateSetDomainCommand:
		cmd := &Local.OtaUpdateSetDomainCommand{}
		cmd.Init(tbl.Bytes, tbl.Pos)
		srv.state.mu.Lock()
		srv.state.OtaUpdate.CdnDomain = string(cmd.Domain())
		srv.state.mu.Unlock()
		log.Printf("[OTA] Domain = %q", string(cmd.Domain()))
		srv.autoSave()

	case Local.LocalToHubMessagePayloadOtaUpdateSetUpdateChannelCommand:
		cmd := &Local.OtaUpdateSetUpdateChannelCommand{}
		cmd.Init(tbl.Bytes, tbl.Pos)
		channelStr := string(cmd.Channel())
		ch, ok := Configuration.EnumValuesOtaUpdateChannel[channelStr]
		if !ok {
			log.Printf("[OTA] UpdateChannel: unknown channel %q", channelStr)
			c.send(BuildErrorMessage(fmt.Sprintf("unknown OTA update channel %q", channelStr)))
			break
		}
		srv.state.mu.Lock()
		srv.state.OtaUpdate.UpdateChannel = ch
		srv.state.mu.Unlock()
		log.Printf("[OTA] UpdateChannel = %q", channelStr)
		srv.autoSave()

	case Local.LocalToHubMessagePayloadOtaUpdateSetCheckIntervalCommand:
		cmd := &Local.OtaUpdateSetCheckIntervalCommand{}
		cmd.Init(tbl.Bytes, tbl.Pos)
		srv.state.mu.Lock()
		srv.state.OtaUpdate.CheckInterval = cmd.Interval()
		srv.state.mu.Unlock()
		log.Printf("[OTA] CheckInterval = %d min", cmd.Interval())
		srv.autoSave()

	case Local.LocalToHubMessagePayloadOtaUpdateSetAllowBackendManagementCommand:
		cmd := &Local.OtaUpdateSetAllowBackendManagementCommand{}
		cmd.Init(tbl.Bytes, tbl.Pos)
		srv.state.mu.Lock()
		srv.state.OtaUpdate.AllowBackendManagement = cmd.Allow()
		srv.state.mu.Unlock()
		log.Printf("[OTA] AllowBackendManagement = %v", cmd.Allow())
		srv.autoSave()

	case Local.LocalToHubMessagePayloadOtaUpdateSetRequireManualApprovalCommand:
		cmd := &Local.OtaUpdateSetRequireManualApprovalCommand{}
		cmd.Init(tbl.Bytes, tbl.Pos)
		srv.state.mu.Lock()
		srv.state.OtaUpdate.RequireManualApproval = cmd.Require()
		srv.state.mu.Unlock()
		log.Printf("[OTA] RequireManualApproval = %v", cmd.Require())
		srv.autoSave()

	case Local.LocalToHubMessagePayloadOtaUpdateHandleUpdateRequestCommand:
		cmd := &Local.OtaUpdateHandleUpdateRequestCommand{}
		cmd.Init(tbl.Bytes, tbl.Pos)
		log.Printf("[OTA] HandleUpdateRequest accept=%v (no-op in mock)", cmd.Accept())

	case Local.LocalToHubMessagePayloadOtaUpdateCheckForUpdatesCommand:
		cmd := &Local.OtaUpdateCheckForUpdatesCommand{}
		cmd.Init(tbl.Bytes, tbl.Pos)
		log.Printf("[OTA] CheckForUpdates channel=%q (no-op in mock)", string(cmd.Channel()))

	case Local.LocalToHubMessagePayloadOtaUpdateStartUpdateCommand:
		cmd := &Local.OtaUpdateStartUpdateCommand{}
		cmd.Init(tbl.Bytes, tbl.Pos)
		log.Printf("[OTA] StartUpdate channel=%q version=%q (no-op in mock)", string(cmd.Channel()), string(cmd.Version()))

	// ── Account ──────────────────────────────────────────────────────────────
	case Local.LocalToHubMessagePayloadAccountLinkCommand:
		cmd := &Local.AccountLinkCommand{}
		cmd.Init(tbl.Bytes, tbl.Pos)
		srv.handleAccountLink(c, string(cmd.Code()))

	case Local.LocalToHubMessagePayloadAccountUnlinkCommand:
		srv.handleAccountUnlink(c)

	// ── RF TX Pin ─────────────────────────────────────────────────────────────
	case Local.LocalToHubMessagePayloadSetRfTxPinCommand:
		cmd := &Local.SetRfTxPinCommand{}
		cmd.Init(tbl.Bytes, tbl.Pos)
		srv.handleSetRfTxPin(c, cmd.Pin())

	// ── E-Stop ────────────────────────────────────────────────────────────────
	case Local.LocalToHubMessagePayloadSetEstopEnabledCommand:
		cmd := &Local.SetEstopEnabledCommand{}
		cmd.Init(tbl.Bytes, tbl.Pos)
		srv.handleSetEstopEnabled(c, cmd.Enabled())

	case Local.LocalToHubMessagePayloadSetEstopPinCommand:
		cmd := &Local.SetEstopPinCommand{}
		cmd.Init(tbl.Bytes, tbl.Pos)
		srv.handleSetEstopPin(c, cmd.Pin())

	default:
		log.Printf("[WS] unknown payload type %d", msg.PayloadType())
		c.send(BuildErrorMessage(fmt.Sprintf("unknown payload type %d", msg.PayloadType())))
	}
}

// ── WiFi handlers ────────────────────────────────────────────────────────────

func (srv *Server) handleWifiScan(c *client, run bool) {
	if !run {
		log.Println("[WiFi] scan aborted")
		srv.broadcast(BuildWifiScanStatus(Types.WifiScanStatusAborted))
		return
	}
	log.Println("[WiFi] scan started")
	srv.broadcast(BuildWifiScanStatus(Types.WifiScanStatusStarted))

	go func() {
		time.Sleep(300 * time.Millisecond)
		srv.broadcast(BuildWifiScanStatus(Types.WifiScanStatusInProgress))
		time.Sleep(700 * time.Millisecond)

		// Check for an armed scan failure
		if failStatus, ok := srv.chaos.ConsumeNextScanFail(); ok {
			log.Printf("[WiFi] scan failing with status %v", failStatus)
			srv.broadcast(BuildWifiScanStatus(failStatus))
			return
		}

		srv.state.mu.RLock()
		nets := make([]WifiNetwork, len(srv.state.AvailableNetworks))
		copy(nets, srv.state.AvailableNetworks)
		srv.state.mu.RUnlock()

		for i := range nets {
			srv.state.mu.RLock()
			idx := srv.state.savedCredentialIndex(nets[i].SSID)
			srv.state.mu.RUnlock()
			nets[i].Saved = idx >= 0
		}

		srv.broadcast(BuildWifiNetworkEvent(Types.WifiNetworkEventTypeDiscovered, nets))
		srv.broadcast(BuildWifiScanStatus(Types.WifiScanStatusCompleted))
		log.Printf("[WiFi] scan complete, %d networks", len(nets))
	}()
}

func (srv *Server) handleWifiSave(c *client, ssid, password string, connect bool) {
	if ssid == "" {
		c.send(BuildErrorMessage("ssid required"))
		return
	}
	srv.state.mu.Lock()
	idx := srv.state.savedCredentialIndex(ssid)
	if idx >= 0 {
		srv.state.WiFi.Credentials[idx].Password = password
	} else {
		id := uint8(len(srv.state.WiFi.Credentials) + 1)
		srv.state.WiFi.Credentials = append(srv.state.WiFi.Credentials, WifiCredential{ID: id, SSID: ssid, Password: password})
	}
	// mark saved in available networks
	for i := range srv.state.AvailableNetworks {
		if srv.state.AvailableNetworks[i].SSID == ssid {
			srv.state.AvailableNetworks[i].Saved = true
		}
	}
	nets := []WifiNetwork{{SSID: ssid, Saved: true}}
	srv.state.mu.Unlock()

	log.Printf("[WiFi] saved credentials for %q", ssid)
	srv.broadcast(BuildWifiNetworkEvent(Types.WifiNetworkEventTypeSaved, nets))

	if connect {
		srv.handleWifiConnect(c, ssid)
	}
}

func (srv *Server) handleWifiForget(c *client, ssid string) {
	srv.state.mu.Lock()
	idx := srv.state.savedCredentialIndex(ssid)
	if idx >= 0 {
		srv.state.WiFi.Credentials = append(srv.state.WiFi.Credentials[:idx], srv.state.WiFi.Credentials[idx+1:]...)
	}
	for i := range srv.state.AvailableNetworks {
		if srv.state.AvailableNetworks[i].SSID == ssid {
			srv.state.AvailableNetworks[i].Saved = false
		}
	}
	wasConnected := srv.state.ConnectedSSID == ssid
	srv.state.mu.Unlock()

	log.Printf("[WiFi] forgot %q", ssid)
	srv.broadcast(BuildWifiNetworkEvent(Types.WifiNetworkEventTypeRemoved, []WifiNetwork{{SSID: ssid}}))

	if wasConnected {
		srv.handleWifiDisconnect(c)
	}
}

func (srv *Server) handleWifiConnect(c *client, ssid string) {
	srv.state.mu.RLock()
	idx := srv.state.savedCredentialIndex(ssid)
	srv.state.mu.RUnlock()

	if idx < 0 {
		c.send(BuildErrorMessage(fmt.Sprintf("no saved credentials for %q", ssid)))
		return
	}

	log.Printf("[WiFi] connecting to %q", ssid)
	go func() {
		time.Sleep(500 * time.Millisecond)

		srv.state.mu.Lock()
		srv.state.ConnectedSSID = ssid
		srv.state.ConnectedIP = "192.168.4.2"
		var net WifiNetwork
		for _, n := range srv.state.AvailableNetworks {
			if n.SSID == ssid {
				net = n
				break
			}
		}
		if net.SSID == "" {
			net = WifiNetwork{SSID: ssid, Saved: true, AuthMode: Types.WifiAuthModeWPA2_PSK}
		}
		ip := srv.state.ConnectedIP
		srv.state.mu.Unlock()

		srv.broadcast(BuildWifiNetworkEvent(Types.WifiNetworkEventTypeConnected, []WifiNetwork{net}))
		srv.broadcast(BuildWifiGotIpEvent(ip))
		log.Printf("[WiFi] connected to %q, IP=%s", ssid, ip)
	}()
}

func (srv *Server) handleWifiDisconnect(c *client) {
	srv.state.mu.Lock()
	ssid := srv.state.ConnectedSSID
	ip := srv.state.ConnectedIP
	srv.state.ConnectedSSID = ""
	srv.state.ConnectedIP = ""
	var net WifiNetwork
	for _, n := range srv.state.AvailableNetworks {
		if n.SSID == ssid {
			net = n
			break
		}
	}
	if net.SSID == "" && ssid != "" {
		net = WifiNetwork{SSID: ssid}
	}
	srv.state.mu.Unlock()

	if ssid != "" {
		log.Printf("[WiFi] disconnected from %q", ssid)
		srv.broadcast(BuildWifiLostIpEvent(ip))
		srv.broadcast(BuildWifiNetworkEvent(Types.WifiNetworkEventTypeDisconnected, []WifiNetwork{net}))
	}
}

// ── Account handlers ─────────────────────────────────────────────────────────

func (srv *Server) handleAccountLink(c *client, code string) {
	if len(code) != 6 {
		log.Printf("[Account] link failed: bad code length %d", len(code))
		c.send(BuildAccountLinkResult(Local.AccountLinkResultCodeInvalidCodeLength))
		return
	}
	srv.state.mu.Lock()
	srv.state.AccountLinked = true
	srv.state.Backend.AuthToken = "mock-token-" + code
	srv.state.mu.Unlock()
	log.Printf("[Account] linked with code %q", code)
	c.send(BuildAccountLinkResult(Local.AccountLinkResultCodeSuccess))
	srv.autoSave()
}

func (srv *Server) handleAccountUnlink(c *client) {
	srv.state.mu.Lock()
	srv.state.AccountLinked = false
	srv.state.Backend.AuthToken = ""
	srv.state.mu.Unlock()
	log.Println("[Account] unlinked")
	srv.autoSave()
}

// ── RF TX Pin handler ─────────────────────────────────────────────────────────

func (srv *Server) handleSetRfTxPin(c *client, pin int8) {
	if !srv.state.isValidOutputPin(pin) {
		log.Printf("[RF] invalid pin %d", pin)
		c.send(BuildSetRfTxPinResult(pin, Local.SetGPIOResultCodeInvalidPin))
		return
	}
	srv.state.mu.Lock()
	srv.state.RF.TxPin = pin
	srv.state.mu.Unlock()
	log.Printf("[RF] TX pin set to %d", pin)
	c.send(BuildSetRfTxPinResult(pin, Local.SetGPIOResultCodeSuccess))
	srv.autoSave()
}

// ── E-Stop handlers ───────────────────────────────────────────────────────────

func (srv *Server) handleSetEstopEnabled(c *client, enabled bool) {
	srv.state.mu.Lock()
	srv.state.EStop.Enabled = enabled
	srv.state.mu.Unlock()
	log.Printf("[EStop] enabled = %v", enabled)
	c.send(BuildSetEstopEnabledResult(enabled, true))
	srv.autoSave()
}

func (srv *Server) handleSetEstopPin(c *client, pin int8) {
	if pin >= 0 && !srv.state.isValidOutputPin(pin) {
		log.Printf("[EStop] invalid pin %d", pin)
		c.send(BuildSetEstopPinResult(pin, Local.SetGPIOResultCodeInvalidPin))
		return
	}
	srv.state.mu.Lock()
	srv.state.EStop.GpioPin = pin
	srv.state.mu.Unlock()
	log.Printf("[EStop] pin set to %d", pin)
	c.send(BuildSetEstopPinResult(pin, Local.SetGPIOResultCodeSuccess))
	srv.autoSave()
}
