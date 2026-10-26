package main

import (
	"context"
	"crypto/rand"
	"database/sql"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	_ "modernc.org/sqlite"
)

var (
	ErrNotFound  = errors.New("not found")
	ErrConflict  = errors.New("conflict")
	ErrForbidden = errors.New("forbidden")
)

type Store struct {
	mu   sync.Mutex
	path string
	db   *sql.DB
}

const schema = `
CREATE TABLE IF NOT EXISTS schema_migrations (
    version INTEGER PRIMARY KEY,
    applied_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS players (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    timezone TEXT NOT NULL DEFAULT 'UTC',
    profile_secret TEXT NOT NULL UNIQUE,
    wins INTEGER NOT NULL DEFAULT 0 CHECK (wins >= 0),
    losses INTEGER NOT NULL DEFAULT 0 CHECK (losses >= 0),
    disabled INTEGER NOT NULL DEFAULT 0 CHECK (disabled IN (0, 1)),
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS devices (
    id TEXT PRIMARY KEY,
    player_id TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    device_identifier TEXT NOT NULL UNIQUE,
    bearer_token TEXT NOT NULL UNIQUE,
    created_at TEXT NOT NULL,
    last_seen_at TEXT NOT NULL,
    revoked_at TEXT
);
CREATE TABLE IF NOT EXISTS games (
    id TEXT PRIMARY KEY,
    join_code TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL,
    mode TEXT NOT NULL CHECK (mode IN ('realtime', 'turn-based')),
    status TEXT NOT NULL CHECK (status IN ('waiting', 'active', 'paused', 'finished')),
    host_player_id TEXT REFERENCES players(id) ON DELETE SET NULL,
    current_player_id TEXT REFERENCES players(id) ON DELETE SET NULL,
    winner_player_id TEXT REFERENCES players(id) ON DELETE SET NULL,
    max_players INTEGER NOT NULL CHECK (max_players BETWEEN 2 AND 6),
    current_round INTEGER NOT NULL DEFAULT 0,
    current_turn INTEGER NOT NULL DEFAULT 0,
    version INTEGER NOT NULL DEFAULT 1,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS game_players (
    game_id TEXT NOT NULL REFERENCES games(id) ON DELETE CASCADE,
    player_id TEXT NOT NULL REFERENCES players(id) ON DELETE CASCADE,
    seat INTEGER NOT NULL,
    joined_at TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'eliminated', 'left')),
    PRIMARY KEY (game_id, player_id),
    UNIQUE (game_id, seat)
);
CREATE TABLE IF NOT EXISTS game_rounds (
    id TEXT PRIMARY KEY,
    game_id TEXT NOT NULL REFERENCES games(id) ON DELETE CASCADE,
    round_number INTEGER NOT NULL CHECK (round_number > 0),
    status TEXT NOT NULL CHECK (status IN ('active', 'completed')),
    starting_player_id TEXT REFERENCES players(id) ON DELETE SET NULL,
    started_at TEXT NOT NULL,
    completed_at TEXT,
    UNIQUE (game_id, round_number)
);
CREATE TABLE IF NOT EXISTS game_turns (
    id TEXT PRIMARY KEY,
    game_id TEXT NOT NULL REFERENCES games(id) ON DELETE CASCADE,
    round_id TEXT NOT NULL REFERENCES game_rounds(id) ON DELETE CASCADE,
    turn_number INTEGER NOT NULL CHECK (turn_number > 0),
    turn_in_round INTEGER NOT NULL CHECK (turn_in_round > 0),
    player_id TEXT REFERENCES players(id) ON DELETE SET NULL,
    status TEXT NOT NULL CHECK (status IN ('active', 'completed', 'skipped')),
    phase TEXT NOT NULL DEFAULT 'reinforce',
    version_start INTEGER NOT NULL,
    version_end INTEGER,
    started_at TEXT,
    deadline_at TEXT,
    completed_at TEXT,
    UNIQUE (game_id, turn_number),
    UNIQUE (round_id, turn_in_round)
);
CREATE TABLE IF NOT EXISTS game_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    game_id TEXT NOT NULL REFERENCES games(id) ON DELETE CASCADE,
    round_id TEXT REFERENCES game_rounds(id) ON DELETE SET NULL,
    turn_id TEXT REFERENCES game_turns(id) ON DELETE SET NULL,
    version INTEGER NOT NULL,
    player_id TEXT REFERENCES players(id) ON DELETE SET NULL,
    kind TEXT NOT NULL,
    payload_json TEXT NOT NULL DEFAULT '{}',
    created_at TEXT NOT NULL,
    UNIQUE (game_id, version)
);
CREATE TABLE IF NOT EXISTS game_state (
    game_id TEXT PRIMARY KEY REFERENCES games(id) ON DELETE CASCADE,
    version INTEGER NOT NULL,
    state_json TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_devices_player ON devices(player_id);
CREATE INDEX IF NOT EXISTS idx_games_status_updated ON games(status, updated_at DESC);
CREATE INDEX IF NOT EXISTS idx_game_players_player ON game_players(player_id, game_id);
CREATE INDEX IF NOT EXISTS idx_game_events_game_version ON game_events(game_id, version);
CREATE INDEX IF NOT EXISTS idx_rounds_game_number ON game_rounds(game_id, round_number);
CREATE INDEX IF NOT EXISTS idx_turns_game_number ON game_turns(game_id, turn_number);
CREATE UNIQUE INDEX IF NOT EXISTS idx_one_active_round ON game_rounds(game_id) WHERE status='active';
CREATE UNIQUE INDEX IF NOT EXISTS idx_one_active_turn ON game_turns(game_id) WHERE status='active';
INSERT OR IGNORE INTO schema_migrations(version, applied_at) VALUES (1, CURRENT_TIMESTAMP);
`

func OpenStore(path string) (*Store, error) {
	if err := os.MkdirAll(filepath.Dir(path), 0750); err != nil {
		return nil, err
	}
	legacy, err := moveLegacyJSON(path)
	if err != nil {
		return nil, err
	}
	abs, err := filepath.Abs(path)
	if err != nil {
		return nil, err
	}
	dsn := (&url.URL{Scheme: "file", Path: abs}).String() + "?_pragma=foreign_keys(1)&_pragma=busy_timeout(5000)&_pragma=journal_mode(WAL)"
	db, err := sql.Open("sqlite", dsn)
	if err != nil {
		return nil, err
	}
	db.SetMaxOpenConns(1)
	if err = db.Ping(); err != nil {
		db.Close()
		return nil, err
	}
	if _, err = db.Exec(schema); err != nil {
		db.Close()
		return nil, fmt.Errorf("create schema: %w", err)
	}
	s := &Store{path: path, db: db}
	if legacy != nil {
		if err = s.importLegacy(legacy); err != nil {
			db.Close()
			return nil, fmt.Errorf("import legacy JSON: %w", err)
		}
	}
	return s, nil
}

func (s *Store) Close() error { return s.db.Close() }

func randomID(bytes int) string {
	b := make([]byte, bytes)
	if _, err := rand.Read(b); err != nil {
		panic(err)
	}
	return hex.EncodeToString(b)
}

func randomUUID() string {
	b := make([]byte, 16)
	if _, err := rand.Read(b); err != nil {
		panic(err)
	}
	b[6] = (b[6] & 0x0f) | 0x40
	b[8] = (b[8] & 0x3f) | 0x80
	return fmt.Sprintf("%08x-%04x-%04x-%04x-%012x", b[0:4], b[4:6], b[6:8], b[8:10], b[10:16])
}

var mythologyNames = []string{
	"Achilles", "Aeneas", "Apollo", "Ares", "Artemis", "Athena", "Atlas", "Bellona",
	"Castor", "Ceres", "Diana", "Hector", "Hera", "Hermes", "Janus", "Juno", "Jupiter",
	"Mars", "Mercury", "Minerva", "Neptune", "Odysseus", "Orion", "Perseus", "Pollux",
	"Romulus", "Selene", "Theseus", "Venus", "Vesta", "Vulcan", "Zephyr",
}

func (s *Store) availableMythologyName(tx *sql.Tx) (string, error) {
	seed := make([]byte, 2)
	if _, err := rand.Read(seed); err != nil {
		return "", err
	}
	start := int(seed[0])<<8 | int(seed[1])
	for i := 0; i < len(mythologyNames)*100; i++ {
		base := mythologyNames[(start+i)%len(mythologyNames)]
		name := base
		if i >= len(mythologyNames) {
			name = fmt.Sprintf("%s-%d", base, i/len(mythologyNames)+1)
		}
		var count int
		if err := tx.QueryRow(`SELECT COUNT(*) FROM players WHERE name = ?`, name).Scan(&count); err != nil {
			return "", err
		}
		if count == 0 {
			return name, nil
		}
	}
	return "Commander-" + randomID(3), nil
}

func (s *Store) RegisterDevice(identifier string) (*Player, *Device, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	tx, err := s.db.Begin()
	if err != nil {
		return nil, nil, err
	}
	defer tx.Rollback()

	var device Device
	var player Player
	err = scanRegistration(tx.QueryRow(`
        SELECT d.id,d.player_id,d.device_identifier,d.bearer_token,d.created_at,d.last_seen_at,d.revoked_at,
               p.id,p.name,p.timezone,p.profile_secret,p.wins,p.losses,p.disabled,p.created_at,p.updated_at
        FROM devices d JOIN players p ON p.id=d.player_id WHERE d.device_identifier=?`, identifier), &device, &player)
	now := time.Now().UTC()
	if err == nil {
		if device.RevokedAt != nil || player.Disabled {
			return nil, nil, ErrForbidden
		}
		device.Token = randomID(32)
		device.LastSeenAt = now
		if _, err = tx.Exec(`UPDATE devices SET bearer_token=?,last_seen_at=? WHERE id=?`, device.Token, dbTime(now), device.ID); err != nil {
			return nil, nil, err
		}
		if err = tx.Commit(); err != nil {
			return nil, nil, err
		}
		return &player, &device, nil
	}
	if !errors.Is(err, sql.ErrNoRows) {
		return nil, nil, err
	}

	name, err := s.availableMythologyName(tx)
	if err != nil {
		return nil, nil, err
	}
	player = Player{ID: randomUUID(), Name: name, Timezone: "UTC", ProfileSecret: randomID(24), CreatedAt: now, UpdatedAt: now}
	device = Device{ID: randomUUID(), PlayerID: player.ID, DeviceIdentifier: identifier, Token: randomID(32), CreatedAt: now, LastSeenAt: now}
	if _, err = tx.Exec(`INSERT INTO players(id,name,timezone,profile_secret,created_at,updated_at) VALUES(?,?,?,?,?,?)`,
		player.ID, player.Name, player.Timezone, player.ProfileSecret, dbTime(now), dbTime(now)); err != nil {
		return nil, nil, err
	}
	if _, err = tx.Exec(`INSERT INTO devices(id,player_id,device_identifier,bearer_token,created_at,last_seen_at) VALUES(?,?,?,?,?,?)`,
		device.ID, device.PlayerID, device.DeviceIdentifier, device.Token, dbTime(now), dbTime(now)); err != nil {
		return nil, nil, err
	}
	if err = tx.Commit(); err != nil {
		return nil, nil, err
	}
	return &player, &device, nil
}

func (s *Store) PlayerByToken(token string) *Player {
	if token == "" {
		return nil
	}
	row := s.db.QueryRow(`SELECT p.id,p.name,p.timezone,p.profile_secret,p.wins,p.losses,p.disabled,p.created_at,p.updated_at
        FROM devices d JOIN players p ON p.id=d.player_id
        WHERE d.bearer_token=? AND d.revoked_at IS NULL AND p.disabled=0`, token)
	p, err := scanPlayer(row)
	if err != nil {
		return nil
	}
	return &p
}

func (s *Store) Player(id string) (Player, error) {
	return scanPlayer(s.db.QueryRow(`SELECT id,name,timezone,profile_secret,wins,losses,disabled,created_at,updated_at FROM players WHERE id=?`, id))
}

func (s *Store) UpdateProfile(id, name, timezone string) (Player, error) {
	name = strings.TrimSpace(name)
	if name == "" || len(name) > 40 {
		return Player{}, fmt.Errorf("name must be 1 to 40 characters")
	}
	if timezone == "" {
		timezone = "UTC"
	}
	if len(timezone) > 64 {
		return Player{}, fmt.Errorf("invalid timezone")
	}
	if _, err := time.LoadLocation(timezone); err != nil {
		return Player{}, fmt.Errorf("timezone must be an IANA name such as America/New_York")
	}
	result, err := s.db.Exec(`UPDATE players SET name=?,timezone=?,updated_at=? WHERE id=? AND disabled=0`, name, timezone, dbTime(time.Now().UTC()), id)
	if err != nil {
		return Player{}, err
	}
	if affected, _ := result.RowsAffected(); affected == 0 {
		return Player{}, ErrForbidden
	}
	return s.Player(id)
}

func (s *Store) CreateGame(hostID, name, mode string, maxPlayers int) (Game, error) {
	name = strings.TrimSpace(name)
	if name == "" {
		name = "Untitled Campaign"
	}
	if len(name) > 80 {
		return Game{}, fmt.Errorf("name must be at most 80 characters")
	}
	if mode != "realtime" && mode != "turn-based" {
		return Game{}, fmt.Errorf("mode must be realtime or turn-based")
	}
	if maxPlayers == 0 {
		maxPlayers = 2
	}
	if maxPlayers < 2 || maxPlayers > 6 {
		return Game{}, fmt.Errorf("maxPlayers must be between 2 and 6")
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	tx, err := s.db.Begin()
	if err != nil {
		return Game{}, err
	}
	defer tx.Rollback()
	if hostID != "" {
		var disabled bool
		if err = tx.QueryRow(`SELECT disabled FROM players WHERE id=?`, hostID).Scan(&disabled); err != nil || disabled {
			return Game{}, ErrForbidden
		}
	}
	now := time.Now().UTC()
	g := Game{ID: randomUUID(), JoinCode: strings.ToUpper(randomID(4)), Name: name, Mode: mode, Status: Waiting, HostID: hostID, MaxPlayers: maxPlayers, Version: 1, CreatedAt: now, UpdatedAt: now}
	var host any
	if hostID != "" {
		host = hostID
	}
	if _, err = tx.Exec(`INSERT INTO games(id,join_code,name,mode,status,host_player_id,max_players,version,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?)`,
		g.ID, g.JoinCode, g.Name, g.Mode, string(g.Status), host, g.MaxPlayers, g.Version, dbTime(now), dbTime(now)); err != nil {
		return Game{}, err
	}
	if hostID != "" {
		if _, err = tx.Exec(`INSERT INTO game_players(game_id,player_id,seat,joined_at) VALUES(?,?,0,?)`, g.ID, hostID, dbTime(now)); err != nil {
			return Game{}, err
		}
	}
	if err = insertEvent(tx, g.ID, 1, hostID, "", "", "game.created", map[string]any{"mode": mode, "maxPlayers": maxPlayers}, now); err != nil {
		return Game{}, err
	}
	if err = tx.Commit(); err != nil {
		return Game{}, err
	}
	return s.GetGame(g.ID, true)
}

func (s *Store) JoinGame(gameID, playerID string) (Game, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	tx, err := s.db.Begin()
	if err != nil {
		return Game{}, err
	}
	defer tx.Rollback()
	g, err := loadGame(tx, gameID, false)
	if err != nil {
		return Game{}, err
	}
	gameID = g.ID
	if contains(g.PlayerIDs, playerID) {
		return g, tx.Commit()
	}
	if g.Status != Waiting {
		return Game{}, fmt.Errorf("%w: only waiting games can be joined", ErrConflict)
	}
	if len(g.PlayerIDs) >= g.MaxPlayers {
		return Game{}, fmt.Errorf("%w: game is full", ErrConflict)
	}
	var disabled bool
	if err = tx.QueryRow(`SELECT disabled FROM players WHERE id=?`, playerID).Scan(&disabled); err != nil || disabled {
		return Game{}, ErrForbidden
	}
	now := time.Now().UTC()
	if _, err = tx.Exec(`INSERT INTO game_players(game_id,player_id,seat,joined_at) VALUES(?,?,?,?)`, gameID, playerID, len(g.PlayerIDs), dbTime(now)); err != nil {
		return Game{}, err
	}
	g.PlayerIDs = append(g.PlayerIDs, playerID)
	if g.HostID == "" {
		g.HostID = playerID
		if _, err = tx.Exec(`UPDATE games SET host_player_id=? WHERE id=?`, playerID, gameID); err != nil {
			return Game{}, err
		}
	}
	newVersion := g.Version + 1
	if len(g.PlayerIDs) == g.MaxPlayers {
		if err = startGameTx(tx, &g, newVersion, now); err != nil {
			return Game{}, err
		}
	}
	if _, err = tx.Exec(`UPDATE games SET status=?,current_player_id=?,current_round=?,current_turn=?,version=?,updated_at=? WHERE id=?`,
		string(g.Status), nullString(g.CurrentPlayerID), g.CurrentRound, g.CurrentTurn, newVersion, dbTime(now), gameID); err != nil {
		return Game{}, err
	}
	if err = insertEvent(tx, gameID, newVersion, playerID, "", "", "game.joined", map[string]any{"seat": len(g.PlayerIDs) - 1}, now); err != nil {
		return Game{}, err
	}
	if err = tx.Commit(); err != nil {
		return Game{}, err
	}
	return s.GetGame(gameID, true)
}

func (s *Store) StartGame(gameID, actorID string) (Game, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	tx, err := s.db.Begin()
	if err != nil {
		return Game{}, err
	}
	defer tx.Rollback()
	g, err := loadGame(tx, gameID, false)
	if err != nil {
		return Game{}, err
	}
	if g.HostID != actorID {
		return Game{}, ErrForbidden
	}
	if g.Status != Waiting || len(g.PlayerIDs) < 2 {
		return Game{}, fmt.Errorf("%w: game needs at least two players and must be waiting", ErrConflict)
	}
	now := time.Now().UTC()
	newVersion := g.Version + 1
	if err = startGameTx(tx, &g, newVersion, now); err != nil {
		return Game{}, err
	}
	if _, err = tx.Exec(`UPDATE games SET status=?,current_player_id=?,current_round=?,current_turn=?,version=?,updated_at=? WHERE id=?`,
		string(g.Status), g.CurrentPlayerID, g.CurrentRound, g.CurrentTurn, newVersion, dbTime(now), gameID); err != nil {
		return Game{}, err
	}
	if err = insertEvent(tx, gameID, newVersion, actorID, "", "", "game.started", nil, now); err != nil {
		return Game{}, err
	}
	if err = tx.Commit(); err != nil {
		return Game{}, err
	}
	return s.GetGame(gameID, true)
}

func startGameTx(tx *sql.Tx, g *Game, version int, now time.Time) error {
	if len(g.PlayerIDs) < 2 {
		return fmt.Errorf("%w: game needs at least two players", ErrConflict)
	}
	g.Status = Active
	g.CurrentPlayerID = g.PlayerIDs[0]
	g.CurrentRound = 1
	g.CurrentTurn = 1
	roundID := randomUUID()
	turnID := randomUUID()
	if _, err := tx.Exec(`INSERT INTO game_rounds(id,game_id,round_number,status,starting_player_id,started_at) VALUES(?,?,1,'active',?,?)`, roundID, g.ID, g.CurrentPlayerID, dbTime(now)); err != nil {
		return err
	}
	_, err := tx.Exec(`INSERT INTO game_turns(id,game_id,round_id,turn_number,turn_in_round,player_id,status,phase,version_start,started_at) VALUES(?,?,?,1,1,?,'active','reinforce',?,?)`,
		turnID, g.ID, roundID, g.CurrentPlayerID, version, dbTime(now))
	return err
}

func (s *Store) SetGameStatus(gameID, actorID string, target GameStatus) (Game, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	tx, err := s.db.Begin()
	if err != nil {
		return Game{}, err
	}
	defer tx.Rollback()
	g, err := loadGame(tx, gameID, false)
	if err != nil {
		return Game{}, err
	}
	if !contains(g.PlayerIDs, actorID) {
		return Game{}, ErrNotFound
	}
	valid := (g.Status == Active && target == Paused) || (g.Status == Paused && target == Active)
	if !valid {
		return Game{}, fmt.Errorf("%w: cannot change %s game to %s", ErrConflict, g.Status, target)
	}
	now := time.Now().UTC()
	version := g.Version + 1
	if _, err = tx.Exec(`UPDATE games SET status=?,version=?,updated_at=? WHERE id=?`, string(target), version, dbTime(now), gameID); err != nil {
		return Game{}, err
	}
	if err = insertEvent(tx, gameID, version, actorID, "", "", "game."+string(target), nil, now); err != nil {
		return Game{}, err
	}
	if err = tx.Commit(); err != nil {
		return Game{}, err
	}
	return s.GetGame(gameID, true)
}

func (s *Store) ResignGame(gameID, actorID string) (Game, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	tx, err := s.db.Begin()
	if err != nil {
		return Game{}, err
	}
	defer tx.Rollback()
	g, err := loadGame(tx, gameID, false)
	if err != nil {
		return Game{}, err
	}
	if !contains(g.PlayerIDs, actorID) {
		return Game{}, ErrNotFound
	}
	if g.Status == Finished {
		return Game{}, fmt.Errorf("%w: game is already finished", ErrConflict)
	}
	originalPlayers := append([]string(nil), g.PlayerIDs...)
	now := time.Now().UTC()
	version := g.Version + 1
	if _, err = tx.Exec(`UPDATE game_players SET status='left' WHERE game_id=? AND player_id=? AND status='active'`, g.ID, actorID); err != nil {
		return Game{}, err
	}
	remaining := make([]string, 0, len(g.PlayerIDs)-1)
	for _, id := range g.PlayerIDs {
		if id != actorID {
			remaining = append(remaining, id)
		}
	}
	g.PlayerIDs = remaining
	if g.HostID == actorID {
		g.HostID = ""
		if len(remaining) > 0 {
			g.HostID = remaining[0]
		}
	}
	if len(remaining) <= 1 {
		g.Status = Finished
		g.CurrentPlayerID = ""
		if len(remaining) == 1 {
			g.WinnerID = remaining[0]
			if _, err = tx.Exec(`UPDATE players SET wins=wins+1,updated_at=? WHERE id=?`, dbTime(now), g.WinnerID); err != nil {
				return Game{}, err
			}
		}
		if _, err = tx.Exec(`UPDATE players SET losses=losses+1,updated_at=? WHERE id=?`, dbTime(now), actorID); err != nil {
			return Game{}, err
		}
		if _, err = tx.Exec(`UPDATE game_turns SET status='completed',version_end=?,completed_at=? WHERE game_id=? AND status='active'`, version, dbTime(now), g.ID); err != nil {
			return Game{}, err
		}
		if _, err = tx.Exec(`UPDATE game_rounds SET status='completed',completed_at=? WHERE game_id=? AND status='active'`, dbTime(now), g.ID); err != nil {
			return Game{}, err
		}
	} else if g.CurrentPlayerID == actorID {
		next := remaining[0]
		for offset := 1; offset <= len(originalPlayers); offset++ {
			candidate := originalPlayers[(indexOf(originalPlayers, actorID)+offset)%len(originalPlayers)]
			if contains(remaining, candidate) {
				next = candidate
				break
			}
		}
		if _, err = tx.Exec(`UPDATE game_turns SET status='completed',version_end=?,completed_at=? WHERE game_id=? AND status='active'`, version, dbTime(now), g.ID); err != nil {
			return Game{}, err
		}
		if _, err = tx.Exec(`UPDATE game_rounds SET status='completed',completed_at=? WHERE game_id=? AND status='active'`, dbTime(now), g.ID); err != nil {
			return Game{}, err
		}
		g.CurrentPlayerID = next
		g.CurrentRound++
		g.CurrentTurn++
		roundID := randomUUID()
		if _, err = tx.Exec(`INSERT INTO game_rounds(id,game_id,round_number,status,starting_player_id,started_at) VALUES(?,?,?,'active',?,?)`, roundID, g.ID, g.CurrentRound, next, dbTime(now)); err != nil {
			return Game{}, err
		}
		if _, err = tx.Exec(`INSERT INTO game_turns(id,game_id,round_id,turn_number,turn_in_round,player_id,status,phase,version_start,started_at) VALUES(?,?,?,?,1,?,'active','reinforce',?,?)`, randomUUID(), g.ID, roundID, g.CurrentTurn, next, version, dbTime(now)); err != nil {
			return Game{}, err
		}
	}
	if _, err = tx.Exec(`UPDATE games SET status=?,host_player_id=?,current_player_id=?,winner_player_id=?,version=?,updated_at=? WHERE id=?`,
		string(g.Status), nullString(g.HostID), nullString(g.CurrentPlayerID), nullString(g.WinnerID), version, dbTime(now), g.ID); err != nil {
		return Game{}, err
	}
	if err = insertEvent(tx, g.ID, version, actorID, "", "", "game.resigned", map[string]any{"remainingPlayers": len(remaining)}, now); err != nil {
		return Game{}, err
	}
	if err = tx.Commit(); err != nil {
		return Game{}, err
	}
	return s.GetGame(g.ID, true)
}

func (s *Store) AddGameEvent(gameID, playerID string, expected int, kind string, payload, state map[string]any) (Game, error) {
	if strings.TrimSpace(kind) == "" {
		return Game{}, fmt.Errorf("event kind is required")
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	tx, err := s.db.Begin()
	if err != nil {
		return Game{}, err
	}
	defer tx.Rollback()
	g, err := loadGame(tx, gameID, false)
	if err != nil {
		return Game{}, err
	}
	if !contains(g.PlayerIDs, playerID) {
		return Game{}, ErrNotFound
	}
	if g.Status != Active {
		return Game{}, fmt.Errorf("%w: game is not active", ErrConflict)
	}
	if expected != g.Version {
		return Game{}, fmt.Errorf("%w: current version is %d", ErrConflict, g.Version)
	}
	if g.Mode == "turn-based" && g.CurrentPlayerID != playerID {
		return Game{}, fmt.Errorf("%w: it is not this player's turn", ErrConflict)
	}
	now := time.Now().UTC()
	version := g.Version + 1
	roundID, turnID, _ := currentRoundTurn(tx, gameID)
	if kind == "turn.completed" {
		if err = advanceTurnTx(tx, &g, version, payload, now); err != nil {
			return Game{}, err
		}
	} else if nextPlayerID, ok := payload["currentPlayerId"].(string); ok && nextPlayerID != "" {
		if !contains(g.PlayerIDs, nextPlayerID) {
			return Game{}, fmt.Errorf("%w: next player is not seated", ErrConflict)
		}
		g.CurrentPlayerID = nextPlayerID
	}
	if state != nil {
		encoded, marshalErr := json.Marshal(state)
		if marshalErr != nil {
			return Game{}, marshalErr
		}
		if _, err = tx.Exec(`INSERT INTO game_state(game_id,version,state_json,updated_at) VALUES(?,?,?,?)
            ON CONFLICT(game_id) DO UPDATE SET version=excluded.version,state_json=excluded.state_json,updated_at=excluded.updated_at`,
			gameID, version, string(encoded), dbTime(now)); err != nil {
			return Game{}, err
		}
	}
	if _, err = tx.Exec(`UPDATE games SET current_player_id=?,current_round=?,current_turn=?,version=?,updated_at=? WHERE id=?`,
		nullString(g.CurrentPlayerID), g.CurrentRound, g.CurrentTurn, version, dbTime(now), gameID); err != nil {
		return Game{}, err
	}
	if err = insertEvent(tx, gameID, version, playerID, roundID, turnID, kind, payload, now); err != nil {
		return Game{}, err
	}
	if err = tx.Commit(); err != nil {
		return Game{}, err
	}
	return s.GetGame(gameID, true)
}

func advanceTurnTx(tx *sql.Tx, g *Game, version int, payload map[string]any, now time.Time) error {
	roundID, turnID, err := currentRoundTurn(tx, g.ID)
	if err != nil {
		return err
	}
	if _, err = tx.Exec(`UPDATE game_turns SET status='completed',version_end=?,completed_at=? WHERE id=? AND status='active'`, version, dbTime(now), turnID); err != nil {
		return err
	}
	currentIndex := -1
	for i, id := range g.PlayerIDs {
		if id == g.CurrentPlayerID {
			currentIndex = i
			break
		}
	}
	if currentIndex < 0 || len(g.PlayerIDs) == 0 {
		return fmt.Errorf("active player is not seated")
	}
	nextIndex := (currentIndex + 1) % len(g.PlayerIDs)
	g.CurrentPlayerID = g.PlayerIDs[nextIndex]
	g.CurrentTurn++
	phase := "reinforce"
	if value, ok := payload["nextPhase"].(string); ok && value != "" {
		phase = value
	}
	if nextIndex == 0 {
		if _, err = tx.Exec(`UPDATE game_rounds SET status='completed',completed_at=? WHERE id=?`, dbTime(now), roundID); err != nil {
			return err
		}
		g.CurrentRound++
		roundID = randomUUID()
		if _, err = tx.Exec(`INSERT INTO game_rounds(id,game_id,round_number,status,starting_player_id,started_at) VALUES(?,?,?,'active',?,?)`,
			roundID, g.ID, g.CurrentRound, g.CurrentPlayerID, dbTime(now)); err != nil {
			return err
		}
	}
	turnInRound := nextIndex + 1
	_, err = tx.Exec(`INSERT INTO game_turns(id,game_id,round_id,turn_number,turn_in_round,player_id,status,phase,version_start,started_at) VALUES(?,?,?,?,?,?,'active',?,?,?)`,
		randomUUID(), g.ID, roundID, g.CurrentTurn, turnInRound, g.CurrentPlayerID, phase, version, dbTime(now))
	return err
}

func (s *Store) FinishGame(gameID, actorID, winnerID string) (Game, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	tx, err := s.db.Begin()
	if err != nil {
		return Game{}, err
	}
	defer tx.Rollback()
	g, err := loadGame(tx, gameID, false)
	if err != nil {
		return Game{}, err
	}
	currentPlayerVictory := g.Mode == "turn-based" && actorID == g.CurrentPlayerID && winnerID == actorID
	if actorID != g.HostID && !currentPlayerVictory {
		return Game{}, fmt.Errorf("%w: only the host or winning current player may finish", ErrConflict)
	}
	if g.Status == Finished {
		return Game{}, fmt.Errorf("%w: game is already finished", ErrConflict)
	}
	if !contains(g.PlayerIDs, winnerID) {
		return Game{}, fmt.Errorf("%w: winner is not in game", ErrConflict)
	}
	now := time.Now().UTC()
	version := g.Version + 1
	if _, err = tx.Exec(`UPDATE games SET status='finished',winner_player_id=?,version=?,updated_at=? WHERE id=?`, winnerID, version, dbTime(now), gameID); err != nil {
		return Game{}, err
	}
	for _, id := range g.PlayerIDs {
		column := "losses"
		if id == winnerID {
			column = "wins"
		}
		if _, err = tx.Exec(`UPDATE players SET `+column+`=`+column+`+1,updated_at=? WHERE id=?`, dbTime(now), id); err != nil {
			return Game{}, err
		}
	}
	if _, err = tx.Exec(`UPDATE game_turns SET status='completed',version_end=?,completed_at=? WHERE game_id=? AND status='active'`, version, dbTime(now), gameID); err != nil {
		return Game{}, err
	}
	if _, err = tx.Exec(`UPDATE game_rounds SET status='completed',completed_at=? WHERE game_id=? AND status='active'`, dbTime(now), gameID); err != nil {
		return Game{}, err
	}
	if err = insertEvent(tx, gameID, version, actorID, "", "", "game.finished", map[string]any{"winnerId": winnerID}, now); err != nil {
		return Game{}, err
	}
	if err = tx.Commit(); err != nil {
		return Game{}, err
	}
	return s.GetGame(gameID, true)
}

func (s *Store) GetGame(id string, details bool) (Game, error) { return loadGame(s.db, id, details) }

func (s *Store) GetGameForPlayer(id, playerID string) (Game, error) {
	g, err := s.GetGame(id, true)
	if err != nil || !contains(g.PlayerIDs, playerID) {
		return Game{}, ErrNotFound
	}
	return g, nil
}

func (s *Store) ListGames(playerID string) ([]Game, error) {
	rows, err := s.db.Query(`SELECT g.id FROM games g WHERE g.status='waiting' OR EXISTS(SELECT 1 FROM game_players gp WHERE gp.game_id=g.id AND gp.player_id=? AND gp.status='active') ORDER BY g.updated_at DESC`, playerID)
	if err != nil {
		return nil, err
	}
	ids, err := collectIDs(rows)
	if err != nil {
		return nil, err
	}
	return s.gamesByIDs(ids)
}

func (s *Store) Dashboard() (Dashboard, error) {
	players, err := s.ListPlayers(false)
	if err != nil {
		return Dashboard{}, err
	}
	rows, err := s.db.Query(`SELECT id FROM games WHERE status IN ('waiting','active','paused') ORDER BY updated_at DESC LIMIT 50`)
	if err != nil {
		return Dashboard{}, err
	}
	ids, err := collectIDs(rows)
	if err != nil {
		return Dashboard{}, err
	}
	games, err := s.gamesByIDs(ids)
	if len(players) > 20 {
		players = players[:20]
	}
	return Dashboard{Leaderboard: players, Games: games}, err
}

func (s *Store) ListPlayers(includeDisabled bool) ([]Player, error) {
	query := `SELECT id,name,timezone,profile_secret,wins,losses,disabled,created_at,updated_at FROM players`
	if !includeDisabled {
		query += ` WHERE disabled=0`
	}
	query += ` ORDER BY wins DESC, losses ASC, name ASC`
	rows, err := s.db.Query(query)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	out := []Player{}
	for rows.Next() {
		p, err := scanPlayer(rows)
		if err != nil {
			return nil, err
		}
		out = append(out, p)
	}
	return out, rows.Err()
}

func (s *Store) ListDevices() ([]Device, error) {
	rows, err := s.db.Query(`SELECT id,player_id,device_identifier,bearer_token,created_at,last_seen_at,revoked_at FROM devices ORDER BY last_seen_at DESC`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	out := []Device{}
	for rows.Next() {
		d, err := scanDevice(rows)
		if err != nil {
			return nil, err
		}
		out = append(out, d)
	}
	return out, rows.Err()
}

func (s *Store) ListAllGames() ([]Game, error) {
	rows, err := s.db.Query(`SELECT id FROM games ORDER BY updated_at DESC`)
	if err != nil {
		return nil, err
	}
	ids, err := collectIDs(rows)
	if err != nil {
		return nil, err
	}
	return s.gamesByIDs(ids)
}

func (s *Store) ListRounds(gameID string) ([]GameRound, error) {
	rows, err := s.db.Query(`SELECT id,round_number,status,starting_player_id,started_at,completed_at FROM game_rounds WHERE game_id=? ORDER BY round_number`, gameID)
	if err != nil {
		return nil, err
	}
	rounds := []GameRound{}
	for rows.Next() {
		var r GameRound
		var starting, started string
		var completed sql.NullString
		if err = rows.Scan(&r.ID, &r.RoundNumber, &r.Status, &starting, &started, &completed); err != nil {
			return nil, err
		}
		r.GameID, r.StartingPlayerID, r.StartedAt = gameID, starting, parseTime(started)
		r.CompletedAt = parseOptionalTime(completed)
		rounds = append(rounds, r)
	}
	if err = rows.Err(); err != nil {
		rows.Close()
		return nil, err
	}
	rows.Close()
	for index := range rounds {
		turns, turnErr := s.turnsForRound(rounds[index].ID)
		if turnErr != nil {
			return nil, turnErr
		}
		rounds[index].Turns = turns
	}
	return rounds, nil
}

func (s *Store) turnsForRound(roundID string) ([]GameTurn, error) {
	rows, err := s.db.Query(`SELECT id,game_id,round_id,turn_number,turn_in_round,player_id,status,phase,version_start,version_end,started_at,deadline_at,completed_at FROM game_turns WHERE round_id=? ORDER BY turn_in_round`, roundID)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	turns := []GameTurn{}
	for rows.Next() {
		var t GameTurn
		var player sql.NullString
		var versionEnd sql.NullInt64
		var started, deadline, completed sql.NullString
		if err = rows.Scan(&t.ID, &t.GameID, &t.RoundID, &t.TurnNumber, &t.TurnInRound, &player, &t.Status, &t.Phase, &t.VersionStart, &versionEnd, &started, &deadline, &completed); err != nil {
			return nil, err
		}
		t.PlayerID = player.String
		if versionEnd.Valid {
			t.VersionEnd = int(versionEnd.Int64)
		}
		t.StartedAt, t.DeadlineAt, t.CompletedAt = parseOptionalTime(started), parseOptionalTime(deadline), parseOptionalTime(completed)
		turns = append(turns, t)
	}
	return turns, rows.Err()
}

func (s *Store) AdminPlayerAction(id, action string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	tx, err := s.db.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()
	now := dbTime(time.Now().UTC())
	switch action {
	case "disable":
		if _, err = tx.Exec(`UPDATE players SET disabled=1,updated_at=? WHERE id=?`, now, id); err == nil {
			_, err = tx.Exec(`UPDATE devices SET revoked_at=? WHERE player_id=? AND revoked_at IS NULL`, now, id)
		}
	case "enable":
		_, err = tx.Exec(`UPDATE players SET disabled=0,updated_at=? WHERE id=?`, now, id)
	case "delete":
		var activeGames int
		if err = tx.QueryRow(`SELECT COUNT(*) FROM game_players gp JOIN games g ON g.id=gp.game_id WHERE gp.player_id=? AND g.status IN ('active','paused')`, id).Scan(&activeGames); err != nil {
			return err
		}
		if activeGames > 0 {
			return fmt.Errorf("%w: close the player's active games before deletion", ErrConflict)
		}
		_, err = tx.Exec(`DELETE FROM players WHERE id=?`, id)
	default:
		return fmt.Errorf("unknown player action")
	}
	if err != nil {
		return err
	}
	return tx.Commit()
}

func (s *Store) AdminDeviceAction(id, action string) error {
	var result sql.Result
	var err error
	switch action {
	case "revoke":
		result, err = s.db.Exec(`UPDATE devices SET revoked_at=? WHERE id=?`, dbTime(time.Now().UTC()), id)
	case "restore":
		result, err = s.db.Exec(`UPDATE devices SET revoked_at=NULL,bearer_token=? WHERE id=?`, randomID(32), id)
	case "delete":
		result, err = s.db.Exec(`DELETE FROM devices WHERE id=?`, id)
	default:
		return fmt.Errorf("unknown device action")
	}
	if err != nil {
		return err
	}
	if affected, _ := result.RowsAffected(); affected == 0 {
		return ErrNotFound
	}
	return nil
}

func (s *Store) AdminGameAction(id, action string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if action == "delete" {
		result, err := s.db.Exec(`DELETE FROM games WHERE id=?`, id)
		if err != nil {
			return err
		}
		if affected, _ := result.RowsAffected(); affected == 0 {
			return ErrNotFound
		}
		return nil
	}
	tx, err := s.db.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()
	game, err := loadGame(tx, id, false)
	if err != nil {
		return err
	}
	var target GameStatus
	switch action {
	case "pause":
		if game.Status != Active {
			return fmt.Errorf("%w: only active games can be paused", ErrConflict)
		}
		target = Paused
	case "resume":
		if game.Status != Paused {
			return fmt.Errorf("%w: only paused games can be resumed", ErrConflict)
		}
		target = Active
	case "close":
		if game.Status == Finished {
			return fmt.Errorf("%w: game is already finished", ErrConflict)
		}
		target = Finished
	default:
		return fmt.Errorf("unknown game action")
	}
	now := time.Now().UTC()
	version := game.Version + 1
	if _, err = tx.Exec(`UPDATE games SET status=?,version=?,updated_at=? WHERE id=?`, string(target), version, dbTime(now), game.ID); err != nil {
		return err
	}
	if target == Finished {
		if _, err = tx.Exec(`UPDATE game_turns SET status='completed',version_end=?,completed_at=? WHERE game_id=? AND status='active'`, version, dbTime(now), game.ID); err != nil {
			return err
		}
		if _, err = tx.Exec(`UPDATE game_rounds SET status='completed',completed_at=? WHERE game_id=? AND status='active'`, dbTime(now), game.ID); err != nil {
			return err
		}
	}
	if err = insertEvent(tx, game.ID, version, "", "", "", "admin.game."+action, nil, now); err != nil {
		return err
	}
	return tx.Commit()
}

type queryer interface {
	QueryRowContext(context.Context, string, ...any) *sql.Row
	QueryContext(context.Context, string, ...any) (*sql.Rows, error)
}

func loadGame(q queryer, id string, details bool) (Game, error) {
	ctx := context.Background()
	var g Game
	var status, created, updated string
	var host, current, winner sql.NullString
	err := q.QueryRowContext(ctx, `SELECT id,join_code,name,mode,status,host_player_id,current_player_id,winner_player_id,max_players,current_round,current_turn,version,created_at,updated_at FROM games WHERE id=? OR join_code=?`, id, strings.ToUpper(id)).Scan(
		&g.ID, &g.JoinCode, &g.Name, &g.Mode, &status, &host, &current, &winner, &g.MaxPlayers, &g.CurrentRound, &g.CurrentTurn, &g.Version, &created, &updated)
	if errors.Is(err, sql.ErrNoRows) {
		return Game{}, ErrNotFound
	}
	if err != nil {
		return Game{}, err
	}
	g.Status, g.HostID, g.CurrentPlayerID, g.WinnerID = GameStatus(status), host.String, current.String, winner.String
	g.CreatedAt, g.UpdatedAt = parseTime(created), parseTime(updated)
	rows, err := q.QueryContext(ctx, `SELECT gp.player_id,p.name FROM game_players gp JOIN players p ON p.id=gp.player_id WHERE gp.game_id=? AND gp.status='active' ORDER BY gp.seat`, g.ID)
	if err != nil {
		return Game{}, err
	}
	for rows.Next() {
		var player GamePlayer
		if err = rows.Scan(&player.ID, &player.Name); err != nil {
			rows.Close()
			return Game{}, err
		}
		g.PlayerIDs = append(g.PlayerIDs, player.ID)
		g.Players = append(g.Players, player)
	}
	rows.Close()
	var stateJSON string
	if err = q.QueryRowContext(ctx, `SELECT state_json FROM game_state WHERE game_id=?`, g.ID).Scan(&stateJSON); err == nil {
		_ = json.Unmarshal([]byte(stateJSON), &g.State)
	} else if !errors.Is(err, sql.ErrNoRows) {
		return Game{}, err
	}
	if details {
		rows, err = q.QueryContext(ctx, `SELECT id,version,player_id,round_id,turn_id,kind,payload_json,created_at FROM game_events WHERE game_id=? ORDER BY version`, g.ID)
		if err != nil {
			return Game{}, err
		}
		for rows.Next() {
			var event GameEvent
			var player, round, turn sql.NullString
			var payload, at string
			if err = rows.Scan(&event.ID, &event.Version, &player, &round, &turn, &event.Kind, &payload, &at); err != nil {
				rows.Close()
				return Game{}, err
			}
			event.PlayerID, event.RoundID, event.TurnID, event.CreatedAt = player.String, round.String, turn.String, parseTime(at)
			_ = json.Unmarshal([]byte(payload), &event.Payload)
			g.Events = append(g.Events, event)
		}
		rows.Close()
	}
	return g, nil
}

func scanPlayer(row interface{ Scan(...any) error }) (Player, error) {
	var p Player
	var created, updated string
	err := row.Scan(&p.ID, &p.Name, &p.Timezone, &p.ProfileSecret, &p.Wins, &p.Losses, &p.Disabled, &created, &updated)
	p.CreatedAt, p.UpdatedAt = parseTime(created), parseTime(updated)
	return p, err
}

func scanDevice(row interface{ Scan(...any) error }) (Device, error) {
	var d Device
	var created, seen string
	var revoked sql.NullString
	err := row.Scan(&d.ID, &d.PlayerID, &d.DeviceIdentifier, &d.Token, &created, &seen, &revoked)
	d.CreatedAt, d.LastSeenAt, d.RevokedAt = parseTime(created), parseTime(seen), parseOptionalTime(revoked)
	return d, err
}

func scanRegistration(row interface{ Scan(...any) error }, d *Device, p *Player) error {
	var dCreated, seen, pCreated, pUpdated string
	var revoked sql.NullString
	err := row.Scan(&d.ID, &d.PlayerID, &d.DeviceIdentifier, &d.Token, &dCreated, &seen, &revoked,
		&p.ID, &p.Name, &p.Timezone, &p.ProfileSecret, &p.Wins, &p.Losses, &p.Disabled, &pCreated, &pUpdated)
	d.CreatedAt, d.LastSeenAt, d.RevokedAt = parseTime(dCreated), parseTime(seen), parseOptionalTime(revoked)
	p.CreatedAt, p.UpdatedAt = parseTime(pCreated), parseTime(pUpdated)
	return err
}

func insertEvent(tx *sql.Tx, gameID string, version int, playerID, roundID, turnID, kind string, payload map[string]any, at time.Time) error {
	if payload == nil {
		payload = map[string]any{}
	}
	b, err := json.Marshal(payload)
	if err != nil {
		return err
	}
	_, err = tx.Exec(`INSERT INTO game_events(game_id,version,player_id,round_id,turn_id,kind,payload_json,created_at) VALUES(?,?,?,?,?,?,?,?)`,
		gameID, version, nullString(playerID), nullString(roundID), nullString(turnID), kind, string(b), dbTime(at))
	return err
}

func currentRoundTurn(tx *sql.Tx, gameID string) (string, string, error) {
	var roundID, turnID string
	err := tx.QueryRow(`SELECT r.id,t.id FROM game_rounds r JOIN game_turns t ON t.round_id=r.id WHERE r.game_id=? AND r.status='active' AND t.status='active'`, gameID).Scan(&roundID, &turnID)
	return roundID, turnID, err
}

func (s *Store) gamesByIDs(ids []string) ([]Game, error) {
	out := make([]Game, 0, len(ids))
	for _, id := range ids {
		g, err := s.GetGame(id, false)
		if err != nil {
			return nil, err
		}
		out = append(out, g)
	}
	return out, nil
}

func collectIDs(rows *sql.Rows) ([]string, error) {
	defer rows.Close()
	ids := []string{}
	for rows.Next() {
		var id string
		if err := rows.Scan(&id); err != nil {
			return nil, err
		}
		ids = append(ids, id)
	}
	return ids, rows.Err()
}

func dbTime(t time.Time) string        { return t.UTC().Format(time.RFC3339Nano) }
func parseTime(value string) time.Time { t, _ := time.Parse(time.RFC3339Nano, value); return t }
func parseOptionalTime(value sql.NullString) *time.Time {
	if !value.Valid {
		return nil
	}
	t := parseTime(value.String)
	return &t
}
func nullString(value string) any {
	if value == "" {
		return nil
	}
	return value
}

type legacyPlayer struct {
	ID, DeviceID, Name, Token string
	Wins, Losses              int
	CreatedAt                 time.Time
}
type legacyGameEvent struct {
	Version        int
	PlayerID, Kind string
	Payload        map[string]any
	At             time.Time
}
type legacyGame struct {
	ID, Name, Mode            string
	Status                    GameStatus
	HostID                    string
	PlayerIDs                 []string
	CurrentPlayerID, WinnerID string
	Version                   int
	Events                    []legacyGameEvent
	CreatedAt, UpdatedAt      time.Time
}
type legacyDatabase struct {
	Players map[string]*legacyPlayer `json:"players"`
	Games   map[string]*legacyGame   `json:"games"`
}

func moveLegacyJSON(path string) (*legacyDatabase, error) {
	b, err := os.ReadFile(path)
	if errors.Is(err, os.ErrNotExist) {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	trimmed := strings.TrimSpace(string(b))
	if !strings.HasPrefix(trimmed, "{") {
		return nil, nil
	}
	var legacy legacyDatabase
	if err = json.Unmarshal(b, &legacy); err != nil {
		return nil, err
	}
	backup := path + ".legacy.json"
	if _, statErr := os.Stat(backup); statErr == nil {
		backup = path + "." + time.Now().UTC().Format("20060102T150405") + ".legacy.json"
	}
	if err = os.Rename(path, backup); err != nil {
		return nil, err
	}
	return &legacy, nil
}

func (s *Store) importLegacy(legacy *legacyDatabase) error {
	tx, err := s.db.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()
	playerIDs := map[string]string{}
	for oldID, old := range legacy.Players {
		id := randomUUID()
		playerIDs[oldID] = id
		name := strings.TrimSpace(old.Name)
		if name == "" {
			name, _ = s.availableMythologyName(tx)
		}
		created := old.CreatedAt
		if created.IsZero() {
			created = time.Now().UTC()
		}
		if _, err = tx.Exec(`INSERT INTO players(id,name,timezone,profile_secret,wins,losses,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?)`, id, name, "UTC", randomID(24), old.Wins, old.Losses, dbTime(created), dbTime(created)); err != nil {
			return err
		}
		if old.DeviceID != "" {
			token := old.Token
			if token == "" {
				token = randomID(32)
			}
			if _, err = tx.Exec(`INSERT INTO devices(id,player_id,device_identifier,bearer_token,created_at,last_seen_at) VALUES(?,?,?,?,?,?)`, randomUUID(), id, old.DeviceID, token, dbTime(created), dbTime(created)); err != nil {
				return err
			}
		}
	}
	for _, old := range legacy.Games {
		id := randomUUID()
		created, updated := old.CreatedAt, old.UpdatedAt
		if created.IsZero() {
			created = time.Now().UTC()
		}
		if updated.IsZero() {
			updated = created
		}
		maxPlayers := len(old.PlayerIDs)
		if maxPlayers < 2 {
			maxPlayers = 2
		}
		if maxPlayers > 6 {
			maxPlayers = 6
		}
		host, current, winner := playerIDs[old.HostID], playerIDs[old.CurrentPlayerID], playerIDs[old.WinnerID]
		roundNumber, turnNumber := 0, 0
		if (old.Status == Active || old.Status == Paused) && current != "" {
			roundNumber, turnNumber = 1, 1
		}
		version := old.Version
		if version < 1 {
			version = 1
		}
		if _, err = tx.Exec(`INSERT INTO games(id,join_code,name,mode,status,host_player_id,current_player_id,winner_player_id,max_players,current_round,current_turn,version,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)`, id, strings.ToUpper(randomID(4)), old.Name, old.Mode, string(old.Status), nullString(host), nullString(current), nullString(winner), maxPlayers, roundNumber, turnNumber, version, dbTime(created), dbTime(updated)); err != nil {
			return err
		}
		for seat, oldPlayerID := range old.PlayerIDs {
			if playerID := playerIDs[oldPlayerID]; playerID != "" {
				if _, err = tx.Exec(`INSERT INTO game_players(game_id,player_id,seat,joined_at) VALUES(?,?,?,?)`, id, playerID, seat, dbTime(created)); err != nil {
					return err
				}
			}
		}
		if roundNumber == 1 {
			roundID, turnID := randomUUID(), randomUUID()
			if _, err = tx.Exec(`INSERT INTO game_rounds(id,game_id,round_number,status,starting_player_id,started_at) VALUES(?,?,1,'active',?,?)`, roundID, id, current, dbTime(updated)); err != nil {
				return err
			}
			if _, err = tx.Exec(`INSERT INTO game_turns(id,game_id,round_id,turn_number,turn_in_round,player_id,status,phase,version_start,started_at) VALUES(?,?,?,1,1,?,'active','reinforce',?,?)`, turnID, id, roundID, current, version, dbTime(updated)); err != nil {
				return err
			}
		}
		for _, event := range old.Events {
			at := event.At
			if at.IsZero() {
				at = updated
			}
			if err = insertEvent(tx, id, event.Version, playerIDs[event.PlayerID], "", "", event.Kind, event.Payload, at); err != nil {
				return err
			}
		}
	}
	return tx.Commit()
}
