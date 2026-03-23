package cli

import "fmt"

// esc is the ANSI Control Sequence Introducer (CSI): ESC + [
const esc = "\033["

// ── Cursor movement ──────────────────────────────────────────────────────────

// CursorUp returns a sequence that moves the cursor up n rows.
func CursorUp(n int) string { return fmt.Sprintf("%s%dA", esc, n) }

// CursorDown returns a sequence that moves the cursor down n rows.
func CursorDown(n int) string { return fmt.Sprintf("%s%dB", esc, n) }

// CursorRight returns a sequence that moves the cursor right n columns.
func CursorRight(n int) string { return fmt.Sprintf("%s%dC", esc, n) }

// CursorLeft returns a sequence that moves the cursor left n columns.
func CursorLeft(n int) string { return fmt.Sprintf("%s%dD", esc, n) }

// CursorCol returns a sequence that moves the cursor to column n (1-based).
func CursorCol(n int) string { return fmt.Sprintf("%s%dG", esc, n) }

// ── Erase ────────────────────────────────────────────────────────────────────

const (
	EraseToEndOfLine   = esc + "K"  // EL0: from cursor to end of line
	EraseToStartOfLine = esc + "1K" // EL1: from start of line to cursor
	EraseLine          = esc + "2K" // EL2: entire current line
	EraseDown          = esc + "J"  // ED0: from cursor to end of screen
	EraseScreen        = esc + "2J" // ED2: entire screen
)

// ── Cursor visibility ────────────────────────────────────────────────────────

const (
	CursorHide = esc + "?25l"
	CursorShow = esc + "?25h"
)

// ── Cursor save / restore (DEC private, widely supported) ───────────────────

const (
	CursorSave    = "\0337"
	CursorRestore = "\0338"
)

// ── Carriage return ──────────────────────────────────────────────────────────

// CR moves the cursor to column 1 of the current line without advancing a row.
const CR = "\r"

// ── Composed helpers ─────────────────────────────────────────────────────────

// ClearLine moves to column 1 and erases to the end of the line.
// Use this to overwrite a prompt or status line in-place.
const ClearLine = CR + EraseToEndOfLine
