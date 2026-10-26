package main

import (
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
	"strings"
	"time"
)

type client struct {
	base, token  string
	http         *http.Client
	registration registration
}
type game struct {
	ID, Status                string
	PlayerIDs                 []string `json:"playerIds"`
	Version                   int      `json:"version"`
	CurrentRound, CurrentTurn int
}
type registration struct {
	Token, DeviceID, ProfileURL string
	Player                      struct {
		ID, Name, Timezone string
	} `json:"player"`
}

func main() {
	base := flag.String("server", "http://127.0.0.1:18080", "server base URL")
	code := flag.String("pairing-code", "harness", "server pairing code")
	adminUser := flag.String("admin-username", "admin", "admin Basic Auth username")
	adminPassword := flag.String("admin-password", "cartarchy", "admin Basic Auth password")
	flag.Parse()
	c := &client{base: strings.TrimRight(*base, "/"), http: &http.Client{Timeout: 5 * time.Second}}
	check(c.status("GET", "/health", nil, "", 200), "health")
	check(c.status("GET", "/", nil, "", 200), "public dashboard")
	png, status := c.raw("GET", "/pairing.png", nil, "")
	check(status == 200 && bytes.HasPrefix(png, []byte{0x89, 'P', 'N', 'G'}), "pairing QR")
	a := c.register("harness-a-"+fmt.Sprint(time.Now().UnixNano()), "Alice", *code)
	b := c.register("harness-b-"+fmt.Sprint(time.Now().UnixNano()), "Bob", *code)
	check(a.registration.ProfileURL != "" && a.registration.Player.Timezone == "UTC", "UUID profile registration")
	check(a.status("GET", "/v1/profile", nil, a.token, 200), "profile API")
	var g game
	a.json("POST", "/v1/games", map[string]any{"Name": "Harness Campaign", "Mode": "turn-based"}, 201, &g)
	check(g.Status == "waiting", "create waiting game")
	png, status = c.raw("GET", "/games/"+g.ID+"/qr.png", nil, "")
	check(status == 200 && bytes.HasPrefix(png, []byte{0x89, 'P', 'N', 'G'}), "game join QR")
	b.json("POST", "/v1/games/"+g.ID+"/join", map[string]any{}, 200, &g)
	check(g.Status == "active" && len(g.PlayerIDs) == 2, "join and activate")
	a.json("POST", "/v1/games/"+g.ID+"/pause", map[string]any{}, 200, &g)
	check(g.Status == "paused", "pause")
	a.json("POST", "/v1/games/"+g.ID+"/resume", map[string]any{}, 200, &g)
	check(g.Status == "active", "resume")
	check(a.status("POST", "/v1/games/"+g.ID+"/events", map[string]any{"ExpectedVersion": g.Version - 1, "Kind": "turn.completed"}, a.token, 409), "reject stale turn")
	a.json("POST", "/v1/games/"+g.ID+"/events", map[string]any{"ExpectedVersion": g.Version, "Kind": "turn.completed", "Payload": map[string]any{"turn": 1}, "State": map[string]any{"phase": "reinforce"}}, 200, &g)
	check(g.CurrentRound == 1 && g.CurrentTurn == 2, "advance first turn")
	b.json("POST", "/v1/games/"+g.ID+"/events", map[string]any{"ExpectedVersion": g.Version, "Kind": "turn.completed"}, 200, &g)
	check(g.CurrentRound == 2 && g.CurrentTurn == 3, "roll over game round")
	var rounds []map[string]any
	a.json("GET", "/v1/games/"+g.ID+"/rounds", nil, 200, &rounds)
	check(len(rounds) == 2, "persistent round history")
	a.json("POST", "/v1/games/"+g.ID+"/finish", map[string]any{"WinnerID": g.PlayerIDs[0]}, 200, &g)
	check(g.Status == "finished", "finish")
	var aStats, bStats struct{ Wins, Losses int }
	a.json("GET", "/v1/stats", nil, 200, &aStats)
	b.json("GET", "/v1/stats", nil, 200, &bStats)
	check(aStats.Wins == 1 && bStats.Losses == 1, "wins and losses")
	check(c.basicStatus("GET", "/admin", *adminUser, *adminPassword, 200), "Basic Auth admin")
	fmt.Println("PASS: Cartarchy black-box harness completed")
}

func (c *client) register(device, name, code string) *client {
	var out registration
	c.json("POST", "/v1/devices/register", map[string]any{"DeviceID": device, "Name": name, "PairingCode": code}, 201, &out)
	check(out.Token != "" && out.Player.ID != "", "device registration")
	return &client{base: c.base, token: out.Token, http: c.http, registration: out}
}
func (c *client) json(method, path string, body any, want int, out any) {
	data, status := c.raw(method, path, body, c.token)
	if status != want {
		fatalf("%s %s: wanted %d, got %d: %s", method, path, want, status, data)
	}
	if out != nil && json.Unmarshal(data, out) != nil {
		fatalf("%s returned invalid JSON", path)
	}
}
func (c *client) status(method, path string, body any, token string, want int) bool {
	_, got := c.raw(method, path, body, token)
	return got == want
}
func (c *client) raw(method, path string, body any, token string) ([]byte, int) {
	var reader io.Reader
	if body != nil {
		b, _ := json.Marshal(body)
		reader = bytes.NewReader(b)
	}
	req, err := http.NewRequest(method, c.base+path, reader)
	if err != nil {
		fatalf("request: %v", err)
	}
	if body != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	if token != "" {
		req.Header.Set("Authorization", "Bearer "+token)
	}
	resp, err := c.http.Do(req)
	if err != nil {
		fatalf("%s %s: %v", method, path, err)
	}
	defer resp.Body.Close()
	data, _ := io.ReadAll(resp.Body)
	return data, resp.StatusCode
}
func (c *client) basicStatus(method, path, username, password string, want int) bool {
	req, err := http.NewRequest(method, c.base+path, nil)
	if err != nil {
		fatalf("request: %v", err)
	}
	req.SetBasicAuth(username, password)
	resp, err := c.http.Do(req)
	if err != nil {
		fatalf("%s %s: %v", method, path, err)
	}
	defer resp.Body.Close()
	return resp.StatusCode == want
}
func check(ok bool, label string) {
	if !ok {
		fatalf("FAIL: %s", label)
	}
	fmt.Println("PASS:", label)
}
func fatalf(format string, args ...any) { fmt.Fprintf(os.Stderr, format+"\n", args...); os.Exit(1) }
