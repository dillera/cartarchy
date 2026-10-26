package main

import "time"

type Player struct {
	ID            string    `json:"id"`
	Name          string    `json:"name"`
	Timezone      string    `json:"timezone"`
	ProfileSecret string    `json:"-"`
	Wins          int       `json:"wins"`
	Losses        int       `json:"losses"`
	Disabled      bool      `json:"disabled,omitempty"`
	CreatedAt     time.Time `json:"createdAt"`
	UpdatedAt     time.Time `json:"updatedAt"`
}

type Device struct {
	ID               string     `json:"id"`
	PlayerID         string     `json:"playerId"`
	DeviceIdentifier string     `json:"deviceIdentifier"`
	Token            string     `json:"-"`
	CreatedAt        time.Time  `json:"createdAt"`
	LastSeenAt       time.Time  `json:"lastSeenAt"`
	RevokedAt        *time.Time `json:"revokedAt,omitempty"`
}

type GameStatus string

const (
	Waiting  GameStatus = "waiting"
	Active   GameStatus = "active"
	Paused   GameStatus = "paused"
	Finished GameStatus = "finished"
)

type GameEvent struct {
	ID        int64          `json:"id"`
	Version   int            `json:"version"`
	PlayerID  string         `json:"playerId,omitempty"`
	RoundID   string         `json:"roundId,omitempty"`
	TurnID    string         `json:"turnId,omitempty"`
	Kind      string         `json:"kind"`
	Payload   map[string]any `json:"payload,omitempty"`
	CreatedAt time.Time      `json:"createdAt"`
}

type GameTurn struct {
	ID           string     `json:"id"`
	GameID       string     `json:"gameId"`
	RoundID      string     `json:"roundId"`
	TurnNumber   int        `json:"turnNumber"`
	TurnInRound  int        `json:"turnInRound"`
	PlayerID     string     `json:"playerId"`
	Status       string     `json:"status"`
	Phase        string     `json:"phase"`
	VersionStart int        `json:"versionStart"`
	VersionEnd   int        `json:"versionEnd,omitempty"`
	StartedAt    *time.Time `json:"startedAt,omitempty"`
	DeadlineAt   *time.Time `json:"deadlineAt,omitempty"`
	CompletedAt  *time.Time `json:"completedAt,omitempty"`
}

type GameRound struct {
	ID               string     `json:"id"`
	GameID           string     `json:"gameId"`
	RoundNumber      int        `json:"roundNumber"`
	Status           string     `json:"status"`
	StartingPlayerID string     `json:"startingPlayerId"`
	StartedAt        time.Time  `json:"startedAt"`
	CompletedAt      *time.Time `json:"completedAt,omitempty"`
	Turns            []GameTurn `json:"turns,omitempty"`
}

type Game struct {
	ID              string         `json:"id"`
	JoinCode        string         `json:"joinCode"`
	Name            string         `json:"name"`
	Mode            string         `json:"mode"`
	Status          GameStatus     `json:"status"`
	HostID          string         `json:"hostId,omitempty"`
	PlayerIDs       []string       `json:"playerIds"`
	Players         []GamePlayer   `json:"players"`
	CurrentPlayerID string         `json:"currentPlayerId,omitempty"`
	WinnerID        string         `json:"winnerId,omitempty"`
	MaxPlayers      int            `json:"maxPlayers"`
	CurrentRound    int            `json:"currentRound"`
	CurrentTurn     int            `json:"currentTurn"`
	Version         int            `json:"version"`
	State           map[string]any `json:"state,omitempty"`
	Events          []GameEvent    `json:"events,omitempty"`
	CreatedAt       time.Time      `json:"createdAt"`
	UpdatedAt       time.Time      `json:"updatedAt"`
}

type GamePlayer struct {
	ID   string `json:"id"`
	Name string `json:"name"`
}

type Dashboard struct {
	Leaderboard []Player `json:"leaderboard"`
	Games       []Game   `json:"games"`
}
