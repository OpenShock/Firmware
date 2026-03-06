package server

import (
	"fmt"
	"log"
	"os"

	flatbuffers "github.com/google/flatbuffers/go"

	Configuration "openshock.dev/mock-portal/fbs/OpenShock/Serialization/Configuration"
)

// SaveConfig serialises the current device configuration as a HubConfig
// FlatBuffers binary and writes it to path.
func (s *State) SaveConfig(path string) error {
	s.mu.RLock()
	b := flatbuffers.NewBuilder(512)
	cfg := buildHubConfig(b, s)
	s.mu.RUnlock()

	Configuration.FinishHubConfigBuffer(b, cfg)
	return os.WriteFile(path, b.FinishedBytes(), 0o644)
}

// LoadConfig reads a HubConfig FlatBuffers binary from path and applies it
// to the state, overwriting only fields present in the schema.
func (s *State) LoadConfig(path string) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	if len(data) < 4 {
		return fmt.Errorf("config file too small (%d bytes)", len(data))
	}

	cfg := Configuration.GetRootAsHubConfig(data, 0)

	s.mu.Lock()
	defer s.mu.Unlock()

	if rf := cfg.Rf(nil); rf != nil {
		s.RF.TxPin = rf.TxPin()
		s.RF.KeepaliveEnabled = rf.KeepaliveEnabled()
	}

	if estop := cfg.Estop(nil); estop != nil {
		s.EStop.Enabled = estop.Enabled()
		s.EStop.GpioPin = estop.GpioPin()
		s.EStop.Active = estop.Active()
		s.EStop.Latching = estop.Latching()
	}

	if ota := cfg.OtaUpdate(nil); ota != nil {
		s.OtaUpdate.IsEnabled = ota.IsEnabled()
		s.OtaUpdate.CdnDomain = string(ota.CdnDomain())
		s.OtaUpdate.UpdateChannel = ota.UpdateChannel()
		s.OtaUpdate.CheckOnStartup = ota.CheckOnStartup()
		s.OtaUpdate.CheckPeriodically = ota.CheckPeriodically()
		s.OtaUpdate.CheckInterval = ota.CheckInterval()
		s.OtaUpdate.AllowBackendManagement = ota.AllowBackendManagement()
		s.OtaUpdate.RequireManualApproval = ota.RequireManualApproval()
		s.OtaUpdate.UpdateID = ota.UpdateId()
		s.OtaUpdate.UpdateStep = ota.UpdateStep()
	}

	if wifi := cfg.Wifi(nil); wifi != nil {
		s.WiFi.ApSSID = string(wifi.ApSsid())
		s.WiFi.Hostname = string(wifi.Hostname())
		s.WiFi.Credentials = s.WiFi.Credentials[:0]
		var cred Configuration.WiFiCredentials
		for i := range wifi.CredentialsLength() {
			if wifi.Credentials(&cred, i) {
				s.WiFi.Credentials = append(s.WiFi.Credentials, WifiCredential{
					ID:       cred.Id(),
					SSID:     string(cred.Ssid()),
					Password: string(cred.Password()),
				})
			}
		}
	}

	if backend := cfg.Backend(nil); backend != nil {
		s.Backend.Domain = string(backend.Domain())
		s.Backend.AuthToken = string(backend.AuthToken())
		s.AccountLinked = s.Backend.AuthToken != ""
	}

	if serial := cfg.SerialInput(nil); serial != nil {
		s.SerialInput.EchoEnabled = serial.EchoEnabled()
	}

	if cp := cfg.CaptivePortal(nil); cp != nil {
		s.CaptivePortal.AlwaysEnabled = cp.AlwaysEnabled()
	}

	return nil
}

// autoSave writes the config to disk if a path is configured, logging errors.
func (srv *Server) autoSave() {
	if srv.configPath == "" {
		return
	}
	if err := srv.state.SaveConfig(srv.configPath); err != nil {
		log.Printf("[config] save failed: %v", err)
	} else {
		log.Printf("[config] saved to %s", srv.configPath)
	}
}
