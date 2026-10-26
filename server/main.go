package main

import (
	"context"
	"crypto/subtle"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"log"
	"net/http"
	"net/url"
	"os"
	"os/signal"
	"strings"
	"sync"
	"syscall"
	"time"

	qrcode "github.com/skip2/go-qrcode"
)

const (
	defaultAdminUsername = "admin"
	defaultAdminPassword = "cartarchy"
	// Below the 60s idle timeouts of URLSession and typical reverse proxies.
	defaultStreamKeepalive = 25 * time.Second
)

type Server struct {
	store                        *Store
	publicURL, pairingCode       string
	adminUsername, adminPassword string
	streamKeepalive              time.Duration
	subsMu                       sync.Mutex
	subs                         map[chan []byte]struct{}
}

func main() {
	listen := flag.String("listen", env("CARTARCHY_LISTEN", ":8080"), "listen address")
	publicURL := flag.String("public-url", env("CARTARCHY_PUBLIC_URL", ""), "fixed URL reachable by phones (default: derive from each request)")
	data := flag.String("data", env("CARTARCHY_DATA", "./data/cartarchy.sqlite"), "SQLite database file")
	pairingCode := flag.String("pairing-code", env("CARTARCHY_PAIRING_CODE", ""), "fixed pairing code (default: random per start)")
	adminUsername := flag.String("admin-username", env("CARTARCHY_ADMIN_USERNAME", defaultAdminUsername), "admin Basic Auth username")
	adminPassword := flag.String("admin-password", env("CARTARCHY_ADMIN_PASSWORD", defaultAdminPassword), "admin Basic Auth password")
	flag.Parse()
	store, err := OpenStore(*data)
	if err != nil {
		log.Fatal(err)
	}
	defer store.Close()
	if *pairingCode == "" {
		*pairingCode = randomID(4)
	}
	s := &Server{
		store: store, publicURL: strings.TrimRight(*publicURL, "/"), pairingCode: *pairingCode,
		adminUsername: *adminUsername, adminPassword: *adminPassword, subs: map[chan []byte]struct{}{},
	}
	httpServer := &http.Server{Addr: *listen, Handler: s.routes(), ReadHeaderTimeout: 5 * time.Second, IdleTimeout: 60 * time.Second}
	go func() {
		if s.publicURL == "" {
			log.Printf("Cartarchy server listening on %s (public URL derived from request, pairing code %s, SQLite %s)", *listen, s.pairingCode, *data)
		} else {
			log.Printf("Cartarchy server: %s (listening on %s, pairing code %s, SQLite %s)", s.publicURL, *listen, s.pairingCode, *data)
		}
		if err := httpServer.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatal(err)
		}
	}()
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGINT, syscall.SIGTERM)
	<-stop
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	_ = httpServer.Shutdown(ctx)
}

func env(key, fallback string) string {
	if value := os.Getenv(key); value != "" {
		return value
	}
	return fallback
}

func (s *Server) routes() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /", s.home)
	mux.HandleFunc("POST /games", s.createGamePage)
	mux.HandleFunc("GET /games/{id}", s.gamePage)
	mux.HandleFunc("GET /games/{id}/qr.png", s.gameQR)
	mux.HandleFunc("GET /profiles/{id}", s.profilePage)
	mux.HandleFunc("POST /profiles/{id}", s.updateProfilePage)
	mux.HandleFunc("GET /profiles/{id}/qr.png", s.profileQR)
	mux.HandleFunc("GET /pairing.png", s.pairingQR)
	mux.HandleFunc("GET /health", func(w http.ResponseWriter, _ *http.Request) {
		writeJSON(w, 200, map[string]string{"status": "ok", "storage": "sqlite"})
	})

	mux.HandleFunc("POST /v1/devices/register", s.register)
	mux.HandleFunc("GET /v1/stats", s.auth(s.stats))
	mux.HandleFunc("GET /v1/profile", s.auth(s.profileAPI))
	mux.HandleFunc("PUT /v1/profile", s.auth(s.updateProfileAPI))
	mux.HandleFunc("GET /v1/profile/qr.png", s.auth(s.profileQRAPI))
	mux.HandleFunc("GET /v1/games", s.auth(s.listGames))
	mux.HandleFunc("POST /v1/games", s.auth(s.createGame))
	mux.HandleFunc("GET /v1/games/{id}", s.auth(s.getGame))
	mux.HandleFunc("POST /v1/games/{id}/join", s.auth(s.joinGame))
	mux.HandleFunc("POST /v1/games/{id}/start", s.auth(s.startGame))
	mux.HandleFunc("POST /v1/games/{id}/pause", s.auth(s.pauseGame))
	mux.HandleFunc("POST /v1/games/{id}/resume", s.auth(s.resumeGame))
	mux.HandleFunc("POST /v1/games/{id}/resign", s.auth(s.resignGame))
	mux.HandleFunc("POST /v1/games/{id}/events", s.auth(s.addEvent))
	mux.HandleFunc("POST /v1/games/{id}/finish", s.auth(s.finishGame))
	mux.HandleFunc("GET /v1/games/{id}/rounds", s.auth(s.gameRounds))
	mux.HandleFunc("GET /v1/events", s.auth(s.eventStream))

	mux.HandleFunc("GET /admin", s.admin(s.adminPage))
	mux.HandleFunc("POST /admin/games/{id}/{action}", s.admin(s.adminGameAction))
	mux.HandleFunc("POST /admin/players/{id}/{action}", s.admin(s.adminPlayerAction))
	mux.HandleFunc("POST /admin/devices/{id}/{action}", s.admin(s.adminDeviceAction))
	return securityHeaders(mux)
}

func (s *Server) baseURL(r *http.Request) string {
	if s.publicURL != "" {
		return s.publicURL
	}
	scheme := "http"
	if r.TLS != nil {
		scheme = "https"
	}
	return (&url.URL{Scheme: scheme, Host: r.Host}).String()
}

func (s *Server) pairingURI(r *http.Request) string {
	return "cartarchy://configure?server=" + url.QueryEscape(s.baseURL(r)) + "&code=" + url.QueryEscape(s.pairingCode)
}

func (s *Server) pairingQR(w http.ResponseWriter, r *http.Request) { s.writeQR(w, s.pairingURI(r)) }

func (s *Server) gameJoinURI(r *http.Request, g Game) string {
	return "cartarchy://join?server=" + url.QueryEscape(s.baseURL(r)) + "&game=" + url.QueryEscape(g.ID) + "&code=" + url.QueryEscape(g.JoinCode) + "&pairing=" + url.QueryEscape(s.pairingCode)
}

func (s *Server) profileURL(r *http.Request, p Player) string {
	return fmt.Sprintf("%s/profiles/%s?key=%s", s.baseURL(r), url.PathEscape(p.ID), url.QueryEscape(p.ProfileSecret))
}

func (s *Server) writeQR(w http.ResponseWriter, value string) {
	png, err := qrcode.Encode(value, qrcode.Medium, 512)
	if err != nil {
		http.Error(w, "QR failure", http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "image/png")
	w.Header().Set("Cache-Control", "no-store")
	_, _ = w.Write(png)
}

func (s *Server) register(w http.ResponseWriter, r *http.Request) {
	var input struct{ DeviceID, Name, PairingCode string }
	if !decode(w, r, &input) {
		return
	}
	if input.PairingCode != s.pairingCode || strings.TrimSpace(input.DeviceID) == "" {
		problem(w, http.StatusForbidden, "invalid pairing code or device ID")
		return
	}
	player, device, err := s.store.RegisterDevice(strings.TrimSpace(input.DeviceID))
	if err != nil {
		s.storeProblem(w, err)
		return
	}
	writeJSON(w, http.StatusCreated, map[string]any{
		"token": device.Token, "deviceId": device.ID, "player": publicPlayer(player),
		"profileURL": s.profileURL(r, *player), "profileQRCodeURL": s.baseURL(r) + "/v1/profile/qr.png",
	})
}

func (s *Server) auth(next func(http.ResponseWriter, *http.Request, *Player)) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		token := strings.TrimPrefix(r.Header.Get("Authorization"), "Bearer ")
		player := s.store.PlayerByToken(token)
		if player == nil {
			problem(w, http.StatusUnauthorized, "invalid bearer token")
			return
		}
		next(w, r, player)
	}
}

func (s *Server) admin(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		username, password, ok := r.BasicAuth()
		usernameOK := subtle.ConstantTimeCompare([]byte(username), []byte(s.adminUsername)) == 1
		passwordOK := subtle.ConstantTimeCompare([]byte(password), []byte(s.adminPassword)) == 1
		if !ok || !usernameOK || !passwordOK {
			w.Header().Set("WWW-Authenticate", `Basic realm="Cartarchy Admin", charset="UTF-8"`)
			http.Error(w, "authentication required", http.StatusUnauthorized)
			return
		}
		next(w, r)
	}
}

func (s *Server) stats(w http.ResponseWriter, _ *http.Request, player *Player) {
	writeJSON(w, http.StatusOK, publicPlayer(player))
}

func publicPlayer(player *Player) map[string]any {
	return map[string]any{
		"id": player.ID, "name": player.Name, "timezone": player.Timezone,
		"wins": player.Wins, "losses": player.Losses, "createdAt": player.CreatedAt,
	}
}

func (s *Server) profileAPI(w http.ResponseWriter, r *http.Request, player *Player) {
	writeJSON(w, http.StatusOK, map[string]any{
		"player": publicPlayer(player), "profileURL": s.profileURL(r, *player),
		"profileQRCodeURL": s.baseURL(r) + "/v1/profile/qr.png",
	})
}

func (s *Server) updateProfileAPI(w http.ResponseWriter, r *http.Request, player *Player) {
	var input struct{ Name, Timezone string }
	if !decode(w, r, &input) {
		return
	}
	updated, err := s.store.UpdateProfile(player.ID, input.Name, input.Timezone)
	if err != nil {
		problem(w, http.StatusBadRequest, err.Error())
		return
	}
	writeJSON(w, http.StatusOK, publicPlayer(&updated))
}

func (s *Server) profileQRAPI(w http.ResponseWriter, r *http.Request, player *Player) {
	s.writeQR(w, s.profileURL(r, *player))
}

func (s *Server) listGames(w http.ResponseWriter, _ *http.Request, player *Player) {
	games, err := s.store.ListGames(player.ID)
	if err != nil {
		s.storeProblem(w, err)
		return
	}
	writeJSON(w, http.StatusOK, games)
}

func (s *Server) getGame(w http.ResponseWriter, r *http.Request, player *Player) {
	game, err := s.store.GetGameForPlayer(r.PathValue("id"), player.ID)
	if err != nil {
		s.storeProblem(w, err)
		return
	}
	writeJSON(w, http.StatusOK, game)
}

func (s *Server) createGame(w http.ResponseWriter, r *http.Request, player *Player) {
	var input struct {
		Name       string
		Mode       string
		MaxPlayers int
	}
	if !decode(w, r, &input) {
		return
	}
	game, err := s.store.CreateGame(player.ID, input.Name, input.Mode, input.MaxPlayers)
	if err != nil {
		problem(w, http.StatusBadRequest, err.Error())
		return
	}
	s.publish("game.created", &game)
	writeJSON(w, http.StatusCreated, game)
}

func (s *Server) joinGame(w http.ResponseWriter, r *http.Request, player *Player) {
	game, err := s.store.JoinGame(r.PathValue("id"), player.ID)
	if err != nil {
		s.storeProblem(w, err)
		return
	}
	s.publish("game.updated", &game)
	writeJSON(w, http.StatusOK, game)
}

func (s *Server) startGame(w http.ResponseWriter, r *http.Request, player *Player) {
	game, err := s.store.StartGame(r.PathValue("id"), player.ID)
	if err != nil {
		s.storeProblem(w, err)
		return
	}
	s.publish("game.updated", &game)
	writeJSON(w, http.StatusOK, game)
}

func (s *Server) pauseGame(w http.ResponseWriter, r *http.Request, player *Player) {
	s.setGameStatus(w, r, player, Paused)
}

func (s *Server) resumeGame(w http.ResponseWriter, r *http.Request, player *Player) {
	s.setGameStatus(w, r, player, Active)
}

func (s *Server) resignGame(w http.ResponseWriter, r *http.Request, player *Player) {
	game, err := s.store.ResignGame(r.PathValue("id"), player.ID)
	if err != nil {
		s.storeProblem(w, err)
		return
	}
	s.publish("game.resigned", &game)
	writeJSON(w, http.StatusOK, game)
}

func (s *Server) setGameStatus(w http.ResponseWriter, r *http.Request, player *Player, status GameStatus) {
	game, err := s.store.SetGameStatus(r.PathValue("id"), player.ID, status)
	if err != nil {
		s.storeProblem(w, err)
		return
	}
	s.publish("game.updated", &game)
	writeJSON(w, http.StatusOK, game)
}

func (s *Server) addEvent(w http.ResponseWriter, r *http.Request, player *Player) {
	var input struct {
		ExpectedVersion int
		Kind            string
		Payload         map[string]any
		State           map[string]any
	}
	if !decode(w, r, &input) {
		return
	}
	game, err := s.store.AddGameEvent(r.PathValue("id"), player.ID, input.ExpectedVersion, input.Kind, input.Payload, input.State)
	if err != nil {
		s.storeProblem(w, err)
		return
	}
	s.publish("game.updated", &game)
	writeJSON(w, http.StatusOK, game)
}

func (s *Server) finishGame(w http.ResponseWriter, r *http.Request, player *Player) {
	var input struct{ WinnerID string }
	if !decode(w, r, &input) {
		return
	}
	game, err := s.store.FinishGame(r.PathValue("id"), player.ID, input.WinnerID)
	if err != nil {
		s.storeProblem(w, err)
		return
	}
	s.publish("game.updated", &game)
	writeJSON(w, http.StatusOK, game)
}

func (s *Server) gameRounds(w http.ResponseWriter, r *http.Request, player *Player) {
	game, err := s.store.GetGameForPlayer(r.PathValue("id"), player.ID)
	if err != nil {
		s.storeProblem(w, err)
		return
	}
	rounds, err := s.store.ListRounds(game.ID)
	if err != nil {
		s.storeProblem(w, err)
		return
	}
	writeJSON(w, http.StatusOK, rounds)
}

func (s *Server) publish(kind string, game *Game) {
	data, _ := json.Marshal(map[string]any{"type": kind, "game": game})
	s.subsMu.Lock()
	defer s.subsMu.Unlock()
	for channel := range s.subs {
		select {
		case channel <- data:
		default:
		}
	}
}

func (s *Server) eventStream(w http.ResponseWriter, r *http.Request, _ *Player) {
	flusher, ok := w.(http.Flusher)
	if !ok {
		problem(w, http.StatusInternalServerError, "stream unsupported")
		return
	}
	channel := make(chan []byte, 16)
	s.subsMu.Lock()
	s.subs[channel] = struct{}{}
	s.subsMu.Unlock()
	defer func() { s.subsMu.Lock(); delete(s.subs, channel); s.subsMu.Unlock() }()
	keepalive := s.streamKeepalive
	if keepalive <= 0 {
		keepalive = defaultStreamKeepalive
	}
	ticker := time.NewTicker(keepalive)
	defer ticker.Stop()
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("X-Accel-Buffering", "no")
	fmt.Fprint(w, "event: ready\ndata: {}\n\n")
	flusher.Flush()
	for {
		select {
		case data := <-channel:
			fmt.Fprintf(w, "event: game\ndata: %s\n\n", data)
			flusher.Flush()
		case <-ticker.C:
			fmt.Fprint(w, ": keepalive\n\n")
			flusher.Flush()
		case <-r.Context().Done():
			return
		}
	}
}

func (s *Server) storeProblem(w http.ResponseWriter, err error) {
	switch {
	case errors.Is(err, ErrNotFound):
		problem(w, http.StatusNotFound, err.Error())
	case errors.Is(err, ErrForbidden):
		problem(w, http.StatusForbidden, err.Error())
	case errors.Is(err, ErrConflict):
		problem(w, http.StatusConflict, err.Error())
	default:
		log.Printf("store error: %v", err)
		problem(w, http.StatusInternalServerError, "internal server error")
	}
}

func contains(values []string, wanted string) bool {
	for _, value := range values {
		if value == wanted {
			return true
		}
	}
	return false
}

func indexOf(values []string, wanted string) int {
	for index, value := range values {
		if value == wanted {
			return index
		}
	}
	return 0
}

func decode(w http.ResponseWriter, r *http.Request, value any) bool {
	r.Body = http.MaxBytesReader(w, r.Body, 1<<20)
	decoder := json.NewDecoder(r.Body)
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(value); err != nil {
		problem(w, http.StatusBadRequest, "invalid JSON")
		return false
	}
	return true
}

func writeJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(value)
}

func problem(w http.ResponseWriter, status int, message string) {
	writeJSON(w, status, map[string]any{"error": message, "status": status})
}

func securityHeaders(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("X-Content-Type-Options", "nosniff")
		w.Header().Set("X-Frame-Options", "DENY")
		w.Header().Set("Referrer-Policy", "no-referrer")
		w.Header().Set("Content-Security-Policy", "default-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; form-action 'self'; base-uri 'none'")
		next.ServeHTTP(w, r)
	})
}
