// Command cube-bridge serves the Claude status payload to the cube.
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"os/signal"
	"syscall"

	"claude-status-cube/bridge/internal/config"
	"claude-status-cube/bridge/internal/limits"
	"claude-status-cube/bridge/internal/mail"
	"claude-status-cube/bridge/internal/server"
	"claude-status-cube/bridge/internal/sources"
)

// parseArgs returns the config path and code -1, or a usage exit code (usage
// already written to stderr). Positional arguments are rejected so that
// subcommands can be added later without changing what an unknown word means.
func parseArgs(args []string, stderr io.Writer) (path string, code int) {
	fs := flag.NewFlagSet("cube-bridge", flag.ContinueOnError)
	fs.SetOutput(stderr)
	p := fs.String("config", "config.json", "path to config.json (default: ./config.json in the working directory)")
	if err := fs.Parse(args); err != nil {
		if errors.Is(err, flag.ErrHelp) {
			return "", 0
		}
		return "", 2
	}
	if fs.NArg() > 0 {
		fmt.Fprintf(stderr, "cube-bridge: unexpected argument %q\nusage: cube-bridge [-config path]\n", fs.Arg(0))
		return "", 2
	}
	return *p, -1
}

func main() {
	path, code := parseArgs(os.Args[1:], os.Stderr)
	if code >= 0 {
		os.Exit(code)
	}
	errLog := log.New(os.Stderr, "", 0)
	cfg := config.Load(os.LookupEnv, config.ReadFile(path), func(s string) { errLog.Println(s) })

	var source sources.Source
	if cfg.Source == "admin" {
		admin, err := sources.NewAdmin(cfg.AdminKey, cfg.OAuthToken)
		if err != nil {
			errLog.Println(err)
			os.Exit(1)
		}
		source = admin
	} else {
		source = sources.NewLocal("")
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	s := server.New(cfg, server.Deps{
		Limits: limits.NewReader(),
		Mail:   mail.NewReader(func(s string) { errLog.Println(s) }),
		Source: source,
		ErrLog: errLog,
	})
	if err := s.Run(ctx); err != nil {
		errLog.Println(err)
		stop()
		os.Exit(1)
	}
}
