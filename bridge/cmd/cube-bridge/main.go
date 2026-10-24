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
	"path/filepath"
	"syscall"

	"claude-status-cube/bridge/internal/agents"
	"claude-status-cube/bridge/internal/config"
	"claude-status-cube/bridge/internal/hooksettings"
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

// command is what the command line asks for: kind "serve" (config is set),
// "hook" (port is set) or "hooks" (action, settings and port are set).
type command struct {
	kind     string
	config   string
	port     int
	action   string // hooks: install | uninstall
	settings string // hooks: path of Claude Code's settings.json
}

const hooksUsage = "usage: cube-bridge hooks install|uninstall [--port N] [--settings PATH]"

func parseHooks(args []string, stderr io.Writer) (command, int) {
	if len(args) == 0 || (args[0] != "install" && args[0] != "uninstall") {
		fmt.Fprintln(stderr, hooksUsage)
		return command{}, 2
	}
	defSettings := "~/.claude/settings.json"
	home, homeErr := os.UserHomeDir()
	if homeErr == nil {
		defSettings = filepath.Join(home, ".claude", "settings.json")
	}
	fs := flag.NewFlagSet("cube-bridge hooks "+args[0], flag.ContinueOnError)
	fs.SetOutput(stderr)
	port := fs.Int("port", 8787, "port of the running bridge")
	settings := fs.String("settings", defSettings, "Claude Code settings file")
	if err := fs.Parse(args[1:]); err != nil {
		if errors.Is(err, flag.ErrHelp) {
			return command{}, 0
		}
		return command{}, 2
	}
	if fs.NArg() > 0 || *port < 1 || *port > 65535 || *settings == "" {
		fmt.Fprintln(stderr, hooksUsage)
		return command{}, 2
	}
	if homeErr != nil && *settings == defSettings {
		fmt.Fprintf(stderr, "cube-bridge: no home directory (%v); pass --settings\n%s\n", homeErr, hooksUsage)
		return command{}, 2
	}
	return command{kind: "hooks", action: args[0], port: *port, settings: *settings}, -1
}

// runHooks installs or removes the bridge's hooks in c.settings, pointing them
// at this binary (symlinks resolved). It returns the exit code.
func runHooks(c command, stdout, stderr io.Writer) int {
	var err error
	if c.action == "install" {
		var exe string
		if exe, err = os.Executable(); err == nil {
			exe, err = filepath.EvalSymlinks(exe)
		}
		if err == nil {
			err = hooksettings.Install(c.settings, hooksettings.Command(exe, c.port))
		}
	} else {
		err = hooksettings.Uninstall(c.settings)
	}
	if err != nil {
		fmt.Fprintf(stderr, "cube-bridge: hooks %s: %v\n", c.action, err)
		return 1
	}
	if c.action == "install" {
		fmt.Fprintf(stdout, "installed the bridge's hooks in %s (port %d)\n", c.settings, c.port)
	} else {
		fmt.Fprintf(stdout, "removed the bridge's hooks from %s\n", c.settings)
	}
	return 0
}

// parseCommand dispatches on the first word; anything that is not a known
// subcommand goes to parseArgs (serve), which rejects stray words.
func parseCommand(args []string, stderr io.Writer) (command, int) {
	if len(args) > 0 && args[0] == "hooks" {
		return parseHooks(args[1:], stderr)
	}
	if len(args) > 0 && args[0] == "hook" {
		fs := flag.NewFlagSet("cube-bridge hook", flag.ContinueOnError)
		fs.SetOutput(stderr)
		port := fs.Int("port", 8787, "port of the running bridge")
		if err := fs.Parse(args[1:]); err != nil {
			if errors.Is(err, flag.ErrHelp) {
				return command{}, 0
			}
			return command{}, 2
		}
		if fs.NArg() > 0 || *port < 1 || *port > 65535 {
			fmt.Fprintln(stderr, "usage: cube-bridge hook [--port N]")
			return command{}, 2
		}
		return command{kind: "hook", port: *port}, -1
	}
	path, code := parseArgs(args, stderr)
	if code >= 0 {
		return command{}, code
	}
	return command{kind: "serve", config: path}, -1
}

func main() {
	cmd, code := parseCommand(os.Args[1:], os.Stderr)
	if code >= 0 {
		os.Exit(code)
	}
	if cmd.kind == "hook" {
		runHook(os.Stdin, cmd.port, hookClient())
		return
	}
	if cmd.kind == "hooks" {
		os.Exit(runHooks(cmd, os.Stdout, os.Stderr))
	}
	path := cmd.config
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
	deps := server.Deps{
		Limits: limits.NewReader(),
		Mail:   mail.NewReader(func(s string) { errLog.Println(s) }),
		Source: source,
		ErrLog: errLog,
	}
	// Only when on: a nil Deps.Agents is what turns the feature off.
	if cfg.Agents {
		projects := agents.DefaultProjects() // $CLAUDE_CONFIG_DIR/projects or ~/.claude/projects
		deps.Agents = agents.NewTracker(agents.TrackerDeps{
			Tmux:     agents.NewTmux(),
			Discover: agents.NewDiscovery(projects),
			Projects: projects,
		})
	}
	s := server.New(cfg, deps)
	if err := s.Run(ctx); err != nil {
		errLog.Println(err)
		stop()
		os.Exit(1)
	}
}
