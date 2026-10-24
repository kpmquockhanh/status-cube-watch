package mail

import (
	"bufio"
	"context"
	"crypto/tls"
	"errors"
	"fmt"
	"log"
	"net"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"
)

const (
	imapHost     = "imap.gmail.com"
	imapAddr     = "imap.gmail.com:993"
	cacheFor     = 60 * time.Second
	fetchTimeout = 8 * time.Second
)

var statusRe = regexp.MustCompile(`(?i)^\* STATUS .*\(UNSEEN (\d+)\)`)

// Reader reads the inbox's unseen count over IMAP, like the Node bridge did: one
// connection per fetch, a 60 s cache that also backs off after a failure, and
// the last good count kept (stale) when a fetch fails.
type Reader struct {
	Dial    func(ctx context.Context) (net.Conn, error)
	Now     func() time.Time
	Timeout time.Duration
	Log     func(string)

	mu    sync.Mutex
	at    time.Time
	cache *Mail
}

// NewReader returns a Reader with the production defaults.
// NewReader's logf receives the reader's log lines (nil = the standard logger).
func NewReader(logf func(string)) *Reader {
	if logf == nil {
		logf = func(s string) { log.Println(s) }
	}
	return &Reader{
		Dial: func(ctx context.Context) (net.Conn, error) {
			d := tls.Dialer{Config: &tls.Config{ServerName: imapHost}}
			return d.DialContext(ctx, "tcp", imapAddr)
		},
		Now:     time.Now,
		Timeout: fetchTimeout,
		Log:     logf,
	}
}

// Get returns the unread count, or nil when mail is not configured.
func (r *Reader) Get(ctx context.Context, user, password string) *Mail {
	if user == "" || password == "" {
		return nil
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	now := r.Now()
	if r.cache != nil && now.Sub(r.at) < cacheFor {
		return r.cache
	}
	// Google displays app passwords in groups separated by spaces.
	n, err := r.fetch(ctx, user, strings.Join(strings.Fields(password), ""))
	if err == nil {
		r.cache = &Mail{Unread: &n}
	} else {
		msg := err.Error()
		r.Log("mail fetch failed: " + msg)
		m := &Mail{Error: msg}
		if r.cache != nil && r.cache.Unread != nil {
			m.Unread, m.Stale = r.cache.Unread, true
		}
		r.cache = m
	}
	r.at = now
	return r.cache
}

// quote makes an IMAP quoted string. CR/LF cannot be quoted.
func quote(s string) (string, error) {
	if strings.ContainsAny(s, "\r\n") {
		return "", errors.New("bad characters in credentials")
	}
	s = strings.ReplaceAll(s, `\`, `\\`)
	s = strings.ReplaceAll(s, `"`, `\"`)
	return `"` + s + `"`, nil
}

func (r *Reader) fetch(ctx context.Context, user, password string) (int, error) {
	timeout := r.Timeout
	if timeout <= 0 {
		timeout = fetchTimeout
	}
	ctx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()
	conn, err := r.Dial(ctx)
	if err != nil {
		return 0, mapErr(err)
	}
	defer conn.Close()
	_ = conn.SetDeadline(time.Now().Add(timeout))

	br := bufio.NewReader(conn)
	line := func() (string, error) {
		s, err := br.ReadString('\n')
		if err != nil {
			return "", mapErr(err)
		}
		return strings.TrimRight(s, "\r\n"), nil
	}
	send := func(step int, cmd string) error {
		_, err := fmt.Fprintf(conn, "a%d %s\r\n", step, cmd)
		return mapErr(err)
	}
	// reply reads lines until the tagged completion; returns the unseen count
	// seen on the way (-1 if none) and whether the tag was OK.
	reply := func(step int) (int, bool, error) {
		unseen, tag := -1, "a"+strconv.Itoa(step)+" "
		for {
			l, err := line()
			if err != nil {
				return 0, false, err
			}
			if m := statusRe.FindStringSubmatch(l); m != nil {
				unseen, _ = strconv.Atoi(m[1])
			}
			if strings.HasPrefix(l, tag) {
				return unseen, strings.HasPrefix(l, tag+"OK"), nil
			}
		}
	}

	// Greeting: skip lines until "* OK".
	for {
		l, err := line()
		if err != nil {
			return 0, err
		}
		if strings.HasPrefix(l, "* OK") {
			break
		}
	}
	qu, err := quote(user)
	if err != nil {
		return 0, err
	}
	qp, err := quote(password)
	if err != nil {
		return 0, err
	}
	if err := send(1, "LOGIN "+qu+" "+qp); err != nil {
		return 0, err
	}
	if _, ok, err := reply(1); err != nil {
		return 0, err
	} else if !ok {
		return 0, errors.New("mail login failed")
	}
	if err := send(2, "STATUS INBOX (UNSEEN)"); err != nil {
		return 0, err
	}
	unseen, ok, err := reply(2)
	if err != nil {
		return 0, err
	}
	if !ok || unseen < 0 {
		return 0, errors.New("mail status failed")
	}
	if err := send(3, "LOGOUT"); err != nil {
		return 0, err
	}
	if _, _, err := reply(3); err != nil {
		return 0, err
	}
	return unseen, nil
}

func isTimeout(err error) bool {
	var ne net.Error
	return errors.As(err, &ne) && ne.Timeout() || errors.Is(err, context.DeadlineExceeded)
}

// mapErr turns transport errors into the messages the Node bridge reported.
func mapErr(err error) error {
	if err == nil {
		return nil
	}
	var dns *net.DNSError
	switch {
	case isTimeout(err):
		return errors.New("mail timeout")
	case errors.As(err, &dns):
		return errors.New("mail offline")
	case errors.Is(err, net.ErrClosed), strings.Contains(err.Error(), "EOF"), strings.Contains(err.Error(), "closed pipe"):
		return errors.New("mail connection closed")
	}
	return err
}
