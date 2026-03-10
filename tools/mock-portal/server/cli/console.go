package cli

import (
	"fmt"
	"os"
	"strings"
	"sync"
)

// Console manages terminal I/O so that asynchronous log/output lines appear
// above a persistent input prompt without overwriting what the user is typing.
//
// Pass it to log.SetOutput to redirect the standard logger through it, and
// use it as an io.Writer for any command output that should respect the prompt.
//
//	con := cli.NewConsole()
//	log.SetOutput(con)
type Console struct {
	mu sync.Mutex
}

// NewConsole returns a ready-to-use Console.
func NewConsole() *Console {
	return &Console{}
}

// Write implements io.Writer.
//
// On each write it:
//  1. Moves to column 1 and erases the prompt line.
//  2. Prints the message (appending a newline if absent).
//  3. Redraws the prompt on the fresh line below.
//
// This keeps the "> " prompt anchored at the bottom while output scrolls
// above it, even when writes arrive from concurrent goroutines.
func (c *Console) Write(p []byte) (int, error) {
	c.mu.Lock()
	defer c.mu.Unlock()

	s := string(p)
	if len(s) > 0 && s[len(s)-1] != '\n' {
		s += "\n"
	}

	fmt.Fprintf(os.Stderr, ClearLine+"%s> ", s)
	return len(p), nil
}

// writeOutput writes output above the prompt without reprinting it.
// Used by Buffer.Flush so the caller (RunCLI) can draw the prompt once itself.
func (c *Console) writeOutput(p []byte) {
	c.mu.Lock()
	defer c.mu.Unlock()

	s := string(p)
	if len(s) > 0 && s[len(s)-1] != '\n' {
		s += "\n"
	}

	fmt.Fprintf(os.Stderr, ClearLine+"%s", s)
}

// Prompt draws the initial "> " prompt. Call once before starting the read loop.
func (c *Console) Prompt() {
	c.mu.Lock()
	defer c.mu.Unlock()
	fmt.Fprint(os.Stderr, "> ")
}

// NewBuffer returns a Buffer that accumulates writes and submits them to this
// Console in a single atomic operation on Flush. Use it for command output so
// that multi-line responses are painted above the prompt in one shot rather
// than flickering the prompt between each line.
func (c *Console) NewBuffer() *Buffer {
	return &Buffer{con: c}
}

// ── Buffer ────────────────────────────────────────────────────────────────────

// Buffer accumulates output from a command and submits it to its Console in
// one atomic write when Flush is called.
type Buffer struct {
	sb  strings.Builder
	con *Console
}

// Write implements io.Writer, appending p to the internal buffer.
func (b *Buffer) Write(p []byte) (int, error) {
	return b.sb.Write(p)
}

// Flush submits everything accumulated since the last Flush to the Console as
// a single write. The buffer is reset afterwards and can be reused.
// Does nothing if the buffer is empty.
func (b *Buffer) Flush() {
	s := b.sb.String()
	b.sb.Reset()
	if s == "" {
		return
	}
	b.con.writeOutput([]byte(s))
}
