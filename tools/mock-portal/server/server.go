package server

import (
	"encoding/json"
	"log"
	"net/http"
	"sync"

	"github.com/gorilla/websocket"

	Types "openshock.dev/mock-portal/fbs/OpenShock/Serialization/Types"
)

var upgrader = websocket.Upgrader{
	CheckOrigin:  func(r *http.Request) bool { return true },
	Subprotocols: []string{"flatbuffers"},
}

type client struct {
	conn   *websocket.Conn
	outbox chan []byte
}

func (c *client) send(msg []byte) {
	select {
	case c.outbox <- msg:
	default:
		log.Println("[WS] outbox full, dropping message")
	}
}

type Server struct {
	state      *State
	mu         sync.Mutex
	clients    map[*client]struct{}
	chaos      *ChaosEngine
	configPath string
}

func New(state *State, configPath string) *Server {
	srv := &Server{
		state:      state,
		clients:    make(map[*client]struct{}),
		configPath: configPath,
	}
	srv.chaos = newChaosEngine(srv)
	return srv
}

// StartChaos enables chaos mode and starts the background goroutine.
func (srv *Server) StartChaos() {
	srv.chaos.Enable()
	go srv.chaos.Run()
}

// StartChaosEngine starts the chaos goroutine without enabling it yet.
// Chaos can be toggled at runtime via the CLI.
func (srv *Server) StartChaosEngine() {
	go srv.chaos.Run()
}

func (srv *Server) Handler() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/captive-portal/api", srv.handleCaptivePortalAPI)
	mux.HandleFunc("/ws", srv.handleWS)
	return mux
}

// GET /captive-portal/api  (RFC 8908)
func (srv *Server) handleCaptivePortalAPI(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	w.Header().Set("Content-Type", "application/captive+json")
	w.Header().Set("Cache-Control", "no-store, no-cache, must-revalidate")
	_ = json.NewEncoder(w).Encode(map[string]any{
		"captive":         true,
		"user-portal-url": "http://" + r.Host + "/",
		"venue-info-url":  "https://openshock.org",
	})
}

// GET /ws  (WebSocket, subprotocol: flatbuffers)
func (srv *Server) handleWS(w http.ResponseWriter, r *http.Request) {
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("[WS] upgrade error: %v", err)
		return
	}

	c := &client{
		conn:   conn,
		outbox: make(chan []byte, 64),
	}

	srv.mu.Lock()
	srv.clients[c] = struct{}{}
	srv.mu.Unlock()

	log.Printf("[WS] client connected: %s", conn.RemoteAddr())

	c.send(BuildReadyMessage(srv.state))

	srv.state.mu.RLock()
	nets := make([]WifiNetwork, len(srv.state.AvailableNetworks))
	copy(nets, srv.state.AvailableNetworks)
	srv.state.mu.RUnlock()
	for i := range nets {
		srv.state.mu.RLock()
		nets[i].Saved = srv.state.savedCredentialIndex(nets[i].SSID) >= 0
		srv.state.mu.RUnlock()
	}
	c.send(BuildWifiNetworkEvent(Types.WifiNetworkEventTypeDiscovered, nets))

	go srv.writePump(c)
	srv.readPump(c)
}

func (srv *Server) readPump(c *client) {
	defer func() {
		srv.mu.Lock()
		delete(srv.clients, c)
		srv.mu.Unlock()
		close(c.outbox)
		c.conn.Close()
		log.Printf("[WS] client disconnected: %s", c.conn.RemoteAddr())
	}()

	for {
		msgType, data, err := c.conn.ReadMessage()
		if err != nil {
			if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway, websocket.CloseNormalClosure) {
				log.Printf("[WS] read error: %v", err)
			}
			return
		}
		if msgType != websocket.BinaryMessage {
			log.Printf("[WS] unexpected message type %d, ignoring", msgType)
			continue
		}
		srv.handleMessage(c, data)
	}
}

func (srv *Server) writePump(c *client) {
	for msg := range c.outbox {
		if err := c.conn.WriteMessage(websocket.BinaryMessage, msg); err != nil {
			log.Printf("[WS] write error: %v", err)
			return
		}
	}
}

func (srv *Server) broadcast(msg []byte) {
	srv.mu.Lock()
	defer srv.mu.Unlock()
	for c := range srv.clients {
		c.send(msg)
	}
}

// broadcastRaw sends raw bytes to all clients with no FlatBuffers wrapping.
func (srv *Server) broadcastRaw(data []byte) {
	srv.mu.Lock()
	defer srv.mu.Unlock()
	for c := range srv.clients {
		select {
		case c.outbox <- data:
		default:
		}
	}
}
