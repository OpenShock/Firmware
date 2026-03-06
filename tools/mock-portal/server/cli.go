package server

import (
	"bufio"
	"fmt"
	"math/rand"
	"os"
	"strconv"
	"strings"

	"github.com/brianvoe/gofakeit/v6"

	Types "openshock.dev/mock-portal/fbs/OpenShock/Serialization/Types"
)

const cliHelp = `
OpenShock mock portal — runtime commands
─────────────────────────────────────────
  help                          this help
  status                        dump current state summary
  clients                       list connected WS clients

  ── Network list ────────────────────────────────────────────
  add-networks [n]              add n random networks, broadcast Discovered (default 10)
  set-networks <n>              replace ALL networks with n fresh random ones
  remove-network <ssid>         remove a network by SSID and broadcast Lost
  list-networks                 print all current networks
  clear-networks                remove all networks and broadcast Lost for each

  ── Single-network manipulation ─────────────────────────────
  network add <ssid> [opts]     add a specific network and broadcast Discovered
    opts: rssi=<dBm>  channel=<1-13|36-165>  auth=<open|wep|wpa|wpa2|wpa3|
          wpa2wpa3|enterprise|wapi|unknown>  saved  hidden
  network update <ssid> [opts]  change properties in-place, broadcast Updated
    (same opts as above)
  network rssi <ssid> <dBm>     quickly set RSSI and broadcast Updated
  network auth <ssid> <mode>    quickly set auth mode and broadcast Updated
  network save <ssid>           mark as saved, broadcast Saved
  network forget <ssid>         mark as unsaved, broadcast Removed

  ── Mesh / multi-AP ──────────────────────────────────────────
  mesh <ssid> [count]           add count APs sharing the same SSID with
                                varying BSSIDs, channels, RSSI (default 3)
  mesh-roam <ssid>              simulate a roam: update each mesh node's RSSI
                                so a different AP becomes strongest

  ── WiFi events ─────────────────────────────────────────────
  wifi-lost                     simulate IP loss + Disconnected event
  wifi-got <ssid>               simulate IP assignment + Connected event
  scan                          trigger a fresh scan cycle to all clients

  ── Fault injection ──────────────────────────────────────────
  error <message>               broadcast ErrorMessage to all clients
  disconnect                    close all WS connections
  spike [n]                     flood n rapid WifiNetworkEvents (default 100)
  corrupt [n]                   send n malformed binary frames (default 1)
  scan-fail [status]            make next scan fail (Error|TimedOut|Aborted)
  chaos [on|off]                toggle automatic random fault injection
`

func (srv *Server) RunCLI() {
	scanner := bufio.NewScanner(os.Stdin)
	fmt.Fprint(os.Stderr, "\n> ")
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line != "" {
			srv.execCommand(line)
		}
		fmt.Fprint(os.Stderr, "> ")
	}
}

func (srv *Server) execCommand(line string) {
	parts := strings.Fields(line)
	if len(parts) == 0 {
		return
	}
	cmd, args := parts[0], parts[1:]

	switch cmd {
	case "help":
		fmt.Fprint(os.Stderr, cliHelp)

	case "status":
		srv.cmdStatus()

	case "clients":
		srv.cmdClients()

	// ── Network list ─────────────────────────────────────────────────────────

	case "add-networks":
		n := 10
		if len(args) > 0 {
			if v, err := strconv.Atoi(args[0]); err == nil && v > 0 {
				n = v
			}
		}
		nets := GenerateNetworks(n)
		srv.state.mu.Lock()
		srv.state.AvailableNetworks = append(srv.state.AvailableNetworks, nets...)
		srv.state.mu.Unlock()
		srv.broadcast(BuildWifiNetworkEvent(Types.WifiNetworkEventTypeDiscovered, nets))
		fmt.Fprintf(os.Stderr, "[CLI] added %d networks (total %d)\n", n, len(srv.state.AvailableNetworks))

	case "set-networks":
		if len(args) == 0 {
			fmt.Fprintln(os.Stderr, "usage: set-networks <n>")
			return
		}
		n, err := strconv.Atoi(args[0])
		if err != nil || n < 0 {
			fmt.Fprintln(os.Stderr, "usage: set-networks <n>")
			return
		}
		srv.state.mu.Lock()
		old := srv.state.AvailableNetworks
		nets := GenerateNetworks(n)
		srv.state.AvailableNetworks = nets
		srv.state.mu.Unlock()
		// Broadcast Lost for old, Discovered for new
		srv.broadcast(BuildWifiNetworkEvent(Types.WifiNetworkEventTypeLost, old))
		if len(nets) > 0 {
			srv.broadcast(BuildWifiNetworkEvent(Types.WifiNetworkEventTypeDiscovered, nets))
		}
		fmt.Fprintf(os.Stderr, "[CLI] replaced with %d networks\n", n)

	case "remove-network":
		if len(args) == 0 {
			fmt.Fprintln(os.Stderr, "usage: remove-network <ssid>")
			return
		}
		ssid := strings.Join(args, " ")
		srv.state.mu.Lock()
		found := false
		for i, n := range srv.state.AvailableNetworks {
			if n.SSID == ssid {
				srv.state.AvailableNetworks = append(srv.state.AvailableNetworks[:i], srv.state.AvailableNetworks[i+1:]...)
				found = true
				break
			}
		}
		srv.state.mu.Unlock()
		if !found {
			fmt.Fprintf(os.Stderr, "[CLI] network %q not found\n", ssid)
			return
		}
		srv.broadcast(BuildWifiNetworkEvent(Types.WifiNetworkEventTypeLost, []WifiNetwork{{SSID: ssid}}))
		fmt.Fprintf(os.Stderr, "[CLI] removed %q\n", ssid)

	case "list-networks":
		srv.state.mu.RLock()
		defer srv.state.mu.RUnlock()
		fmt.Fprintf(os.Stderr, "[networks] %d total:\n", len(srv.state.AvailableNetworks))
		for i, n := range srv.state.AvailableNetworks {
			ssid := n.SSID
			if ssid == "" {
				ssid = "<hidden>"
			}
			fmt.Fprintf(os.Stderr, "  %3d  %-34q  ch%-3d  %4ddBm  %-14s  saved=%v\n",
				i+1, ssid, n.Channel, n.RSSI, authModeName(n.AuthMode), n.Saved)
		}

	case "clear-networks":
		srv.state.mu.Lock()
		old := srv.state.AvailableNetworks
		srv.state.AvailableNetworks = nil
		srv.state.mu.Unlock()
		if len(old) > 0 {
			srv.broadcast(BuildWifiNetworkEvent(Types.WifiNetworkEventTypeLost, old))
		}
		fmt.Fprintf(os.Stderr, "[CLI] cleared %d networks\n", len(old))

	// ── Single-network manipulation ───────────────────────────────────────────

	case "network":
		if len(args) == 0 {
			fmt.Fprintln(os.Stderr, "usage: network <add|update|rssi|auth|save|forget> ...")
			return
		}
		srv.cmdNetwork(args)

	// ── Mesh ──────────────────────────────────────────────────────────────────

	case "mesh":
		if len(args) == 0 {
			fmt.Fprintln(os.Stderr, "usage: mesh <ssid> [count]")
			return
		}
		ssid := args[0]
		count := 3
		if len(args) > 1 {
			if v, err := strconv.Atoi(args[1]); err == nil && v > 0 {
				count = v
			}
		}
		nets := makeMeshNodes(ssid, count)
		srv.state.mu.Lock()
		srv.state.AvailableNetworks = append(srv.state.AvailableNetworks, nets...)
		srv.state.mu.Unlock()
		srv.broadcast(BuildWifiNetworkEvent(Types.WifiNetworkEventTypeDiscovered, nets))
		fmt.Fprintf(os.Stderr, "[CLI] mesh %q: added %d nodes\n", ssid, count)

	case "mesh-roam":
		if len(args) == 0 {
			fmt.Fprintln(os.Stderr, "usage: mesh-roam <ssid>")
			return
		}
		ssid := args[0]
		srv.cmdMeshRoam(ssid)

	// ── WiFi events ───────────────────────────────────────────────────────────

	case "wifi-lost":
		srv.handleWifiDisconnect(nil)

	case "wifi-got":
		if len(args) == 0 {
			fmt.Fprintln(os.Stderr, "usage: wifi-got <ssid>")
			return
		}
		ssid := strings.Join(args, " ")
		srv.state.mu.Lock()
		srv.state.ConnectedSSID = ssid
		srv.state.ConnectedIP = "192.168.4.2"
		ip := srv.state.ConnectedIP
		var net WifiNetwork
		for _, n := range srv.state.AvailableNetworks {
			if n.SSID == ssid {
				net = n
				break
			}
		}
		if net.SSID == "" {
			net = WifiNetwork{SSID: ssid, Saved: true}
		}
		srv.state.mu.Unlock()
		srv.broadcast(BuildWifiNetworkEvent(Types.WifiNetworkEventTypeConnected, []WifiNetwork{net}))
		srv.broadcast(BuildWifiGotIpEvent(ip))
		fmt.Fprintf(os.Stderr, "[CLI] simulated connect to %q, IP=%s\n", ssid, ip)

	case "scan":
		go srv.handleWifiScan(nil, true)

	// ── Fault injection ───────────────────────────────────────────────────────

	case "error":
		if len(args) == 0 {
			fmt.Fprintln(os.Stderr, "usage: error <message>")
			return
		}
		msg := strings.Join(args, " ")
		srv.broadcast(BuildErrorMessage(msg))
		fmt.Fprintf(os.Stderr, "[CLI] broadcast error: %q\n", msg)

	case "disconnect":
		srv.cmdDisconnectAll()

	case "spike":
		n := 100
		if len(args) > 0 {
			if v, err := strconv.Atoi(args[0]); err == nil && v > 0 {
				n = v
			}
		}
		go srv.cmdSpike(n)
		fmt.Fprintf(os.Stderr, "[CLI] spiking %d events\n", n)

	case "corrupt":
		n := 1
		if len(args) > 0 {
			if v, err := strconv.Atoi(args[0]); err == nil && v > 0 {
				n = v
			}
		}
		srv.cmdCorrupt(n)
		fmt.Fprintf(os.Stderr, "[CLI] sent %d corrupt frames\n", n)

	case "scan-fail":
		status := Types.WifiScanStatusError
		if len(args) > 0 {
			switch strings.ToLower(args[0]) {
			case "timedout", "timed-out":
				status = Types.WifiScanStatusTimedOut
			case "aborted":
				status = Types.WifiScanStatusAborted
			case "error":
				status = Types.WifiScanStatusError
			default:
				fmt.Fprintf(os.Stderr, "[CLI] unknown status %q, using Error\n", args[0])
			}
		}
		srv.chaos.SetNextScanFail(status)
		fmt.Fprintf(os.Stderr, "[CLI] next scan will fail with status %d\n", status)

	case "chaos":
		toggle := ""
		if len(args) > 0 {
			toggle = strings.ToLower(args[0])
		}
		switch toggle {
		case "on":
			srv.chaos.Enable()
			fmt.Fprintln(os.Stderr, "[CLI] chaos mode ON")
		case "off":
			srv.chaos.Disable()
			fmt.Fprintln(os.Stderr, "[CLI] chaos mode OFF")
		default:
			if srv.chaos.IsEnabled() {
				srv.chaos.Disable()
				fmt.Fprintln(os.Stderr, "[CLI] chaos mode OFF")
			} else {
				srv.chaos.Enable()
				fmt.Fprintln(os.Stderr, "[CLI] chaos mode ON")
			}
		}

	default:
		fmt.Fprintf(os.Stderr, "[CLI] unknown command %q — type 'help'\n", cmd)
	}
}

// ── network subcommand ────────────────────────────────────────────────────────

func (srv *Server) cmdNetwork(args []string) {
	sub, rest := args[0], args[1:]

	switch sub {
	case "add":
		if len(rest) == 0 {
			fmt.Fprintln(os.Stderr, "usage: network add <ssid> [opts]")
			return
		}
		net := parseNetworkOpts(rest[0], rest[1:])
		srv.state.mu.Lock()
		srv.state.AvailableNetworks = append(srv.state.AvailableNetworks, net)
		srv.state.mu.Unlock()
		srv.broadcast(BuildWifiNetworkEvent(Types.WifiNetworkEventTypeDiscovered, []WifiNetwork{net}))
		fmt.Fprintf(os.Stderr, "[CLI] added %q ch%d %ddBm auth=%s saved=%v\n",
			net.SSID, net.Channel, net.RSSI, authModeName(net.AuthMode), net.Saved)

	case "update":
		if len(rest) == 0 {
			fmt.Fprintln(os.Stderr, "usage: network update <ssid> [opts]")
			return
		}
		ssid := rest[0]
		srv.state.mu.Lock()
		idx := -1
		for i, n := range srv.state.AvailableNetworks {
			if n.SSID == ssid {
				idx = i
				break
			}
		}
		if idx < 0 {
			srv.state.mu.Unlock()
			fmt.Fprintf(os.Stderr, "[CLI] network %q not found\n", ssid)
			return
		}
		applyNetworkOpts(&srv.state.AvailableNetworks[idx], rest[1:])
		net := srv.state.AvailableNetworks[idx]
		srv.state.mu.Unlock()
		srv.broadcast(BuildWifiNetworkEvent(Types.WifiNetworkEventTypeUpdated, []WifiNetwork{net}))
		fmt.Fprintf(os.Stderr, "[CLI] updated %q\n", ssid)

	case "rssi":
		if len(rest) < 2 {
			fmt.Fprintln(os.Stderr, "usage: network rssi <ssid> <dBm>")
			return
		}
		ssid := rest[0]
		val, err := strconv.Atoi(rest[1])
		if err != nil {
			fmt.Fprintln(os.Stderr, "[CLI] invalid rssi value")
			return
		}
		srv.updateNetworkField(ssid, func(n *WifiNetwork) { n.RSSI = int8(val) })

	case "auth":
		if len(rest) < 2 {
			fmt.Fprintln(os.Stderr, "usage: network auth <ssid> <mode>")
			return
		}
		ssid := rest[0]
		mode, ok := parseAuthMode(rest[1])
		if !ok {
			fmt.Fprintf(os.Stderr, "[CLI] unknown auth mode %q\n", rest[1])
			return
		}
		srv.updateNetworkField(ssid, func(n *WifiNetwork) { n.AuthMode = mode })

	case "save":
		if len(rest) == 0 {
			fmt.Fprintln(os.Stderr, "usage: network save <ssid>")
			return
		}
		ssid := rest[0]
		srv.updateNetworkField(ssid, func(n *WifiNetwork) { n.Saved = true })
		srv.broadcast(BuildWifiNetworkEvent(Types.WifiNetworkEventTypeSaved, []WifiNetwork{{SSID: ssid, Saved: true}}))

	case "forget":
		if len(rest) == 0 {
			fmt.Fprintln(os.Stderr, "usage: network forget <ssid>")
			return
		}
		ssid := rest[0]
		srv.updateNetworkField(ssid, func(n *WifiNetwork) { n.Saved = false })
		srv.broadcast(BuildWifiNetworkEvent(Types.WifiNetworkEventTypeRemoved, []WifiNetwork{{SSID: ssid}}))

	default:
		fmt.Fprintf(os.Stderr, "[CLI] unknown network subcommand %q\n", sub)
	}
}

func (srv *Server) updateNetworkField(ssid string, fn func(*WifiNetwork)) {
	srv.state.mu.Lock()
	var updated WifiNetwork
	found := false
	for i := range srv.state.AvailableNetworks {
		if srv.state.AvailableNetworks[i].SSID == ssid {
			fn(&srv.state.AvailableNetworks[i])
			updated = srv.state.AvailableNetworks[i]
			found = true
			break
		}
	}
	srv.state.mu.Unlock()
	if !found {
		fmt.Fprintf(os.Stderr, "[CLI] network %q not found\n", ssid)
		return
	}
	srv.broadcast(BuildWifiNetworkEvent(Types.WifiNetworkEventTypeUpdated, []WifiNetwork{updated}))
	fmt.Fprintf(os.Stderr, "[CLI] updated %q\n", ssid)
}

// ── Mesh helpers ──────────────────────────────────────────────────────────────

func makeMeshNodes(ssid string, count int) []WifiNetwork {
	// Spread across 3 non-overlapping channels; RSSI descends so first node is "nearest"
	channels := []uint8{1, 6, 11, 36, 40, 44, 48}
	nets := make([]WifiNetwork, count)
	for i := range nets {
		rssi := int8(-40 - i*10 - rand.Intn(5))
		nets[i] = WifiNetwork{
			SSID:     ssid,
			BSSID:    randomBSSID(),
			Channel:  channels[i%len(channels)],
			RSSI:     rssi,
			AuthMode: Types.WifiAuthModeWPA2_PSK,
			Saved:    false,
		}
	}
	return nets
}

func (srv *Server) cmdMeshRoam(ssid string) {
	srv.state.mu.Lock()
	var nodes []int
	for i, n := range srv.state.AvailableNetworks {
		if n.SSID == ssid {
			nodes = append(nodes, i)
		}
	}
	if len(nodes) < 2 {
		srv.state.mu.Unlock()
		fmt.Fprintf(os.Stderr, "[CLI] fewer than 2 mesh nodes for %q\n", ssid)
		return
	}
	// Shuffle RSSI values among nodes to simulate roam
	rssis := make([]int8, len(nodes))
	for i, idx := range nodes {
		rssis[i] = srv.state.AvailableNetworks[idx].RSSI
	}
	rand.Shuffle(len(rssis), func(i, j int) { rssis[i], rssis[j] = rssis[j], rssis[i] })
	updated := make([]WifiNetwork, len(nodes))
	for i, idx := range nodes {
		srv.state.AvailableNetworks[idx].RSSI = rssis[i]
		updated[i] = srv.state.AvailableNetworks[idx]
	}
	srv.state.mu.Unlock()
	srv.broadcast(BuildWifiNetworkEvent(Types.WifiNetworkEventTypeUpdated, updated))
	fmt.Fprintf(os.Stderr, "[CLI] mesh-roam on %q (%d nodes updated)\n", ssid, len(nodes))
}

// ── Option parsing ────────────────────────────────────────────────────────────

// parseNetworkOpts builds a WifiNetwork from a base SSID and key=value tokens.
// Recognised opts: rssi=<n>  channel=<n>  auth=<mode>  saved  hidden
func parseNetworkOpts(ssid string, opts []string) WifiNetwork {
	net := WifiNetwork{
		SSID:     ssid,
		BSSID:    randomBSSID(),
		Channel:  uint8(gofakeit.Number(1, 13)),
		RSSI:     int8(-gofakeit.Number(30, 90)),
		AuthMode: Types.WifiAuthModeWPA2_PSK,
	}
	for _, opt := range opts {
		if opt == "saved" {
			net.Saved = true
			continue
		}
		if opt == "hidden" {
			net.SSID = ""
			continue
		}
		kv := strings.SplitN(opt, "=", 2)
		if len(kv) != 2 {
			continue
		}
		applyKV(&net, kv[0], kv[1])
	}
	return net
}

// applyNetworkOpts applies opts in-place to an existing WifiNetwork.
func applyNetworkOpts(net *WifiNetwork, opts []string) {
	for _, opt := range opts {
		if opt == "saved" {
			net.Saved = true
			continue
		}
		if opt == "unsaved" {
			net.Saved = false
			continue
		}
		if opt == "hidden" {
			net.SSID = ""
			continue
		}
		kv := strings.SplitN(opt, "=", 2)
		if len(kv) == 2 {
			applyKV(net, kv[0], kv[1])
		}
	}
}

func applyKV(net *WifiNetwork, key, val string) {
	switch strings.ToLower(key) {
	case "rssi":
		if v, err := strconv.Atoi(val); err == nil {
			net.RSSI = int8(v)
		}
	case "channel", "ch":
		if v, err := strconv.Atoi(val); err == nil {
			net.Channel = uint8(v)
		}
	case "auth":
		if mode, ok := parseAuthMode(val); ok {
			net.AuthMode = mode
		}
	case "bssid":
		net.BSSID = val
	}
}

func parseAuthMode(s string) (Types.WifiAuthMode, bool) {
	switch strings.ToLower(s) {
	case "open":
		return Types.WifiAuthModeOpen, true
	case "wep":
		return Types.WifiAuthModeWEP, true
	case "wpa", "wpa1", "wpa_psk":
		return Types.WifiAuthModeWPA_PSK, true
	case "wpa2", "wpa2_psk":
		return Types.WifiAuthModeWPA2_PSK, true
	case "wpa_wpa2", "wpa_wpa2_psk":
		return Types.WifiAuthModeWPA_WPA2_PSK, true
	case "enterprise", "wpa2_enterprise":
		return Types.WifiAuthModeWPA2_ENTERPRISE, true
	case "wpa3", "wpa3_psk":
		return Types.WifiAuthModeWPA3_PSK, true
	case "wpa2wpa3", "wpa2_wpa3", "wpa2_wpa3_psk":
		return Types.WifiAuthModeWPA2_WPA3_PSK, true
	case "wapi", "wapi_psk":
		return Types.WifiAuthModeWAPI_PSK, true
	case "unknown":
		return Types.WifiAuthModeUNKNOWN, true
	}
	return 0, false
}

func authModeName(m Types.WifiAuthMode) string {
	switch m {
	case Types.WifiAuthModeOpen:
		return "open"
	case Types.WifiAuthModeWEP:
		return "wep"
	case Types.WifiAuthModeWPA_PSK:
		return "wpa"
	case Types.WifiAuthModeWPA2_PSK:
		return "wpa2"
	case Types.WifiAuthModeWPA_WPA2_PSK:
		return "wpa+wpa2"
	case Types.WifiAuthModeWPA2_ENTERPRISE:
		return "enterprise"
	case Types.WifiAuthModeWPA3_PSK:
		return "wpa3"
	case Types.WifiAuthModeWPA2_WPA3_PSK:
		return "wpa2+wpa3"
	case Types.WifiAuthModeWAPI_PSK:
		return "wapi"
	default:
		return "unknown"
	}
}

func (srv *Server) cmdStatus() {
	srv.state.mu.RLock()
	defer srv.state.mu.RUnlock()
	srv.mu.Lock()
	nClients := len(srv.clients)
	srv.mu.Unlock()
	fmt.Fprintf(os.Stderr,
		"[status] clients=%d networks=%d connected=%q ip=%q linked=%v chaos=%v\n",
		nClients,
		len(srv.state.AvailableNetworks),
		srv.state.ConnectedSSID,
		srv.state.ConnectedIP,
		srv.state.AccountLinked,
		srv.chaos.IsEnabled(),
	)
}

func (srv *Server) cmdClients() {
	srv.mu.Lock()
	defer srv.mu.Unlock()
	if len(srv.clients) == 0 {
		fmt.Fprintln(os.Stderr, "[clients] none connected")
		return
	}
	for c := range srv.clients {
		fmt.Fprintf(os.Stderr, "  %s\n", c.conn.RemoteAddr())
	}
}

func (srv *Server) cmdDisconnectAll() {
	srv.mu.Lock()
	conns := make([]*client, 0, len(srv.clients))
	for c := range srv.clients {
		conns = append(conns, c)
	}
	srv.mu.Unlock()
	for _, c := range conns {
		c.conn.Close()
	}
	fmt.Fprintf(os.Stderr, "[CLI] disconnected %d client(s)\n", len(conns))
}

func (srv *Server) cmdSpike(n int) {
	nets := GenerateNetworks(5)
	for range n {
		// Alternate between a few event types to really stress the client
		switch n % 3 {
		case 0:
			srv.broadcast(BuildWifiNetworkEvent(Types.WifiNetworkEventTypeDiscovered, GenerateNetworks(3)))
		case 1:
			srv.broadcast(BuildWifiNetworkEvent(Types.WifiNetworkEventTypeUpdated, nets))
		case 2:
			srv.broadcast(BuildWifiScanStatus(Types.WifiScanStatusInProgress))
		}
		n--
	}
}

func (srv *Server) cmdCorrupt(n int) {
	garbage := [][]byte{
		{0x00, 0x00, 0x00, 0x00},           // all zeros
		{0xFF, 0xFE, 0xFD, 0xFC},           // all ones-ish
		{0x04, 0x00, 0x00, 0x00, 0xAB},     // valid size prefix, garbage body
		[]byte("not flatbuffers at all"),   // text in binary frame
		{},                                  // empty frame
	}
	for i := range n {
		srv.broadcastRaw(garbage[i%len(garbage)])
	}
}
