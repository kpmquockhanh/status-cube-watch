package mail

import (
	"bufio"
	"context"
	"net"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

type fakeClock struct{ t time.Time }

func (c *fakeClock) now() time.Time { return c.t }

// serve plays an IMAP server on the far end of a pipe. script reads the
// client's lines (read returns "" at EOF) and writes replies.
func serve(greet bool, script func(read func() string, write func(string))) (net.Conn, <-chan struct{}) {
	cli, srv := net.Pipe()
	done := make(chan struct{})
	go func() {
		defer close(done)
		defer srv.Close()
		br := bufio.NewReader(srv)
		read := func() string {
			line, err := br.ReadString('\n')
			if err != nil {
				return ""
			}
			return strings.TrimSuffix(line, "\r\n")
		}
		write := func(s string) { _, _ = srv.Write([]byte(s + "\r\n")) }
		if greet {
			write("* OK ready")
		}
		script(read, write)
	}()
	return cli, done
}

func newTestReader(clock *fakeClock, conns ...net.Conn) (*Reader, *int32) {
	var dials int32
	r := NewReader()
	r.Now = clock.now
	r.Log = func(string) {}
	r.Dial = func(ctx context.Context) (net.Conn, error) {
		i := int(atomic.AddInt32(&dials, 1)) - 1
		return conns[i], nil
	}
	return r, &dials
}

func rejectLogin(read func() string, write func(string)) {
	read()
	write("a1 NO")
}

func goodScript(t *testing.T, login, unseen string) func(func() string, func(string)) {
	return func(read func() string, write func(string)) {
		if got := read(); got != login {
			t.Errorf("login line = %q, want %q", got, login)
		}
		write("a1 OK")
		if got := read(); got != "a2 STATUS INBOX (UNSEEN)" {
			t.Errorf("status line = %q", got)
		}
		write("* STATUS INBOX (UNSEEN " + unseen + ")")
		write("a2 OK")
		if got := read(); got != "a3 LOGOUT" {
			t.Errorf("logout line = %q", got)
		}
		write("a3 OK")
	}
}

func TestUnseen(t *testing.T) {
	cli, done := serve(true, goodScript(t, `a1 LOGIN "u" "pw"`, "7"))
	r, _ := newTestReader(&fakeClock{t: time.Unix(1000, 0)}, cli)
	m := r.Get(context.Background(), "u", "p w")
	<-done
	if m == nil || m.Unread == nil || *m.Unread != 7 || m.Stale || m.Error != "" {
		t.Fatalf("got %+v", m)
	}
}

func TestQuote(t *testing.T) {
	var got string
	cli, done := serve(true, func(read func() string, write func(string)) {
		got = read()
		write("a1 NO")
	})
	r, _ := newTestReader(&fakeClock{t: time.Unix(1000, 0)}, cli)
	r.Get(context.Background(), `a"b\c`, "pw")
	<-done
	if want := `a1 LOGIN "a\"b\\c" "pw"`; got != want {
		t.Fatalf("login line = %q, want %q", got, want)
	}

	var sent []string
	cli2, done2 := serve(true, func(read func() string, write func(string)) {
		for l := read(); l != ""; l = read() {
			sent = append(sent, l)
		}
	})
	r2, _ := newTestReader(&fakeClock{t: time.Unix(1000, 0)}, cli2)
	m := r2.Get(context.Background(), "u\rx", "pw")
	<-done2
	if m.Error != "bad characters in credentials" || m.Unread != nil {
		t.Fatalf("got %+v", m)
	}
	if len(sent) != 0 {
		t.Fatalf("sent after greeting: %v", sent)
	}
}

func TestLoginFails(t *testing.T) {
	cli, done := serve(true, rejectLogin)
	r, _ := newTestReader(&fakeClock{t: time.Unix(1000, 0)}, cli)
	m := r.Get(context.Background(), "u", "pw")
	<-done
	if m.Error != "mail login failed" || m.Unread != nil {
		t.Fatalf("got %+v", m)
	}
}

func TestStaleKeepsCount(t *testing.T) {
	good, done := serve(true, goodScript(t, `a1 LOGIN "u" "pw"`, "7"))
	bad, done2 := serve(true, rejectLogin)
	clock := &fakeClock{t: time.Unix(1000, 0)}
	r, _ := newTestReader(clock, good, bad)
	r.Get(context.Background(), "u", "pw")
	<-done
	clock.t = clock.t.Add(61 * time.Second)
	m := r.Get(context.Background(), "u", "pw")
	<-done2
	if m.Unread == nil || *m.Unread != 7 || !m.Stale || m.Error == "" {
		t.Fatalf("got %+v", m)
	}
}

func TestCacheAndBackoff(t *testing.T) {
	bad, done := serve(true, rejectLogin)
	clock := &fakeClock{t: time.Unix(1000, 0)}
	r, dials := newTestReader(clock, bad)
	r.Get(context.Background(), "u", "pw")
	<-done
	clock.t = clock.t.Add(30 * time.Second)
	m := r.Get(context.Background(), "u", "pw")
	if n := atomic.LoadInt32(dials); n != 1 {
		t.Fatalf("dialled %d times", n)
	}
	if m.Error != "mail login failed" {
		t.Fatalf("got %+v", m)
	}
}

func TestTimeout(t *testing.T) {
	cli, done := serve(false, func(read func() string, write func(string)) { read() })
	r, _ := newTestReader(&fakeClock{t: time.Unix(1000, 0)}, cli)
	r.Timeout = 50 * time.Millisecond
	m := r.Get(context.Background(), "u", "pw")
	<-done
	if m.Error != "mail timeout" {
		t.Fatalf("got %+v", m)
	}
}

func TestNotConfigured(t *testing.T) {
	r, dials := newTestReader(&fakeClock{t: time.Unix(1000, 0)})
	if r.Get(context.Background(), "", "pw") != nil || r.Get(context.Background(), "u", "") != nil {
		t.Fatal("want nil")
	}
	if n := atomic.LoadInt32(dials); n != 0 {
		t.Fatal("dialled")
	}
}
