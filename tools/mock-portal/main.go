package main

import (
	"flag"
	"log"
	"net/http"
	"os"

	"github.com/brianvoe/gofakeit/v6"
	"openshock.dev/mock-portal/server"
	"openshock.dev/mock-portal/server/cli"
)

func main() {
	addr       := flag.String("addr",   ":8080",              "listen address")
	configPath := flag.String("config", "mock-config.bin",    "FlatBuffers HubConfig state file (saved on mutations)")
	chaos      := flag.Bool("chaos",    false,                "start with chaos mode enabled")
	flag.Parse()

	log.SetFlags(log.Ltime | log.Lmsgprefix)

	gofakeit.Seed(0) // 0 = time-based seed for per-run randomness

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
	if *chaos {
		srv.StartChaos()
		log.Println("[chaos] chaos mode enabled at startup")
	} else {
		srv.StartChaosEngine()
	}

	log.Printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
	log.Printf("  OpenShock mock captive portal")
	log.Printf("  HTTP  : http://%s", *addr)
	log.Printf("  WS    : ws://%s/ws  (subprotocol: flatbuffers)", *addr)
	log.Printf("  RFC   : http://%s/captive-portal/api", *addr)
	log.Printf("  Config: %s", *configPath)
	log.Printf("  Type 'help' for runtime commands")
	log.Printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")

	con := cli.NewConsole()
	go cli.RunCLI(srv, con)

	if err := http.ListenAndServe(*addr, srv.Handler()); err != nil {
		log.Fatalf("server error: %v", err)
	}
}
