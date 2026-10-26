# Cartarchy for the Amiga

A Workbench 1.3 client for Cartarchy: the same 42-territory world, rules and
game server as the iOS and Android apps, on any Amiga from a 68000 A500 up,
talking to the server through [FujiNet NIO](https://github.com/markjfisher/fujinet-nio)
(`fujinet-nio.device`).

- **Campaign vs computer**: you against one to five CPU commanders
  (cautious, balanced or aggressive), with the iOS app's rules and AI.
- **Server games**: pair with a Cartarchy server, then create, join, start,
  play, resume and resign games, and play against iOS and Android players.

It runs on a 640×200 hires screen in 16 colours, with a copper-graded sea
behind the map. Territories fill with their owner's colour and show army
badges; the land you pick shows checked in white, and the lands it can reach
pulse. One binary runs on Kickstart 1.3 to 3.x.

## Playing

| Action | Mouse | Keys |
| --- | --- | --- |
| Claim, place, reinforce, pick a land, attack, move | left-click a land or its badge | |
| Cancel a pick | right button | Esc |
| End attacks / end turn | END ATTACKS, END TURN | E or Space |
| Trade a card set | TRADE SET +n | T |
| Deal every land and army at random during setup | RANDOM SETUP | R |
| Back to the games list / menu | GAMES / MENU | G |
| Clear a text field | | Ctrl-X |

The rules are the iOS app's `GameStore` and `ClassicRules`. An attack is one
roll of up to three dice against up to two. A fortify moves all but one army
to a neighbouring land and ends the turn. Holding five cards forces a trade
before you can deploy.

### On a server

1. **PAIR WITH A SERVER**: enter the server's address, e.g.
   `https://cartarchy.diller.org` or `http://192.168.1.10:18080`, and the
   pairing code from its home page. The server names you after a deity; you
   can **RENAME** yourself there. The address, token and device ID are kept
   in FujiNet app keys (creator `0xCA27`), so pairing survives a reboot.
2. **PLAY ON THE SERVER** lists your games and every open lobby. Each row
   offers PLAY or RESUME, JOIN, START (the host, with at least two seats
   filled), or VIEW for a finished game. **NEW GAME** makes a lobby and shows
   its join code; **JOIN CODE** joins one from another player.
3. Whoever moves first deals the board. As in the mobile apps, each move
   sends the whole game state. Clicks made on your own turn are sent
   together, 1.5 s after the last one or as soon as the turn passes. While
   others play, the Amiga checks the server every 4 s, and the chime tells
   you it's your turn.

## Building

Needs [bebbo's m68k-amigaos-gcc](https://github.com/bebbo/amiga-gcc) on the
`PATH` and a [fujinet-nio-workspace](https://github.com/markjfisher/fujinet-nio-workspace)
checkout for fujinet-nio-lib (default: `../../fujinet-nio-workspace`).

```sh
cd amiga
make                                   # build/Cartarchy and Cartarchy.info
make NIO_WORKSPACE=/path/to/workspace
make test                              # rules, JSON and state checks, natively
```

The map is generated at build time from the iOS app's own
`ios/cartarchy/board_paths.json` and `Models.swift` by `support/mkmap.py`, so
all clients share one board and one adjacency table. The logo and icon art
(`art/*.png`) are drawn by `support/mklogo.py` from `artwork/`. That needs
Pillow and macOS's Copperplate font, so its output is committed:

```sh
uv run --with pillow support/mklogo.py ../artwork/cartarchy-icon-master-1024.png art
```

Copy `build/Cartarchy` and `Cartarchy.info` anywhere. The game looks for
`fujinet-nio.device` in `DEVS:`, `NIO:` or its own directory if it isn't
resident yet.

### Testing against a server

`tests/opponent.py` plays one seat of a server game with plain Python dicts.
It deals the board when nobody has, places at random and ends its turns. It
makes a quick second player, and checks that another implementation reads
the state the Amiga writes.

```sh
cd server && go run . -listen 0.0.0.0:18080 -pairing-code harness -data /tmp/c.sqlite
TOKEN=$(curl -s -X POST localhost:18080/v1/devices/register \
  -d '{"DeviceID":"bot-1","Name":"bot","PairingCode":"harness"}' | jq -r .token)
python3 amiga/tests/opponent.py http://127.0.0.1:18080 $TOKEN <game-id-or-join-code>
```

## Notes for porting

- **Request headers.** fujinet-nio-lib's `fn_open()` sends no HTTP request
  headers, and the API needs `Authorization: Bearer …` on every call. So
  `src/net.c` builds the NetworkDevice Open request itself (header pairs and
  a body length) and sends it with the library's internal `fn_open_exchange()`.
  A public `fn_open_headers()` in the library would remove that dependency.
- **Reads** are at most 512 bytes. The reply to a bigger read, headers and
  all, would not fit the library's 1024-byte packet buffer, and the device
  rejects it.
- **Link speed.** `fujinet-nio.device` talks to the FujiNet at 19200 baud by
  default, about 1.9 KB/s, and a game state is about 5 KB. So the client
  reads only as much of a reply as it needs: up to `version` after a move,
  and up to the end of `state` when opening a game, before the event
  history that keeps growing. Raise the baud with `fujinet-nio-baud` for
  snappier play on hardware that keeps up.
- **Chip RAM.** With 16 hires colours the display takes nearly every chip RAM
  cycle while the beam is on screen. The map is therefore redrawn only where
  owners, armies or highlights changed, and dialogs only redraw themselves.
  The Help key times the board drawing, for tuning on real hardware.
- **Memory.** About 210 KB of chip RAM (screen, back buffer, base map, logo,
  sounds) and 200 KB of any RAM. A 512 KB chip + 512 KB A500 is enough.

## Files

| File | |
| --- | --- |
| `src/main.c` | start-up, event loop, timer and keyboard |
| `src/gfx.c` | screen, copper sea, bevel panels, dirty-area map renderer, badges, dice |
| `src/ui.c` | menus, pairing, games list, new game, join and campaign dialogs |
| `src/play.c` | the game screen, campaign AI pacing, server sync and polling |
| `src/rules.c` | rules, moves and AI, after the iOS app's `GameStore` |
| `src/state.c` | the game state as the mobile apps' JSON (`SavedMatch`) |
| `src/json.c` | an in-place JSON reader that tolerates cut-short replies, and a writer |
| `src/api.c` | the server's `/v1` API and app-key preferences |
| `src/net.c` | NIO device loading and HTTP with request headers |
| `src/sound.c` | dice, capture, turn and click sounds, synthesised at start |
| `support/` | map, logo, icon and bitplane generators |
| `manual/` | the player's manual (`Cartarchy-Amiga-Manual.pdf`), rebuilt by `uv run --with reportlab --with pillow manual/mkmanual.py` from the screenshots in `manual/shots/` |
| `tests/` | host tests (`make test`) and the stand-in opponent |

## Credits

The territory outlines are adapted from "Risk board.svg" by Gr0gmint
(CC BY-SA 3.0), as in the iOS app; see `ios/cartarchy/MapAttribution.txt`.
The menu screen credits it too.
