package server

import (
	"fmt"
	"math/rand"

	"github.com/brianvoe/gofakeit/v6"

	Types "openshock.dev/mock-portal/fbs/OpenShock/Serialization/Types"
)

// ssidTemplates are expanded with random data to produce realistic WiFi names.
var ssidTemplates = []func() string{
	// ── Router vendor defaults ────────────────────────────────────────────────
	func() string { return fmt.Sprintf("NETGEAR_%s", gofakeit.Lexify("??????")) },
	func() string { return fmt.Sprintf("Xfinity_%s", gofakeit.Lexify("????????")) },
	func() string { return fmt.Sprintf("TP-Link_%s", gofakeit.Lexify("######")) },
	func() string { return fmt.Sprintf("ASUS_%s", gofakeit.Lexify("######")) },
	func() string { return fmt.Sprintf("AT&T-WiFi-%s", gofakeit.Lexify("####")) },
	func() string { return fmt.Sprintf("Linksys%s", gofakeit.Lexify("?????")) },
	func() string { return fmt.Sprintf("dlink-%s", gofakeit.Lexify("####")) },
	func() string { return fmt.Sprintf("2WIRE%s", gofakeit.Lexify("###")) },
	func() string { return fmt.Sprintf("belkin.%s", gofakeit.Lexify("######")) },
	func() string { return fmt.Sprintf("Spectrum-%s-2.4G", gofakeit.Lexify("####")) },
	func() string { return fmt.Sprintf("Spectrum-%s-5G", gofakeit.Lexify("####")) },
	func() string { return fmt.Sprintf("MySpectrumWiFi%s", gofakeit.Lexify("####")) },
	func() string { return fmt.Sprintf("Fios-G%s", gofakeit.Lexify("####")) },
	func() string { return fmt.Sprintf("CenturyLink%s", gofakeit.Lexify("####")) },
	func() string { return fmt.Sprintf("HOME-%s-2.4", gofakeit.Lexify("####")) },
	func() string { return fmt.Sprintf("BELL%s", gofakeit.Lexify("###")) },
	// ── Personal ─────────────────────────────────────────────────────────────
	func() string {
		return fmt.Sprintf("%s's %s", gofakeit.FirstName(), gofakeit.RandomString([]string{"WiFi", "Network", "Home", "WLAN", "Internet", "Hub"}))
	},
	func() string {
		return fmt.Sprintf("The %s %s", gofakeit.LastName(), gofakeit.RandomString([]string{"Network", "WiFi", "LAN", "Internet", "Household"}))
	},
	func() string { return fmt.Sprintf("%s_%s", gofakeit.City(), gofakeit.Lexify("####")) },
	func() string { return fmt.Sprintf("Apt%s", gofakeit.Numerify("##")) },
	func() string { return fmt.Sprintf("Unit%s_WiFi", gofakeit.Numerify("##")) },
	// ── Business / venue ─────────────────────────────────────────────────────
	func() string { return fmt.Sprintf("%s_Office", gofakeit.Company()) },
	func() string { return fmt.Sprintf("%s_Guest", gofakeit.Company()) },
	func() string { return fmt.Sprintf("%s_IoT", gofakeit.AnimalType()) },
	func() string { return gofakeit.HackerNoun() + "_net" },
	// ── Short names ───────────────────────────────────────────────────────────
	func() string { return gofakeit.Lexify("WiFi-??") },
	func() string { return gofakeit.Lexify("NET##") },
	func() string { return gofakeit.LastName()[:3] + gofakeit.Numerify("##") },
	// ── Long names (near 32-char limit) ──────────────────────────────────────
	func() string {
		s := fmt.Sprintf("%s_%s_%s", gofakeit.LastName(), gofakeit.City(), gofakeit.Lexify("####"))
		if len(s) > 32 {
			s = s[:32]
		}
		return s
	},
	func() string {
		s := fmt.Sprintf("%s_Guest_Network_%s", gofakeit.Company(), gofakeit.Lexify("##"))
		if len(s) > 32 {
			s = s[:32]
		}
		return s
	},
}

var allAuthModes = []Types.WifiAuthMode{
	Types.WifiAuthModeOpen,
	Types.WifiAuthModeWEP,
	Types.WifiAuthModeWPA_PSK,
	Types.WifiAuthModeWPA2_PSK,
	Types.WifiAuthModeWPA_WPA2_PSK,
	Types.WifiAuthModeWPA2_ENTERPRISE,
	Types.WifiAuthModeWPA3_PSK,
	Types.WifiAuthModeWPA2_WPA3_PSK,
	Types.WifiAuthModeWAPI_PSK,
	Types.WifiAuthModeUNKNOWN,
}

func randomBSSID() string {
	return fmt.Sprintf("%02X:%02X:%02X:%02X:%02X:%02X",
		gofakeit.Number(0, 255), gofakeit.Number(0, 255), gofakeit.Number(0, 255),
		gofakeit.Number(0, 255), gofakeit.Number(0, 255), gofakeit.Number(0, 255))
}

func randomAuthMode() Types.WifiAuthMode {
	// Weight towards WPA2 to be realistic
	r := rand.Intn(10)
	switch {
	case r < 5:
		return Types.WifiAuthModeWPA2_PSK
	case r < 7:
		return Types.WifiAuthModeWPA3_PSK
	case r < 8:
		return Types.WifiAuthModeOpen
	default:
		return allAuthModes[rand.Intn(len(allAuthModes))]
	}
}

var wifiChannels = []uint8{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 36, 40, 44, 48, 52, 100, 149}

func RandomNetwork(saved bool) WifiNetwork {
	ssid := ssidTemplates[rand.Intn(len(ssidTemplates))]()
	return WifiNetwork{
		SSID:     ssid,
		BSSID:    randomBSSID(),
		Channel:  wifiChannels[rand.Intn(len(wifiChannels))],
		RSSI:     int8(-gofakeit.Number(30, 95)),
		AuthMode: randomAuthMode(),
		Saved:    saved,
	}
}

func RandomHiddenNetwork() WifiNetwork {
	return WifiNetwork{
		SSID:     "", // hidden
		BSSID:    randomBSSID(),
		Channel:  uint8(gofakeit.Number(1, 13)),
		RSSI:     int8(-gofakeit.Number(40, 90)),
		AuthMode: Types.WifiAuthModeWPA2_PSK,
		Saved:    false,
	}
}

// GenerateNetworks produces a large randomised set of networks including
// hidden ones and weird edge-case SSIDs.
func GenerateNetworks(count int) []WifiNetwork {
	nets := make([]WifiNetwork, 0, count)

	// A few hidden networks
	hiddenCount := max(1, count/8)
	for range hiddenCount {
		nets = append(nets, RandomHiddenNetwork())
	}

	// Fill the rest
	for len(nets) < count {
		nets = append(nets, RandomNetwork(false))
	}

	// Shuffle
	rand.Shuffle(len(nets), func(i, j int) { nets[i], nets[j] = nets[j], nets[i] })
	return nets
}
