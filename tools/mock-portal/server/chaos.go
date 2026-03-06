package server

import (
	"log"
	"math/rand"
	"sync"
	"time"

	Types "openshock.dev/mock-portal/fbs/OpenShock/Serialization/Types"
)

var chaosErrors = []string{
	"internal error: assertion failed",
	"wifi driver crashed",
	"out of memory",
	"task watchdog triggered",
	"NVS partition corrupt",
	"RF subsystem timeout",
	"backend connection refused",
	"OTA server unreachable",
	"invalid FlatBuffer received",
	"gpio interrupt storm",
	"flash write failed",
	"",  // empty error — tests blank message handling
}

type ChaosEngine struct {
	mu           sync.Mutex
	enabled      bool
	nextScanFail *Types.WifiScanStatus
	srv          *Server
}

func newChaosEngine(srv *Server) *ChaosEngine {
	return &ChaosEngine{srv: srv}
}

func (c *ChaosEngine) Enable() {
	c.mu.Lock()
	c.enabled = true
	c.mu.Unlock()
}

func (c *ChaosEngine) Disable() {
	c.mu.Lock()
	c.enabled = false
	c.mu.Unlock()
}

func (c *ChaosEngine) IsEnabled() bool {
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.enabled
}

func (c *ChaosEngine) SetNextScanFail(s Types.WifiScanStatus) {
	c.mu.Lock()
	c.nextScanFail = &s
	c.mu.Unlock()
}

// ConsumeNextScanFail returns a pending scan-fail status and clears it.
// Returns (status, true) if one was set, (_, false) otherwise.
func (c *ChaosEngine) ConsumeNextScanFail() (Types.WifiScanStatus, bool) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.nextScanFail == nil {
		return 0, false
	}
	s := *c.nextScanFail
	c.nextScanFail = nil
	return s, true
}

// Run starts the chaos engine loop; it blocks until the server shuts down.
func (c *ChaosEngine) Run() {
	for {
		// Random delay between 2–10 seconds between chaos events
		delay := time.Duration(2000+rand.Intn(8000)) * time.Millisecond
		time.Sleep(delay)

		if !c.IsEnabled() {
			continue
		}

		c.fireRandom()
	}
}

func (c *ChaosEngine) fireRandom() {
	srv := c.srv
	action := rand.Intn(10)
	switch action {
	case 0:
		// Disconnect all clients
		log.Println("[chaos] disconnecting all clients")
		srv.cmdDisconnectAll()

	case 1:
		// Broadcast a random error
		msg := chaosErrors[rand.Intn(len(chaosErrors))]
		log.Printf("[chaos] error broadcast: %q", msg)
		srv.broadcast(BuildErrorMessage(msg))

	case 2:
		// Traffic spike
		n := 20 + rand.Intn(80)
		log.Printf("[chaos] spike %d events", n)
		go srv.cmdSpike(n)

	case 3:
		// Corrupt frame(s)
		n := 1 + rand.Intn(3)
		log.Printf("[chaos] %d corrupt frame(s)", n)
		srv.cmdCorrupt(n)

	case 4:
		// Arm a scan failure for the next scan
		statuses := []Types.WifiScanStatus{
			Types.WifiScanStatusError,
			Types.WifiScanStatusTimedOut,
			Types.WifiScanStatusAborted,
		}
		s := statuses[rand.Intn(len(statuses))]
		log.Printf("[chaos] arming scan fail: %v", s)
		c.SetNextScanFail(s)

	case 5:
		// Add a wave of new random networks
		n := 3 + rand.Intn(10)
		nets := GenerateNetworks(n)
		srv.state.mu.Lock()
		srv.state.AvailableNetworks = append(srv.state.AvailableNetworks, nets...)
		srv.state.mu.Unlock()
		srv.broadcast(BuildWifiNetworkEvent(Types.WifiNetworkEventTypeDiscovered, nets))
		log.Printf("[chaos] added %d networks", n)

	case 6:
		// Remove a random network
		srv.state.mu.Lock()
		if len(srv.state.AvailableNetworks) > 0 {
			i := rand.Intn(len(srv.state.AvailableNetworks))
			net := srv.state.AvailableNetworks[i]
			srv.state.AvailableNetworks = append(srv.state.AvailableNetworks[:i], srv.state.AvailableNetworks[i+1:]...)
			srv.state.mu.Unlock()
			srv.broadcast(BuildWifiNetworkEvent(Types.WifiNetworkEventTypeLost, []WifiNetwork{net}))
			log.Printf("[chaos] removed network %q", net.SSID)
		} else {
			srv.state.mu.Unlock()
		}

	case 7:
		// Update RSSI on a random existing network (simulate signal fluctuation)
		srv.state.mu.Lock()
		if len(srv.state.AvailableNetworks) > 0 {
			i := rand.Intn(len(srv.state.AvailableNetworks))
			srv.state.AvailableNetworks[i].RSSI = int8(-30 - rand.Intn(65))
			net := srv.state.AvailableNetworks[i]
			srv.state.mu.Unlock()
			srv.broadcast(BuildWifiNetworkEvent(Types.WifiNetworkEventTypeUpdated, []WifiNetwork{net}))
			log.Printf("[chaos] RSSI flutter on %q → %d dBm", net.SSID, net.RSSI)
		} else {
			srv.state.mu.Unlock()
		}

	case 8:
		// Simulate IP lost then regained
		srv.state.mu.RLock()
		ssid := srv.state.ConnectedSSID
		ip := srv.state.ConnectedIP
		srv.state.mu.RUnlock()
		if ssid != "" {
			log.Printf("[chaos] simulating IP flap on %q", ssid)
			srv.broadcast(BuildWifiLostIpEvent(ip))
			time.Sleep(200 * time.Millisecond)
			srv.broadcast(BuildWifiGotIpEvent(ip))
		}

	case 9:
		// Burst scan status transitions to confuse state machines
		log.Println("[chaos] scan status burst")
		go func() {
			for _, s := range []Types.WifiScanStatus{
				Types.WifiScanStatusStarted,
				Types.WifiScanStatusInProgress,
				Types.WifiScanStatusInProgress,
				Types.WifiScanStatusError,
				Types.WifiScanStatusStarted,
				Types.WifiScanStatusCompleted,
			} {
				srv.broadcast(BuildWifiScanStatus(s))
				time.Sleep(50 * time.Millisecond)
			}
		}()
	}
}
