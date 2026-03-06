package main

import (
	"flag"
	"log"
	"net/http"
	"os"

	"openshock.dev/mock-portal/server"
)

func main() {
	addr       := flag.String("addr",   ":8080",              "listen address")
	configPath := flag.String("config", "mock-config.bin",    "FlatBuffers HubConfig state file (saved on mutations)")
	chaos      := flag.Bool("chaos",    false,                "start with chaos mode enabled")
	flag.Parse()

	log.SetFlags(log.Ltime | log.Lmsgprefix)

	state := server.NewState()

	if *configPath != "" {
		if err := state.LoadConfig(*configPath); err != nil {
			if !os.IsNotExist(err) {
				log.Printf("[config] load error: %v (starting with defaults)", err)
			} else {
				log.Printf("[config] no existing config at %s, starting fresh", *configPath)
			}
		} else {
			log.Printf("[config] loaded from %s", *configPath)
		}
	}

	srv := server.New(state, *configPath)
	srv.StartChaosEngine()
	if *chaos {
		srv.StartChaos()
		log.Println("[chaos] chaos mode enabled at startup")
	}

	log.Printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
	log.Printf("  OpenShock mock captive portal")
	log.Printf("  HTTP  : http://%s", *addr)
	log.Printf("  WS    : ws://%s/ws  (subprotocol: flatbuffers)", *addr)
	log.Printf("  RFC   : http://%s/captive-portal/api", *addr)
	log.Printf("  Config: %s", *configPath)
	log.Printf("  Type 'help' for runtime commands")
	log.Printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")

	go srv.RunCLI()

	if err := http.ListenAndServe(*addr, srv.Handler()); err != nil {
		log.Fatalf("server error: %v", err)
	}
}
