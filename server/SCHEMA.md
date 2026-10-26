# Cartarchy SQLite model

Cartarchy uses normalized SQLite tables so a paused or turn-based game can be resumed without reconstructing authoritative state from a mutable JSON document.

## Identity

- `players` owns the server-generated UUID, mythology name, timezone, profile capability secret, record, and enabled state.
- `devices` maps an app-provided stable device identifier to a player. Each device has its own revocable bearer token and last-seen timestamp.
- A new device creates exactly one player. Re-registering the same device retains the player identity and rotates the bearer token.

## Games and seats

- `games` is the current index record: mode, lifecycle status, host, winner, current player, round, turn, optimistic version, and timestamps.
- `game_players` is the ordered seat list. Seat order determines turn order and is stable for the life of the game.
- Resignation marks a seat `left` rather than deleting it, preserving its audit history while excluding it from subsequent turns and player game lists.
- A waiting game may have two to six seats. It becomes active when full, or when its host explicitly starts it with at least two players.

## Rounds and turns

- `game_rounds` records every round, its starting player, status, and start/completion timestamps.
- `game_turns` records both the global turn number and its one-based position within a round. It also records player, phase, lifecycle status, version bounds, optional deadline, and timestamps.
- There is at most one active round and one active turn per game. Application transactions enforce this invariant; unique `(game_id, round_number)`, `(game_id, turn_number)`, and `(round_id, turn_in_round)` constraints prevent duplicate positions.
- Completing a turn marks it completed at the new game version and creates the next active turn. Moving past the last occupied seat closes the current round and creates the next round before opening its first turn.
- Pausing changes the game lifecycle only. The active turn remains intact, so resume continues the same player, phase, and version.

## Events and resumable state

- `game_events` is the append-only audit stream. Each game version can have exactly one event. Events may reference the round and turn in which they occurred and carry a JSON payload.
- `game_state` stores the latest complete mobile-provided state snapshot and the version that produced it. Events explain how the game changed; the snapshot makes reconnect and resume constant-time.
- Event submission uses `ExpectedVersion`. Version checking, event insertion, turn/round advancement, game-index updates, and snapshot replacement occur in one SQLite transaction.
- A stale writer receives HTTP 409 and cannot partially update any table.

## Lifecycle and records

- `waiting → active → paused → active` models normal lobby and pause/resume operation.
- A finished game is immutable through the player API.
- Finishing records the winner, increments the winner's wins and every opponent's losses in the same transaction, and closes the active turn and round. The host may finish a game; in a turn-based game, the current player may also finish by declaring themself the winner after uploading the final state.
- Admin removal cascades dependent device and membership rows through foreign keys; game references to removed players are set to null.

## Operational settings

Every connection enables:

```sql
PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;
PRAGMA busy_timeout = 5000;
```

Schema changes are tracked in `schema_migrations`. Back up the database file and its WAL/SHM companions together, or use SQLite's online backup mechanism.
