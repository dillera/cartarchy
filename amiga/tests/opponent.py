#!/usr/bin/env python3
"""A stand-in Cartarchy player for testing a client against the server.

Plays one seat with plain dicts, sharing no code with the clients: deals the
board if nobody has, claims and places at random, piles its reinforcements
on one land, never attacks, and ends its turn. Polls every two seconds and
stops when the game ends.

Get a token by pairing a device, e.g. with curl:

    curl -s -X POST $SERVER/v1/devices/register \
      -d '{"DeviceID":"bot-1","Name":"bot","PairingCode":"harness"}'

Usage: opponent.py <server> <token> <game-id-or-join-code>
"""
import json
import os
import random
import re
import sys
import time
import urllib.error
import urllib.request

TERRITORIES = None  # (id, continent) in map order, from the iOS Models.swift
CONTINENT_BONUS = {"northAmerica": 5, "southAmerica": 2, "europe": 5, "africa": 3, "asia": 7, "australia": 2}


def load_map(path):
    src = open(path).read()
    rows = re.findall(r't\("(\w+)","[^"]+",\.(\w+),', src)
    return rows


def call(server, token, method, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(server + path, data=data, method=method,
                                 headers={"Authorization": "Bearer " + token, "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req) as r:
            return r.status, json.load(r)
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"{}")


def initial(players):
    n = len(players)
    start = {2: 40, 3: 35, 4: 30, 5: 25}.get(n, 20)
    units = ["infantry", "cavalry", "artillery"]
    deck = [{"id": t, "territoryID": t, "unit": units[i % 3]} for i, (t, _) in enumerate(TERRITORIES)]
    deck += [{"id": "wild-1"}, {"id": "wild-2"}]
    random.shuffle(deck)
    return {
        "players": [{"id": i, "name": p["name"].upper(), "kind": "Human", "isAlive": True, "cards": []}
                    for i, p in enumerate(players)],
        "territories": {t: {"owner": -1, "armies": 0} for t, _ in TERRITORIES},
        "currentPlayer": 0, "phase": "CLAIM TERRITORIES", "reinforcements": 0, "turn": 0,
        "message": "Claim one unoccupied territory.", "deck": deck, "discard": [], "tradeCount": 0,
        "conqueredThisTurn": False, "setupArmies": [start] * n,
    }


def reinforcements(s, owner):
    owned = [t for t, v in s["territories"].items() if v["owner"] == owner]
    total = max(3, len(owned) // 3)
    for cont, bonus in CONTINENT_BONUS.items():
        if all(s["territories"][t]["owner"] == owner for t, c in TERRITORIES if c == cont):
            total += bonus
    return total


def advance_setup(s):
    n = len(s["players"])
    if s["phase"] == "CLAIM TERRITORIES" and all(v["owner"] >= 0 for v in s["territories"].values()):
        s["phase"] = "PLACE ARMIES"
    if s["phase"] == "PLACE ARMIES" and all(a == 0 for a in s["setupArmies"]):
        s.update(currentPlayer=0, phase="REINFORCE", turn=1, reinforcements=reinforcements(s, 0),
                 message="Setup complete. Deploy reinforcements.")
        return
    while True:
        s["currentPlayer"] = (s["currentPlayer"] + 1) % n
        if not (s["phase"] == "PLACE ARMIES" and s["setupArmies"][s["currentPlayer"]] == 0):
            break
    p = s["players"][s["currentPlayer"]]["name"]
    s["message"] = (f"{p} claims a territory." if s["phase"] == "CLAIM TERRITORIES"
                    else f"{p} places an army. {s['setupArmies'][s['currentPlayer']]} remain.")


def end_turn(s):
    n = len(s["players"])
    while True:
        s["currentPlayer"] = (s["currentPlayer"] + 1) % n
        if s["players"][s["currentPlayer"]]["isAlive"]:
            break
    if s["currentPlayer"] == 0:
        s["turn"] += 1
    s.update(phase="REINFORCE", reinforcements=reinforcements(s, s["currentPlayer"]), conqueredThisTurn=False,
             message=f"{s['players'][s['currentPlayer']]['name']} reinforces.")


def move(s, me):
    """One move for seat me; returns the event kind."""
    terr = s["territories"]
    if s["phase"] == "CLAIM TERRITORIES":
        t = random.choice([t for t, v in terr.items() if v["owner"] == -1])
        terr[t] = {"owner": me, "armies": 1}
        s["setupArmies"][me] -= 1
        advance_setup(s)
    elif s["phase"] == "PLACE ARMIES":
        t = random.choice([t for t, v in terr.items() if v["owner"] == me])
        terr[t]["armies"] += 1
        s["setupArmies"][me] -= 1
        advance_setup(s)
    elif s["phase"] == "REINFORCE":
        t = max((t for t, v in terr.items() if v["owner"] == me), key=lambda t: terr[t]["armies"])
        terr[t]["armies"] += s["reinforcements"]
        s.update(reinforcements=0, phase="ATTACK", message="Reinforcement complete. Begin attacks.")
    elif s["phase"] == "ATTACK":
        s.update(phase="FORTIFY", message="Move one army, or end your turn.")
    elif s["phase"] == "FORTIFY":
        end_turn(s)
        return "turn.completed"
    return "state.updated"


def main():
    global TERRITORIES
    server, token, game_id = sys.argv[1:4]
    here = os.path.dirname(os.path.abspath(__file__))
    TERRITORIES = load_map(os.path.join(here, "..", "..", "ios", "cartarchy", "Models.swift"))
    status, me_info = call(server, token, "GET", "/v1/stats")
    me_id = me_info["id"]
    while True:
        status, g = call(server, token, "GET", f"/v1/games/{game_id}")
        if status != 200 or g["status"] == "finished":
            print("game", status, g.get("status"))
            return
        if g["status"] == "active" and g.get("currentPlayerId") == me_id:
            s = g.get("state")
            kind = "state.updated"
            if s is None:
                s, kind = initial(g["players"]), "state.initialized"
            else:
                me = g["playerIds"].index(me_id)
                if s["currentPlayer"] != me:
                    time.sleep(2)
                    continue
                kind = move(s, me)
            nxt = g["playerIds"][s["currentPlayer"]]
            # A join code finds the game, but moves go to its id
            status, r = call(server, token, "POST", f"/v1/games/{g['id']}/events",
                             {"ExpectedVersion": g["version"], "Kind": kind,
                              "Payload": {"currentPlayerId": nxt}, "State": s})
            print(f"v{g['version']} {kind} {s['phase']} -> {status} {s['message']}", flush=True)
            continue
        time.sleep(2)


if __name__ == "__main__":
    main()
