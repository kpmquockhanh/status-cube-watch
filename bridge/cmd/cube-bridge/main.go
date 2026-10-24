// Command cube-bridge serves the Claude status payload to the cube.
package main

import (
	"flag"
	"fmt"
	"log"
	"os"

	"claude-status-cube/bridge/internal/config"
)

func main() {
	path := flag.String("config", "config.json", "path to config.json")
	flag.Parse()
	cfg := config.Load(os.LookupEnv, config.ReadFile(*path), func(s string) { log.Println(s) })
	fmt.Printf("%+v\n", cfg)
}
