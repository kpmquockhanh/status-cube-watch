// Package mail reads the unread-inbox count for the usage card's badge.
package mail

// Mail is the last unread count. Unread nil = never had a reading.
type Mail struct {
	Unread *int
	Stale  bool
	Error  string
}
