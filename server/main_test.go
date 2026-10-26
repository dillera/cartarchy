package main

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"

	qrcode "github.com/skip2/go-qrcode"
)

func request(t *testing.T, h http.Handler, method, path, token string, body any) *httptest.ResponseRecorder {
	t.Helper()
	b, _ := json.Marshal(body)
	r := httptest.NewRequest(method, path, bytes.NewReader(b))
	if token != "" {
		r.Header.Set("Authorization", "Bearer "+token)
	}
	w := httptest.NewRecorder()
	h.ServeHTTP(w, r)
	return w
}
func registerTest(t *testing.T, h http.Handler, id string) string {
	w := request(t, h, "POST", "/v1/devices/register", "", map[string]any{"DeviceID": id, "Name": id, "PairingCode": "pair"})
	if w.Code != 201 {
		t.Fatalf("register: %d %s", w.Code, w.Body.String())
	}
	var out struct{ Token string }
	_ = json.Unmarshal(w.Body.Bytes(), &out)
	return out.Token
}

type testRegistration struct {
	Token, DeviceID, ProfileURL string
	Player                      Player
}

func registrationTest(t *testing.T, h http.Handler, id string) testRegistration {
	t.Helper()
	w := request(t, h, "POST", "/v1/devices/register", "", map[string]any{"DeviceID": id, "Name": "Ignored Name", "PairingCode": "pair"})
	if w.Code != http.StatusCreated {
		t.Fatalf("register: %d %s", w.Code, w.Body.String())
	}
	var out testRegistration
	if err := json.Unmarshal(w.Body.Bytes(), &out); err != nil {
		t.Fatal(err)
	}
	return out
}

func newTestServer(t *testing.T) (*Server, http.Handler) {
	t.Helper()
	store, err := OpenStore(filepath.Join(t.TempDir(), "db.sqlite"))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = store.Close() })
	s := &Server{store: store, publicURL: "http://test", pairingCode: "pair", adminUsername: "admin", adminPassword: "cartarchy", subs: map[chan []byte]struct{}{}}
	return s, s.routes()
}

func TestAuthenticationAndPairingBoundary(t *testing.T) {
	_, h := newTestServer(t)
	t.Run("protected endpoint rejects missing token", func(t *testing.T) {
		w := request(t, h, "GET", "/v1/games", "", nil)
		if w.Code != http.StatusUnauthorized {
			t.Fatalf("got %d: %s", w.Code, w.Body.String())
		}
	})
	t.Run("registration rejects incorrect pairing code", func(t *testing.T) {
		w := request(t, h, "POST", "/v1/devices/register", "", map[string]any{"DeviceID": "phone", "Name": "Alice", "PairingCode": "wrong"})
		if w.Code != http.StatusForbidden {
			t.Fatalf("got %d: %s", w.Code, w.Body.String())
		}
	})
	t.Run("registration rejects missing device identifier", func(t *testing.T) {
		w := request(t, h, "POST", "/v1/devices/register", "", map[string]any{"Name": "Alice", "PairingCode": "pair"})
		if w.Code != http.StatusForbidden {
			t.Fatalf("got %d: %s", w.Code, w.Body.String())
		}
	})
}

func TestGameCreationValidation(t *testing.T) {
	_, h := newTestServer(t)
	token := registerTest(t, h, "alice")
	for _, mode := range []string{"", "postal", "REALTIME"} {
		t.Run("rejects mode "+mode, func(t *testing.T) {
			w := request(t, h, "POST", "/v1/games", token, map[string]any{"Name": "Bad game", "Mode": mode})
			if w.Code != http.StatusBadRequest {
				t.Fatalf("got %d: %s", w.Code, w.Body.String())
			}
		})
	}
}

func TestPublicGamePageListsJoinedPlayers(t *testing.T) {
	_, h := newTestServer(t)
	host := registrationTest(t, h, "lobby-host")
	guest := registrationTest(t, h, "lobby-guest")
	w := request(t, h, http.MethodPost, "/v1/games", host.Token, map[string]any{
		"Name": "Roster Test", "Mode": "turn-based", "MaxPlayers": 3,
	})
	if w.Code != http.StatusCreated {
		t.Fatalf("create game: %d %s", w.Code, w.Body.String())
	}
	var game Game
	if err := json.Unmarshal(w.Body.Bytes(), &game); err != nil {
		t.Fatal(err)
	}
	w = request(t, h, http.MethodPost, "/v1/games/"+game.ID+"/join", guest.Token, map[string]any{})
	if w.Code != http.StatusOK {
		t.Fatalf("join game: %d %s", w.Code, w.Body.String())
	}

	w = request(t, h, http.MethodGet, "/games/"+game.ID, "", nil)
	if w.Code != http.StatusOK {
		t.Fatalf("game page: %d %s", w.Code, w.Body.String())
	}
	page := w.Body.String()
	for _, name := range []string{host.Player.Name, guest.Player.Name} {
		if !strings.Contains(page, name) {
			t.Errorf("game page does not list joined player %q: %s", name, page)
		}
	}
}

func createJoinedGame(t *testing.T, h http.Handler) (Game, string, string) {
	t.Helper()
	a, b := registerTest(t, h, randomID(4)), registerTest(t, h, randomID(4))
	w := request(t, h, "POST", "/v1/games", a, map[string]any{"Name": "Campaign", "Mode": "turn-based"})
	if w.Code != http.StatusCreated {
		t.Fatal(w.Body.String())
	}
	var g Game
	if err := json.Unmarshal(w.Body.Bytes(), &g); err != nil {
		t.Fatal(err)
	}
	w = request(t, h, "POST", "/v1/games/"+g.ID+"/join", b, map[string]any{})
	if w.Code != http.StatusOK {
		t.Fatal(w.Body.String())
	}
	if err := json.Unmarshal(w.Body.Bytes(), &g); err != nil {
		t.Fatal(err)
	}
	return g, a, b
}

func TestFinishedGameCannotBeFinishedTwice(t *testing.T) {
	_, h := newTestServer(t)
	g, host, _ := createJoinedGame(t, h)
	w := request(t, h, "POST", "/v1/games/"+g.ID+"/finish", host, map[string]any{"WinnerID": g.PlayerIDs[0]})
	if w.Code != http.StatusOK {
		t.Fatal(w.Body.String())
	}
	w = request(t, h, "POST", "/v1/games/"+g.ID+"/finish", host, map[string]any{"WinnerID": g.PlayerIDs[0]})
	if w.Code != http.StatusConflict {
		t.Fatalf("duplicate finish returned %d: %s", w.Code, w.Body.String())
	}
	w = request(t, h, "GET", "/v1/stats", host, nil)
	if !bytes.Contains(w.Body.Bytes(), []byte(`"wins":1`)) {
		t.Fatalf("win counted more than once: %s", w.Body.String())
	}
}

func TestCurrentPlayerCanFinishTurnBasedGameAsWinner(t *testing.T) {
	_, h := newTestServer(t)
	g, host, guestToken := createJoinedGame(t, h)
	guestID := g.PlayerIDs[1]
	w := request(t, h, "POST", "/v1/games/"+g.ID+"/events", host, map[string]any{
		"ExpectedVersion": g.Version,
		"Kind":            "state.updated",
		"Payload":         map[string]any{"currentPlayerId": guestID},
	})
	if w.Code != http.StatusOK {
		t.Fatal(w.Body.String())
	}
	w = request(t, h, "POST", "/v1/games/"+g.ID+"/finish", guestToken, map[string]any{"WinnerID": guestID})
	if w.Code != http.StatusOK {
		t.Fatalf("current player could not finish as winner: %d %s", w.Code, w.Body.String())
	}
	var finished Game
	if err := json.Unmarshal(w.Body.Bytes(), &finished); err != nil {
		t.Fatal(err)
	}
	if finished.Status != Finished || finished.WinnerID != guestID {
		t.Fatalf("unexpected finished game: %+v", finished)
	}
}

func TestGameLifecycleGuards(t *testing.T) {
	_, h := newTestServer(t)
	host := registerTest(t, h, "host")
	outsider := registerTest(t, h, "outsider")
	w := request(t, h, "POST", "/v1/games", host, map[string]any{"Name": "Guarded", "Mode": "realtime"})
	var waiting Game
	_ = json.Unmarshal(w.Body.Bytes(), &waiting)
	if got := request(t, h, "POST", "/v1/games/"+waiting.ID+"/pause", host, map[string]any{}).Code; got != http.StatusConflict {
		t.Fatalf("pause waiting game: %d", got)
	}
	if got := request(t, h, "GET", "/v1/games/"+waiting.ID, outsider, nil).Code; got != http.StatusNotFound {
		t.Fatalf("outsider read: %d", got)
	}

	w = request(t, h, "POST", "/v1/games/"+waiting.ID+"/join", outsider, map[string]any{})
	var active Game
	_ = json.Unmarshal(w.Body.Bytes(), &active)
	if got := request(t, h, "POST", "/v1/games/"+active.ID+"/events", host, map[string]any{"ExpectedVersion": active.Version - 1, "Kind": "turn"}).Code; got != http.StatusConflict {
		t.Fatalf("stale event: %d", got)
	}
	w = request(t, h, "POST", "/v1/games/"+active.ID+"/pause", host, map[string]any{})
	_ = json.Unmarshal(w.Body.Bytes(), &active)
	if got := request(t, h, "POST", "/v1/games/"+active.ID+"/events", host, map[string]any{"ExpectedVersion": active.Version, "Kind": "turn"}).Code; got != http.StatusConflict {
		t.Fatalf("event while paused: %d", got)
	}
	w = request(t, h, "POST", "/v1/games/"+active.ID+"/resume", host, map[string]any{})
	_ = json.Unmarshal(w.Body.Bytes(), &active)
	if got := request(t, h, "POST", "/v1/games/"+active.ID+"/finish", outsider, map[string]any{"WinnerID": active.PlayerIDs[0]}).Code; got != http.StatusConflict {
		t.Fatalf("non-host finish: %d", got)
	}
	if got := request(t, h, "POST", "/v1/games/"+active.ID+"/finish", host, map[string]any{"WinnerID": active.PlayerIDs[0]}).Code; got != http.StatusOK {
		t.Fatalf("host finish: %d", got)
	}
	late := registerTest(t, h, "late")
	if got := request(t, h, "POST", "/v1/games/"+active.ID+"/join", late, map[string]any{}).Code; got != http.StatusConflict {
		t.Fatalf("join finished game: %d", got)
	}
}

func TestOperationalEndpointsAndQRCode(t *testing.T) {
	_, h := newTestServer(t)
	w := request(t, h, "GET", "/health", "", nil)
	if w.Code != http.StatusOK || !bytes.Contains(w.Body.Bytes(), []byte(`"status":"ok"`)) {
		t.Fatalf("health: %d %s", w.Code, w.Body.String())
	}
	if w.Header().Get("X-Content-Type-Options") != "nosniff" {
		t.Fatal("missing security headers")
	}
	w = request(t, h, "GET", "/", "", nil)
	if !strings.Contains(w.Body.String(), "Cartarchy SERVER") || !strings.Contains(w.Body.String(), "pair") {
		t.Fatal("pairing page missing configuration")
	}
	w = request(t, h, "GET", "/pairing.png", "", nil)
	if w.Code != http.StatusOK || w.Header().Get("Content-Type") != "image/png" || !bytes.HasPrefix(w.Body.Bytes(), []byte{0x89, 'P', 'N', 'G'}) {
		t.Fatal("pairing endpoint did not return a PNG")
	}
}

func TestPairingQRCodeUsesRequestAddressWhenPublicURLIsUnset(t *testing.T) {
	s, h := newTestServer(t)
	s.publicURL = ""
	r := httptest.NewRequest(http.MethodGet, "http://192.168.1.236:18080/pairing.png", nil)
	w := httptest.NewRecorder()
	h.ServeHTTP(w, r)

	want, err := qrcode.Encode(
		"cartarchy://configure?server=http%3A%2F%2F192.168.1.236%3A18080&code=pair",
		qrcode.Medium,
		512,
	)
	if err != nil {
		t.Fatal(err)
	}
	if w.Code != http.StatusOK || !bytes.Equal(w.Body.Bytes(), want) {
		t.Fatal("pairing QR did not encode the address used for the request")
	}
}

func TestRegistrationRotatesTokenAndPersistsGamesAcrossRestart(t *testing.T) {
	path := filepath.Join(t.TempDir(), "db.json")
	store, _ := OpenStore(path)
	s := &Server{store: store, publicURL: "http://test", pairingCode: "pair", subs: map[chan []byte]struct{}{}}
	h := s.routes()
	oldToken := registerTest(t, h, "same-phone")
	newToken := registerTest(t, h, "same-phone")
	if oldToken == newToken {
		t.Fatal("re-registration did not rotate credential")
	}
	if got := request(t, h, "GET", "/v1/games", oldToken, nil).Code; got != http.StatusUnauthorized {
		t.Fatalf("old token still accepted: %d", got)
	}
	w := request(t, h, "POST", "/v1/games", newToken, map[string]any{"Name": "Durable", "Mode": "turn-based"})
	if w.Code != http.StatusCreated {
		t.Fatal(w.Body.String())
	}
	reopened, err := OpenStore(path)
	if err != nil {
		t.Fatal(err)
	}
	restarted := (&Server{store: reopened, publicURL: "http://test", pairingCode: "pair", subs: map[chan []byte]struct{}{}}).routes()
	w = request(t, restarted, "GET", "/v1/games", newToken, nil)
	if w.Code != http.StatusOK || !bytes.Contains(w.Body.Bytes(), []byte("Durable")) {
		t.Fatalf("game missing after restart: %d %s", w.Code, w.Body.String())
	}
}

func TestRealtimeStreamPublishesGameUpdates(t *testing.T) {
	_, handler := newTestServer(t)
	httpServer := httptest.NewServer(handler)
	defer httpServer.Close()
	token := registerTest(t, handler, "stream-player")
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	req, _ := http.NewRequestWithContext(ctx, "GET", httpServer.URL+"/v1/events", nil)
	req.Header.Set("Authorization", "Bearer "+token)
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	reader := bufio.NewReader(resp.Body)
	ready := readSSEEvent(t, reader)
	if !strings.Contains(ready, "event: ready") {
		t.Fatalf("missing ready event: %q", ready)
	}
	w := request(t, handler, "POST", "/v1/games", token, map[string]any{"Name": "Live", "Mode": "realtime"})
	if w.Code != http.StatusCreated {
		t.Fatal(w.Body.String())
	}
	update := readSSEEvent(t, reader)
	if !strings.Contains(update, "event: game") || !strings.Contains(update, "game.created") || !strings.Contains(update, "Live") {
		t.Fatalf("bad update: %q", update)
	}
}

func TestIdleStreamSendsKeepaliveComments(t *testing.T) {
	s, handler := newTestServer(t)
	s.streamKeepalive = 20 * time.Millisecond
	httpServer := httptest.NewServer(handler)
	defer httpServer.Close()
	token := registerTest(t, handler, "idle-player")
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	req, _ := http.NewRequestWithContext(ctx, "GET", httpServer.URL+"/v1/events", nil)
	req.Header.Set("Authorization", "Bearer "+token)
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.Header.Get("X-Accel-Buffering") != "no" {
		t.Fatal("stream does not disable proxy buffering")
	}
	reader := bufio.NewReader(resp.Body)
	readSSEEvent(t, reader)
	if keepalive := readSSEEvent(t, reader); keepalive != ": keepalive\n\n" {
		t.Fatalf("expected keepalive comment, got %q", keepalive)
	}
}

func TestResigningTwoPlayerGameAwardsOpponentAndBroadcasts(t *testing.T) {
	_, handler := newTestServer(t)
	httpServer := httptest.NewServer(handler)
	defer httpServer.Close()
	game, host, guest := createJoinedGame(t, handler)

	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	req, _ := http.NewRequestWithContext(ctx, http.MethodGet, httpServer.URL+"/v1/events", nil)
	req.Header.Set("Authorization", "Bearer "+host)
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	reader := bufio.NewReader(resp.Body)
	_ = readSSEEvent(t, reader)

	w := request(t, handler, http.MethodPost, "/v1/games/"+game.ID+"/resign", guest, map[string]any{})
	if w.Code != http.StatusOK {
		t.Fatalf("resign: %d %s", w.Code, w.Body.String())
	}
	var resigned Game
	if err = json.Unmarshal(w.Body.Bytes(), &resigned); err != nil {
		t.Fatal(err)
	}
	if resigned.Status != Finished || resigned.WinnerID != resigned.PlayerIDs[0] || len(resigned.PlayerIDs) != 1 {
		t.Fatalf("bad resignation result: %#v", resigned)
	}
	update := readSSEEvent(t, reader)
	if !strings.Contains(update, "game.resigned") || !strings.Contains(update, game.ID) {
		t.Fatalf("opponent was not informed: %q", update)
	}
	if got := request(t, handler, http.MethodGet, "/v1/games", guest, nil).Body.String(); strings.Contains(got, game.ID) {
		t.Fatalf("resigned game remains in player's list: %s", got)
	}
}

func TestCurrentPlayerResignationKeepsMultiplayerGamePlayable(t *testing.T) {
	_, handler := newTestServer(t)
	host := registerTest(t, handler, "three-host")
	second := registerTest(t, handler, "three-second")
	third := registerTest(t, handler, "three-third")
	w := request(t, handler, http.MethodPost, "/v1/games", host, map[string]any{"Name": "Three", "Mode": "turn-based", "MaxPlayers": 3})
	var game Game
	_ = json.Unmarshal(w.Body.Bytes(), &game)
	for _, token := range []string{second, third} {
		w = request(t, handler, http.MethodPost, "/v1/games/"+game.ID+"/join", token, map[string]any{})
		_ = json.Unmarshal(w.Body.Bytes(), &game)
	}
	w = request(t, handler, http.MethodPost, "/v1/games/"+game.ID+"/resign", host, map[string]any{})
	if w.Code != http.StatusOK {
		t.Fatalf("resign: %d %s", w.Code, w.Body.String())
	}
	var continued Game
	_ = json.Unmarshal(w.Body.Bytes(), &continued)
	if continued.Status != Active || len(continued.PlayerIDs) != 2 || continued.CurrentPlayerID != continued.PlayerIDs[0] || continued.HostID != continued.PlayerIDs[0] {
		t.Fatalf("game did not continue: %#v", continued)
	}
	w = request(t, handler, http.MethodPost, "/v1/games/"+game.ID+"/events", second, map[string]any{
		"ExpectedVersion": continued.Version, "Kind": "state.updated", "State": map[string]any{"phase": "CLAIM TERRITORIES"},
	})
	if w.Code != http.StatusOK {
		t.Fatalf("successor could not play: %d %s", w.Code, w.Body.String())
	}
}

func TestGameResponsesIncludeSafePlayerNames(t *testing.T) {
	_, handler := newTestServer(t)
	game, host, _ := createJoinedGame(t, handler)
	w := request(t, handler, http.MethodGet, "/v1/games/"+game.ID, host, nil)
	if w.Code != http.StatusOK {
		t.Fatalf("get game: %d %s", w.Code, w.Body.String())
	}
	var response struct {
		Players []struct {
			ID            string `json:"id"`
			Name          string `json:"name"`
			ProfileSecret string `json:"profileSecret"`
		} `json:"players"`
	}
	if err := json.Unmarshal(w.Body.Bytes(), &response); err != nil {
		t.Fatal(err)
	}
	if len(response.Players) != 2 || response.Players[0].ID == "" || response.Players[0].Name == "" {
		t.Fatalf("missing game player identity: %s", w.Body.String())
	}
	if response.Players[0].ProfileSecret != "" {
		t.Fatalf("game response exposed profile secret: %s", w.Body.String())
	}
}

func TestTurnBasedStateUpdatesRequireCurrentPlayerAndAdvanceControl(t *testing.T) {
	_, handler := newTestServer(t)
	game, host, guest := createJoinedGame(t, handler)
	w := request(t, handler, http.MethodPost, "/v1/games/"+game.ID+"/events", guest, map[string]any{
		"ExpectedVersion": game.Version, "Kind": "state.updated", "State": map[string]any{"phase": "CLAIM TERRITORIES"},
	})
	if w.Code != http.StatusConflict {
		t.Fatalf("non-current player update returned %d: %s", w.Code, w.Body.String())
	}
	w = request(t, handler, http.MethodPost, "/v1/games/"+game.ID+"/events", host, map[string]any{
		"ExpectedVersion": game.Version, "Kind": "state.updated",
		"Payload": map[string]any{"currentPlayerId": game.PlayerIDs[1]},
		"State":   map[string]any{"phase": "CLAIM TERRITORIES"},
	})
	if w.Code != http.StatusOK {
		t.Fatalf("current player update returned %d: %s", w.Code, w.Body.String())
	}
	var updated Game
	_ = json.Unmarshal(w.Body.Bytes(), &updated)
	if updated.CurrentPlayerID != game.PlayerIDs[1] || updated.Version != game.Version+1 {
		t.Fatalf("control did not advance: %#v", updated)
	}
}

func TestConcurrentEventsAcceptExactlyOneVersion(t *testing.T) {
	_, h := newTestServer(t)
	g, host, guest := createJoinedGame(t, h)
	results := make(chan int, 2)
	var wg sync.WaitGroup
	for _, token := range []string{host, guest} {
		wg.Add(1)
		go func(token string) {
			defer wg.Done()
			w := request(t, h, "POST", "/v1/games/"+g.ID+"/events", token, map[string]any{"ExpectedVersion": g.Version, "Kind": "turn.completed"})
			results <- w.Code
		}(token)
	}
	wg.Wait()
	close(results)
	counts := map[int]int{}
	for status := range results {
		counts[status]++
	}
	if counts[http.StatusOK] != 1 || counts[http.StatusConflict] != 1 {
		t.Fatalf("statuses: %#v", counts)
	}
}

func TestRegistrationAssignsUUIDMythologyProfileAndTimezone(t *testing.T) {
	_, handler := newTestServer(t)
	registered := registrationTest(t, handler, "myth-phone")
	if len(registered.Player.ID) != 36 || strings.Count(registered.Player.ID, "-") != 4 {
		t.Fatalf("player does not have UUID: %q", registered.Player.ID)
	}
	if registered.Player.Name == "Ignored Name" || registered.Player.Name == "" {
		t.Fatalf("server did not assign mythology name: %q", registered.Player.Name)
	}
	if registered.Player.Timezone != "UTC" || registered.ProfileURL == "" {
		t.Fatalf("bad initial profile: %#v", registered)
	}
	profileURL, err := url.Parse(registered.ProfileURL)
	if err != nil || profileURL.Query().Get("key") == "" {
		t.Fatalf("profile URL missing capability key: %q", registered.ProfileURL)
	}
	w := request(t, handler, "GET", profileURL.RequestURI(), "", nil)
	if w.Code != http.StatusOK || !strings.Contains(w.Body.String(), registered.Player.Name) {
		t.Fatalf("profile page: %d %s", w.Code, w.Body.String())
	}
	w = request(t, handler, "GET", profileURL.Path+"/qr.png?"+profileURL.RawQuery, "", nil)
	if w.Code != http.StatusOK || !bytes.HasPrefix(w.Body.Bytes(), []byte{0x89, 'P', 'N', 'G'}) {
		t.Fatal("profile QR is not a PNG")
	}
	form := url.Values{"key": {profileURL.Query().Get("key")}, "name": {"Zeus the Bold"}, "timezone": {"America/New_York"}}
	r := httptest.NewRequest("POST", profileURL.Path, strings.NewReader(form.Encode()))
	r.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	w = httptest.NewRecorder()
	handler.ServeHTTP(w, r)
	if w.Code != http.StatusSeeOther {
		t.Fatalf("profile update: %d %s", w.Code, w.Body.String())
	}
	w = request(t, handler, "GET", "/v1/profile", registered.Token, nil)
	if w.Code != http.StatusOK || !bytes.Contains(w.Body.Bytes(), []byte("Zeus the Bold")) || !bytes.Contains(w.Body.Bytes(), []byte("America/New_York")) {
		t.Fatalf("updated profile missing: %s", w.Body.String())
	}
	if got := request(t, handler, "GET", profileURL.Path+"?key=wrong", "", nil).Code; got != http.StatusNotFound {
		t.Fatalf("wrong profile key returned %d", got)
	}
}

func TestTurnBasedRoundsAdvanceAndStateSurvivesRestart(t *testing.T) {
	path := filepath.Join(t.TempDir(), "rounds.sqlite")
	store, err := OpenStore(path)
	if err != nil {
		t.Fatal(err)
	}
	s := &Server{store: store, publicURL: "http://test", pairingCode: "pair", adminUsername: "admin", adminPassword: "cartarchy", subs: map[chan []byte]struct{}{}}
	handler := s.routes()
	host := registerTest(t, handler, "round-host")
	guest := registerTest(t, handler, "round-guest")
	w := request(t, handler, "POST", "/v1/games", host, map[string]any{"Name": "Round Trip", "Mode": "turn-based"})
	var game Game
	_ = json.Unmarshal(w.Body.Bytes(), &game)
	w = request(t, handler, "POST", "/v1/games/"+game.ID+"/join", guest, map[string]any{})
	_ = json.Unmarshal(w.Body.Bytes(), &game)
	if game.Status != Active || game.CurrentRound != 1 || game.CurrentTurn != 1 || game.CurrentPlayerID != game.PlayerIDs[0] {
		t.Fatalf("bad initial turn: %#v", game)
	}
	w = request(t, handler, "POST", "/v1/games/"+game.ID+"/events", host, map[string]any{
		"ExpectedVersion": game.Version, "Kind": "turn.completed", "Payload": map[string]any{"nextPhase": "reinforce"},
		"State": map[string]any{"phase": "reinforce", "territories": map[string]any{"alaska": map[string]any{"owner": 0, "armies": 3}}},
	})
	_ = json.Unmarshal(w.Body.Bytes(), &game)
	if game.CurrentRound != 1 || game.CurrentTurn != 2 || game.CurrentPlayerID != game.PlayerIDs[1] {
		t.Fatalf("first advance failed: %#v", game)
	}
	w = request(t, handler, "POST", "/v1/games/"+game.ID+"/events", guest, map[string]any{"ExpectedVersion": game.Version, "Kind": "turn.completed"})
	_ = json.Unmarshal(w.Body.Bytes(), &game)
	if game.CurrentRound != 2 || game.CurrentTurn != 3 || game.CurrentPlayerID != game.PlayerIDs[0] {
		t.Fatalf("round rollover failed: %#v", game)
	}
	w = request(t, handler, "GET", "/v1/games/"+game.ID+"/rounds", host, nil)
	var rounds []GameRound
	if w.Code != http.StatusOK || json.Unmarshal(w.Body.Bytes(), &rounds) != nil || len(rounds) != 2 {
		t.Fatalf("round history: %d %s", w.Code, w.Body.String())
	}
	if rounds[0].Status != "completed" || len(rounds[0].Turns) != 2 || rounds[1].Status != "active" || len(rounds[1].Turns) != 1 {
		t.Fatalf("bad round records: %#v", rounds)
	}
	if err = store.Close(); err != nil {
		t.Fatal(err)
	}
	reopened, err := OpenStore(path)
	if err != nil {
		t.Fatal(err)
	}
	defer reopened.Close()
	persisted, err := reopened.GetGame(game.ID, true)
	if err != nil || persisted.CurrentRound != 2 || persisted.CurrentTurn != 3 || persisted.State["phase"] != "reinforce" {
		t.Fatalf("state did not survive restart: %#v %v", persisted, err)
	}
}

func TestPublicDashboardCreatesShareableGame(t *testing.T) {
	_, handler := newTestServer(t)
	form := url.Values{"name": {"Public Campaign"}, "mode": {"turn-based"}, "maxPlayers": {"4"}}
	r := httptest.NewRequest("POST", "/games", strings.NewReader(form.Encode()))
	r.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	w := httptest.NewRecorder()
	handler.ServeHTTP(w, r)
	if w.Code != http.StatusSeeOther || !strings.HasPrefix(w.Header().Get("Location"), "/games/") {
		t.Fatalf("create page: %d %#v", w.Code, w.Header())
	}
	location := w.Header().Get("Location")
	w = request(t, handler, "GET", location, "", nil)
	if w.Code != http.StatusOK || !strings.Contains(w.Body.String(), "Public Campaign") || !strings.Contains(w.Body.String(), "Join this game") {
		t.Fatalf("game page: %d %s", w.Code, w.Body.String())
	}
	w = request(t, handler, "GET", location+"/qr.png", "", nil)
	if w.Code != http.StatusOK || !bytes.HasPrefix(w.Body.Bytes(), []byte{0x89, 'P', 'N', 'G'}) {
		t.Fatal("game QR is not a PNG")
	}
	w = request(t, handler, "GET", "/", "", nil)
	if !strings.Contains(w.Body.String(), "Public Campaign") || !strings.Contains(w.Body.String(), "Leaderboard") {
		t.Fatal("dashboard missing game or leaderboard")
	}
}

func TestAdminBasicAuthAndManagementActions(t *testing.T) {
	_, handler := newTestServer(t)
	registered := registrationTest(t, handler, "admin-phone")
	if got := request(t, handler, "GET", "/admin", "", nil).Code; got != http.StatusUnauthorized {
		t.Fatalf("unprotected admin returned %d", got)
	}
	adminRequest := func(method, path string) *httptest.ResponseRecorder {
		r := httptest.NewRequest(method, path, nil)
		r.SetBasicAuth("admin", "cartarchy")
		w := httptest.NewRecorder()
		handler.ServeHTTP(w, r)
		return w
	}
	w := adminRequest("GET", "/admin")
	if w.Code != http.StatusOK || !strings.Contains(w.Body.String(), registered.Player.Name) || !strings.Contains(w.Body.String(), "admin-phone") {
		t.Fatalf("admin page: %d %s", w.Code, w.Body.String())
	}
	w = adminRequest("POST", "/admin/players/"+registered.Player.ID+"/disable")
	if w.Code != http.StatusSeeOther {
		t.Fatalf("disable: %d %s", w.Code, w.Body.String())
	}
	if got := request(t, handler, "GET", "/v1/profile", registered.Token, nil).Code; got != http.StatusUnauthorized {
		t.Fatalf("disabled player token returned %d", got)
	}
	if got := adminRequest("POST", "/admin/players/"+registered.Player.ID+"/enable").Code; got != http.StatusSeeOther {
		t.Fatalf("enable returned %d", got)
	}
	if got := adminRequest("POST", "/admin/devices/"+registered.DeviceID+"/restore").Code; got != http.StatusSeeOther {
		t.Fatalf("restore device returned %d", got)
	}
	newRegistration := registrationTest(t, handler, "admin-phone")
	if newRegistration.Token == registered.Token {
		t.Fatal("restored device did not rotate token")
	}
	guest := registrationTest(t, handler, "admin-guest")
	w = request(t, handler, "POST", "/v1/games", newRegistration.Token, map[string]any{"Name": "Admin Campaign", "Mode": "realtime"})
	var game Game
	_ = json.Unmarshal(w.Body.Bytes(), &game)
	w = request(t, handler, "POST", "/v1/games/"+game.ID+"/join", guest.Token, map[string]any{})
	_ = json.Unmarshal(w.Body.Bytes(), &game)
	if game.Status != Active {
		t.Fatalf("admin test game did not activate: %s", w.Body.String())
	}
	for _, step := range []struct {
		action string
		status GameStatus
	}{{"pause", Paused}, {"resume", Active}, {"close", Finished}} {
		if got := adminRequest("POST", "/admin/games/"+game.ID+"/"+step.action).Code; got != http.StatusSeeOther {
			t.Fatalf("admin %s returned %d", step.action, got)
		}
		persistedResponse := request(t, handler, "GET", "/v1/games/"+game.ID, newRegistration.Token, nil)
		var persisted Game
		err := json.Unmarshal(persistedResponse.Body.Bytes(), &persisted)
		if persistedResponse.Code != http.StatusOK || err != nil || persisted.Status != step.status {
			t.Fatalf("admin %s did not persist: %d %#v %v", step.action, persistedResponse.Code, persisted, err)
		}
	}
	if got := adminRequest("POST", "/admin/games/"+game.ID+"/delete").Code; got != http.StatusSeeOther {
		t.Fatalf("admin delete returned %d", got)
	}
	if got := request(t, handler, "GET", "/v1/games/"+game.ID, newRegistration.Token, nil).Code; got != http.StatusNotFound {
		t.Fatalf("deleted game returned %d", got)
	}
}

func TestLegacyJSONIsMigratedToSQLiteWithBackup(t *testing.T) {
	path := filepath.Join(t.TempDir(), "cartarchy.json")
	legacy := `{"players":{"old":{"ID":"old","DeviceID":"legacy-phone","Name":"Legacy Mars","Token":"legacy-token","Wins":3,"Losses":2,"CreatedAt":"2026-01-01T00:00:00Z"}},"games":{"old-game":{"ID":"old-game","Name":"Legacy War","Mode":"turn-based","Status":"active","HostID":"old","PlayerIDs":["old"],"CurrentPlayerID":"old","Version":2,"CreatedAt":"2026-01-01T00:00:00Z","UpdatedAt":"2026-01-02T00:00:00Z"}}}`
	if err := os.WriteFile(path, []byte(legacy), 0600); err != nil {
		t.Fatal(err)
	}
	store, err := OpenStore(path)
	if err != nil {
		t.Fatal(err)
	}
	defer store.Close()
	header := make([]byte, 16)
	file, err := os.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	_, _ = file.Read(header)
	_ = file.Close()
	if string(header) != "SQLite format 3\x00" {
		t.Fatalf("not SQLite: %q", header)
	}
	if _, err = os.Stat(path + ".legacy.json"); err != nil {
		t.Fatal("legacy backup missing")
	}
	players, err := store.ListPlayers(true)
	if err != nil || len(players) != 1 || players[0].Name != "Legacy Mars" || players[0].Wins != 3 {
		t.Fatalf("legacy player not migrated: %#v %v", players, err)
	}
	games, err := store.ListAllGames()
	if err != nil || len(games) != 1 || games[0].Status != Active || games[0].CurrentRound != 1 {
		t.Fatalf("legacy active game not migrated: %#v %v", games, err)
	}
	rounds, err := store.ListRounds(games[0].ID)
	if err != nil || len(rounds) != 1 || len(rounds[0].Turns) != 1 || rounds[0].Turns[0].Status != "active" {
		t.Fatalf("legacy active turn not reconstructed: %#v %v", rounds, err)
	}
}

func readSSEEvent(t *testing.T, reader *bufio.Reader) string {
	t.Helper()
	var b strings.Builder
	for {
		line, err := reader.ReadString('\n')
		if err != nil && err != io.EOF {
			t.Fatal(err)
		}
		b.WriteString(line)
		if line == "\n" || err == io.EOF {
			return b.String()
		}
	}
}

func TestGameLifecyclePersistsStats(t *testing.T) {
	store, _ := OpenStore(filepath.Join(t.TempDir(), "db.json"))
	s := &Server{store: store, publicURL: "http://test", pairingCode: "pair", subs: map[chan []byte]struct{}{}}
	h := s.routes()
	a := registerTest(t, h, "alice")
	b := registerTest(t, h, "bob")
	w := request(t, h, "POST", "/v1/games", a, map[string]any{"Name": "Campaign", "Mode": "turn-based"})
	if w.Code != 201 {
		t.Fatal(w.Body.String())
	}
	var g Game
	_ = json.Unmarshal(w.Body.Bytes(), &g)
	w = request(t, h, "POST", "/v1/games/"+g.ID+"/join", b, map[string]any{})
	if w.Code != 200 {
		t.Fatal(w.Body.String())
	}
	w = request(t, h, "POST", "/v1/games/"+g.ID+"/pause", a, map[string]any{})
	if w.Code != 200 {
		t.Fatal(w.Body.String())
	}
	w = request(t, h, "POST", "/v1/games/"+g.ID+"/resume", a, map[string]any{})
	if w.Code != 200 {
		t.Fatal(w.Body.String())
	}
	_ = json.Unmarshal(w.Body.Bytes(), &g)
	w = request(t, h, "POST", "/v1/games/"+g.ID+"/events", a, map[string]any{"ExpectedVersion": g.Version, "Kind": "turn.completed", "Payload": map[string]any{"turn": 1}})
	if w.Code != 200 {
		t.Fatal(w.Body.String())
	}
	winner := g.PlayerIDs[0]
	w = request(t, h, "POST", "/v1/games/"+g.ID+"/finish", a, map[string]any{"WinnerID": winner})
	if w.Code != 200 {
		t.Fatal(w.Body.String())
	}
	w = request(t, h, "GET", "/v1/stats", a, nil)
	if w.Code != 200 || !bytes.Contains(w.Body.Bytes(), []byte(`"wins":1`)) {
		t.Fatalf("stats: %s", w.Body.String())
	}
	loaded, err := OpenStore(store.path)
	if err != nil {
		t.Fatal(err)
	}
	defer loaded.Close()
	persisted, err := loaded.GetGame(g.ID, true)
	if err != nil || persisted.Status != Finished {
		t.Fatal("finished game was not persisted")
	}
}
