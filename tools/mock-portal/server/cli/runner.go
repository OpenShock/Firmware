package cli

import (
	"bufio"
	"fmt"
	"log"
	"os"
	"strings"

	"openshock.dev/mock-portal/server"
)

// RunCLI runs the interactive command-line interface, blocking until "exit" /
// "quit" is entered or stdin reaches EOF.
//
// It installs the Console as the log output so that all log.Printf calls
// elsewhere in the server are routed through it (prompt stays intact).
func RunCLI(srv *server.Server, con *Console) {
	log.SetOutput(con)
	log.SetFlags(log.Ltime | log.Lmsgprefix)

	con.Prompt()
	scanner := bufio.NewScanner(os.Stdin)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		switch line {
		case "exit", "quit":
			fmt.Fprintln(con, "[CLI] shutting down")
			os.Exit(0)
		case "":
			// ignore blank lines
		default:
			buf := con.NewBuffer()
			srv.ExecCommand(line, buf)
			buf.Flush()
		}
		con.Prompt()
	}
}
