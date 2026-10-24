// Command cube-bridge serves the Claude status payload to the cube.
package main

import (
	"context"
	"flag"
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

func main() {
	path := flag.String("config", "config.json", "path to config.json")
	flag.Parse()
	errLog := log.New(os.Stderr, "", 0)
	cfg := config.Load(os.LookupEnv, config.ReadFile(*path), func(s string) { errLog.Println(s) })

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
		Mail:   mail.NewReader(),
		Source: source,
		ErrLog: errLog,
	})
	if err := s.Run(ctx); err != nil {
		errLog.Println(err)
		stop()
		os.Exit(1)
	}
}
