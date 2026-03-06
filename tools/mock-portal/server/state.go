package server

import (
	"sync"

	Configuration "openshock.dev/mock-portal/fbs/OpenShock/Serialization/Configuration"
	Types "openshock.dev/mock-portal/fbs/OpenShock/Serialization/Types"
)

type WifiNetwork struct {
	SSID     string
	BSSID    string
	Channel  uint8
	RSSI     int8
	AuthMode Types.WifiAuthMode
	Saved    bool
}

type WifiCredential struct {
	ID       uint8
	SSID     string
	Password string
}

type RFConfig struct {
	TxPin            int8
	KeepaliveEnabled bool
}

type EStopConfig struct {
	Enabled  bool
	GpioPin  int8
	Active   bool
	Latching bool
}

type OtaUpdateConfig struct {
	IsEnabled              bool
	CdnDomain              string
	UpdateChannel          Configuration.OtaUpdateChannel
	CheckOnStartup         bool
	CheckPeriodically      bool
	CheckInterval          uint16
	AllowBackendManagement bool
	RequireManualApproval  bool
	UpdateID               int32
	UpdateStep             Configuration.OtaUpdateStep
}

type BackendConfig struct {
	Domain    string
	AuthToken string
}

type SerialInputConfig struct {
	EchoEnabled bool
}

type CaptivePortalConfig struct {
	AlwaysEnabled bool
}

type WiFiConfig struct {
	ApSSID      string
	Hostname    string
	Credentials []WifiCredential
}

type State struct {
	mu sync.RWMutex

	// Available networks visible in a scan
	AvailableNetworks []WifiNetwork

	// Currently connected network (nil if disconnected)
	ConnectedSSID string
	ConnectedIP   string

	// Account
	AccountLinked bool

	// Configs
	RF           RFConfig
	EStop        EStopConfig
	OtaUpdate    OtaUpdateConfig
	WiFi         WiFiConfig
	Backend      BackendConfig
	SerialInput  SerialInputConfig
	CaptivePortal CaptivePortalConfig

	// Valid GPIO pins exposed to UI
	ValidInputPins  []int8
	ValidOutputPins []int8

	// Scan in progress
	ScanRunning bool
}

func NewState() *State {
	return &State{
		AvailableNetworks: GenerateNetworks(40),
		ConnectedSSID: "",
		ConnectedIP:   "",
		AccountLinked: false,
		RF: RFConfig{
			TxPin:            15,
			KeepaliveEnabled: true,
		},
		EStop: EStopConfig{
			Enabled:  false,
			GpioPin:  -1,
			Active:   false,
			Latching: false,
		},
		OtaUpdate: OtaUpdateConfig{
			IsEnabled:              true,
			CdnDomain:              "cdn.openshock.app",
			UpdateChannel:          Configuration.OtaUpdateChannelStable,
			CheckOnStartup:         true,
			CheckPeriodically:      true,
			CheckInterval:          60,
			AllowBackendManagement: false,
			RequireManualApproval:  false,
			UpdateID:               0,
			UpdateStep:             Configuration.OtaUpdateStepNone,
		},
		WiFi: WiFiConfig{
			ApSSID:   "OpenShock",
			Hostname: "openshock",
			Credentials: []WifiCredential{
				{ID: 1, SSID: "HomeNetwork", Password: "secret123"},
			},
		},
		Backend: BackendConfig{
			Domain:    "api.openshock.app",
			AuthToken: "",
		},
		SerialInput: SerialInputConfig{
			EchoEnabled: true,
		},
		CaptivePortal: CaptivePortalConfig{
			AlwaysEnabled: false,
		},
		ValidInputPins:  []int8{0, 2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33, 34, 35, 36, 39},
		ValidOutputPins: []int8{0, 2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33},
	}
}

func (s *State) isValidOutputPin(pin int8) bool {
	if pin < 0 {
		return false
	}
	s.mu.RLock()
	defer s.mu.RUnlock()
	for _, p := range s.ValidOutputPins {
		if p == pin {
			return true
		}
	}
	return false
}

func (s *State) savedCredentialIndex(ssid string) int {
	for i, c := range s.WiFi.Credentials {
		if c.SSID == ssid {
			return i
		}
	}
	return -1
}
