package server

import (
	"fmt"
	"math/rand"

	"github.com/brianvoe/gofakeit/v6"

	Types "openshock.dev/mock-portal/fbs/OpenShock/Serialization/Types"
)

// ssidTemplates are expanded with random data to produce realistic WiFi names.
var ssidTemplates = []func() string{
	func() string { return fmt.Sprintf("%s's %s", gofakeit.FirstName(), gofakeit.RandomString([]string{"WiFi", "Network", "Home", "WLAN", "Internet", "Hub"})) },
	func() string { return fmt.Sprintf("NETGEAR_%s", gofakeit.Lexify("??????")) },
	func() string { return fmt.Sprintf("Xfinity_%s", gofakeit.Lexify("????????")) },
	func() string { return fmt.Sprintf("TP-Link_%s", gofakeit.Lexify("######")) },
	func() string { return fmt.Sprintf("ASUS_%s", gofakeit.Lexify("######")) },
	func() string { return fmt.Sprintf("AT&T-WiFi-%s", gofakeit.Lexify("####")) },
	func() string { return fmt.Sprintf("%s_%s", gofakeit.City(), gofakeit.Lexify("####")) },
	func() string { return fmt.Sprintf("%s Office", gofakeit.Company()) },
	func() string { return fmt.Sprintf("%s Guest", gofakeit.Company()) },
	func() string { return fmt.Sprintf("%s IoT", gofakeit.AnimalType()) },
	func() string { return gofakeit.HackerNoun() + "_net" },
	func() string { return fmt.Sprintf("FBI Surveillance Van %d", gofakeit.Number(1, 9)) },
	func() string { return fmt.Sprintf("Pretty Fly for a WiFi") },
	func() string { return fmt.Sprintf("No Internet Access") },
	func() string { return fmt.Sprintf("Loading...") },
	func() string { return fmt.Sprintf("Not %s's WiFi", gofakeit.LastName()) },
	func() string { return fmt.Sprintf("Definitely Not A Spy Network") },
	func() string { return fmt.Sprintf("Bill Wi The Science Fi") },
	func() string { return fmt.Sprintf("The Promised LAN") },
	func() string { return fmt.Sprintf("LAN Solo") },
	func() string { return fmt.Sprintf("404 Network Not Found") },
	func() string { return fmt.Sprintf("drop table wifi;--") },           // SQL injection
	func() string { return fmt.Sprintf("<script>alert(1)</script>") },    // XSS
	func() string { return fmt.Sprintf("../../etc/passwd") },             // path traversal
	func() string { return fmt.Sprintf("%s\x00hidden", gofakeit.Word()) }, // null byte
	func() string { return fmt.Sprintf("🔥WiFi🔥") },
	func() string { return fmt.Sprintf("☠️ Pirate Network ☠️") },
	func() string { return fmt.Sprintf("Café_WiFi_Ñoño") },
	func() string { return fmt.Sprintf("网络_%s", gofakeit.Lexify("####")) },
	func() string { return fmt.Sprintf("Ω≈ç√∫˜µ≤≥÷") },
	// Very long name (near/at 32-char limit)
	func() string {
		s := gofakeit.LoremIpsumWord() + "_" + gofakeit.LoremIpsumWord() + "_" + gofakeit.LoremIpsumWord()
		if len(s) > 32 {
			s = s[:32]
		}
		return s
	},
	// Whitespace-heavy
	func() string { return fmt.Sprintf("   %s   ", gofakeit.Word()) },
	// Repeated chars
	func() string { return "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAaaa" },
	func() string { return "\t\r\n" },
	// Zero-width space
	func() string { return fmt.Sprintf("%s\u200B%s", gofakeit.Word(), gofakeit.Word()) },
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

func RandomNetwork(saved bool) WifiNetwork {
	ssid := ssidTemplates[rand.Intn(len(ssidTemplates))]()
	return WifiNetwork{
		SSID:     ssid,
		BSSID:    randomBSSID(),
		Channel:  uint8(gofakeit.RandomString([]string{"1", "6", "11", "2", "3", "4", "5", "7", "8", "9", "10", "12", "13", "36", "40", "44", "48", "52", "100", "149"})[0] % 16),
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

func max(a, b int) int {
	if a > b {
		return a
	}
	return b
}
