package main

import (
	"crypto/subtle"
	"fmt"
	"html/template"
	"net/http"
	"net/url"
	"strconv"
	"strings"
)

const pageStyle = `
*{box-sizing:border-box}body{font:15px ui-monospace,SFMono-Regular,Menlo,monospace;max-width:1100px;margin:0 auto;padding:24px;background:#e9e9e9;color:#111}
a{color:#111}header,section,.panel{background:#fff;border:3px solid #111;padding:20px;margin:0 0 22px;box-shadow:7px 7px #111}h1,h2{margin-top:0;text-transform:uppercase}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:18px}.compact{padding:12px;border:2px solid;margin:10px 0}.muted{color:#555}.status{text-transform:uppercase;font-weight:bold}
table{width:100%;border-collapse:collapse;background:#fff}th,td{text-align:left;border:1px solid #777;padding:8px;vertical-align:top}th{background:#ddd}form.inline{display:inline}button,.button,input,select{font:inherit;border:2px solid #111;background:#fff;color:#111;padding:8px;margin:3px 0}button,.button{font-weight:bold;cursor:pointer;text-decoration:none;display:inline-block}button:hover,.button:hover{background:#111;color:#fff}
label{display:block;font-weight:bold;margin-top:10px}input,select{width:100%}img.qr{width:min(280px,100%);image-rendering:pixelated}.actions{display:flex;gap:6px;flex-wrap:wrap}.danger{border-style:dashed}code{word-break:break-all}@media(max-width:600px){body{padding:12px}header,section,.panel{padding:14px;box-shadow:4px 4px #111}table{font-size:12px}th,td{padding:5px}}
`

var homeTemplate = template.Must(template.New("home").Parse(`<!doctype html><html><head><meta name=viewport content="width=device-width"><title>Cartarchy Server</title><style>` + pageStyle + `</style></head><body>
<header><h1>Cartarchy SERVER</h1><p>Local real-time and turn-based campaigns.</p><div class=actions><a class=button href=/admin>ADMIN</a></div></header>
<div class=grid><section><h2>Connect a device</h2><p>You should already have the Cartarchy Mobile app installed on your device.</p><img class=qr src=/pairing.png alt="Server pairing QR"><p>Server: <code>{{.URL}}</code></p><p>Pairing code: <b>{{.Code}}</b></p></section>
<section><h2>Start a game</h2><form method=post action=/games><label>Campaign name</label><input name=name maxlength=80 required value="New Campaign"><label>Mode</label><select name=mode><option value=turn-based>Turn based</option><option value=realtime>Real time</option></select><label>Players</label><select name=maxPlayers>{{range $n := .PlayerCounts}}<option value={{$n}}>{{$n}}</option>{{end}}</select><p><button type=submit>CREATE GAME + QR</button></p></form></section></div>
<section><h2>Leaderboard</h2>{{if .Dashboard.Leaderboard}}<table><thead><tr><th>Commander</th><th>Wins</th><th>Losses</th><th>Timezone</th></tr></thead><tbody>{{range .Dashboard.Leaderboard}}<tr><td>{{.Name}}</td><td>{{.Wins}}</td><td>{{.Losses}}</td><td>{{.Timezone}}</td></tr>{{end}}</tbody></table>{{else}}<p class=muted>No registered commanders yet.</p>{{end}}</section>
<section><h2>Games in progress</h2>{{if .Dashboard.Games}}<div class=grid>{{range .Dashboard.Games}}<article class=compact><h3><a href="/games/{{.ID}}">{{.Name}}</a></h3><p><span class=status>{{.Status}}</span> · {{.Mode}} · {{len .PlayerIDs}}/{{.MaxPlayers}} players</p><p>Round {{.CurrentRound}} · Turn {{.CurrentTurn}}</p><small><code>{{.ID}}</code></small></article>{{end}}</div>{{else}}<p class=muted>No waiting or active games.</p>{{end}}</section>
</body></html>`))

var gameTemplate = template.Must(template.New("game").Parse(`<!doctype html><html><head><meta name=viewport content="width=device-width"><title>{{.Game.Name}} · Cartarchy</title><style>` + pageStyle + `</style></head><body>
<header><p><a href=/>← SERVER HOME</a></p><h1>{{.Game.Name}}</h1><p><span class=status>{{.Game.Status}}</span> · {{.Game.Mode}} · {{len .Game.PlayerIDs}}/{{.Game.MaxPlayers}} players</p></header>
<div class=grid><section><h2>Join this game</h2><p>Share or scan this QR in Cartarchy.</p><img class=qr src="/games/{{.Game.ID}}/qr.png" alt="Game join QR"><p>Join code: <b>{{.Game.JoinCode}}</b></p><p><a class=button href="{{.JoinURI}}">OPEN IN CARTARCHY</a></p></section>
<section><h2>Joined commanders</h2>{{if .Game.Players}}<ol>{{range .Game.Players}}<li><b>{{.Name}}</b></li>{{end}}</ol>{{else}}<p class=muted>No commanders have joined yet.</p>{{end}}<p class=muted>Waiting games begin automatically when all seats fill. The host can also start after two players join.</p></section>
<section><h2>Game record</h2><p>ID: <code>{{.Game.ID}}</code></p><p>Version: {{.Game.Version}}</p><p>Round: {{.Game.CurrentRound}}</p><p>Turn: {{.Game.CurrentTurn}}</p><p>Created: {{.Game.CreatedAt}}</p></section></div>
</body></html>`))

var profileTemplate = template.Must(template.New("profile").Parse(`<!doctype html><html><head><meta name=viewport content="width=device-width"><title>{{.Player.Name}} · Cartarchy</title><style>` + pageStyle + `</style></head><body>
<header><p><a href=/>← SERVER HOME</a></p><h1>Commander profile</h1><p>ID: <code>{{.Player.ID}}</code></p></header>
<div class=grid><section><h2>{{.Player.Name}}</h2><p>Wins: <b>{{.Player.Wins}}</b> · Losses: <b>{{.Player.Losses}}</b></p><p>Timezone: {{.Player.Timezone}}</p><img class=qr src="/profiles/{{.Player.ID}}/qr.png?key={{.Key}}" alt="Profile QR"><p class=muted>Save this QR. It is the private key back to this profile.</p></section>
<section><h2>Edit profile</h2>{{if .Message}}<p><b>{{.Message}}</b></p>{{end}}<form method=post><input type=hidden name=key value="{{.Key}}"><label>Commander name</label><input name=name maxlength=40 required value="{{.Player.Name}}"><label>IANA timezone</label><input name=timezone maxlength=64 required value="{{.Player.Timezone}}" placeholder="America/New_York"><p><button type=submit>SAVE PROFILE</button></p></form></section></div>
</body></html>`))

type adminPlayerView struct {
	Player
	ProfileURL string
}

type adminView struct {
	Players    []adminPlayerView
	Devices    []Device
	Games      []Game
	PlayerByID map[string]string
}

var adminTemplate = template.Must(template.New("admin").Parse(`<!doctype html><html><head><meta name=viewport content="width=device-width"><title>Admin · Cartarchy</title><style>` + pageStyle + `</style></head><body>
<header><p><a href=/>← SERVER HOME</a></p><h1>Cartarchy Admin</h1><p>Manage commanders, registered devices, and every campaign.</p></header>
<section><h2>Games ({{len .Games}})</h2>{{if .Games}}<table><thead><tr><th>Game</th><th>State</th><th>Players</th><th>Round / Turn</th><th>Actions</th></tr></thead><tbody>{{range .Games}}<tr><td><a href="/games/{{.ID}}">{{.Name}}</a><br><small><code>{{.ID}}</code></small></td><td>{{.Status}}<br>{{.Mode}} · v{{.Version}}</td><td>{{len .PlayerIDs}}/{{.MaxPlayers}}</td><td>{{.CurrentRound}} / {{.CurrentTurn}}</td><td><div class=actions>{{if eq .Status "active"}}<form class=inline method=post action="/admin/games/{{.ID}}/pause"><button>PAUSE</button></form>{{end}}{{if eq .Status "paused"}}<form class=inline method=post action="/admin/games/{{.ID}}/resume"><button>RESUME</button></form>{{end}}{{if ne .Status "finished"}}<form class=inline method=post action="/admin/games/{{.ID}}/close"><button>CLOSE</button></form>{{end}}<form class=inline method=post action="/admin/games/{{.ID}}/delete"><button class=danger>DELETE</button></form></div></td></tr>{{end}}</tbody></table>{{else}}<p>No games.</p>{{end}}</section>
<section><h2>Players ({{len .Players}})</h2>{{if .Players}}<table><thead><tr><th>Commander</th><th>Record</th><th>State</th><th>Actions</th></tr></thead><tbody>{{range .Players}}<tr><td>{{.Name}}<br><small><code>{{.ID}}</code></small><br>{{.Timezone}}</td><td>{{.Wins}}W / {{.Losses}}L</td><td>{{if .Disabled}}DISABLED{{else}}ACTIVE{{end}}</td><td><div class=actions><a class=button href="{{.ProfileURL}}">PROFILE</a>{{if .Disabled}}<form class=inline method=post action="/admin/players/{{.ID}}/enable"><button>ENABLE</button></form>{{else}}<form class=inline method=post action="/admin/players/{{.ID}}/disable"><button>DISABLE</button></form>{{end}}<form class=inline method=post action="/admin/players/{{.ID}}/delete"><button class=danger>DELETE</button></form></div></td></tr>{{end}}</tbody></table>{{else}}<p>No players.</p>{{end}}</section>
<section><h2>Devices ({{len .Devices}})</h2>{{if .Devices}}<table><thead><tr><th>Device</th><th>Player</th><th>Last seen</th><th>Actions</th></tr></thead><tbody>{{range .Devices}}<tr><td><code>{{.DeviceIdentifier}}</code><br><small>{{.ID}}</small></td><td>{{index $.PlayerByID .PlayerID}}</td><td>{{.LastSeenAt}}<br>{{if .RevokedAt}}REVOKED{{else}}ACTIVE{{end}}</td><td><div class=actions>{{if .RevokedAt}}<form class=inline method=post action="/admin/devices/{{.ID}}/restore"><button>RESTORE</button></form>{{else}}<form class=inline method=post action="/admin/devices/{{.ID}}/revoke"><button>REVOKE</button></form>{{end}}<form class=inline method=post action="/admin/devices/{{.ID}}/delete"><button class=danger>DELETE</button></form></div></td></tr>{{end}}</tbody></table>{{else}}<p>No devices.</p>{{end}}</section>
</body></html>`))

func (s *Server) home(w http.ResponseWriter, r *http.Request) {
	dashboard, err := s.store.Dashboard()
	if err != nil {
		http.Error(w, "dashboard unavailable", http.StatusInternalServerError)
		return
	}
	data := struct {
		URL, Code    string
		PlayerCounts []int
		Dashboard    Dashboard
	}{s.baseURL(r), s.pairingCode, []int{2, 3, 4, 5, 6}, dashboard}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	_ = homeTemplate.Execute(w, data)
}

func (s *Server) createGamePage(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		http.Error(w, "invalid form", http.StatusBadRequest)
		return
	}
	maxPlayers, _ := strconv.Atoi(r.FormValue("maxPlayers"))
	game, err := s.store.CreateGame("", r.FormValue("name"), r.FormValue("mode"), maxPlayers)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	s.publish("game.created", &game)
	http.Redirect(w, r, "/games/"+url.PathEscape(game.ID), http.StatusSeeOther)
}

func (s *Server) gamePage(w http.ResponseWriter, r *http.Request) {
	game, err := s.store.GetGame(r.PathValue("id"), false)
	if err != nil {
		http.NotFound(w, r)
		return
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	_ = gameTemplate.Execute(w, map[string]any{"Game": game, "JoinURI": s.gameJoinURI(r, game)})
}

func (s *Server) gameQR(w http.ResponseWriter, r *http.Request) {
	game, err := s.store.GetGame(r.PathValue("id"), false)
	if err != nil {
		http.NotFound(w, r)
		return
	}
	s.writeQR(w, s.gameJoinURI(r, game))
}

func (s *Server) authorizedProfile(r *http.Request) (Player, bool) {
	player, err := s.store.Player(r.PathValue("id"))
	if err != nil || player.Disabled {
		return Player{}, false
	}
	key := r.URL.Query().Get("key")
	if r.Method == http.MethodPost {
		key = r.FormValue("key")
	}
	return player, subtle.ConstantTimeCompare([]byte(key), []byte(player.ProfileSecret)) == 1
}

func (s *Server) profilePage(w http.ResponseWriter, r *http.Request) {
	player, ok := s.authorizedProfile(r)
	if !ok {
		http.NotFound(w, r)
		return
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	_ = profileTemplate.Execute(w, map[string]any{"Player": player, "Key": r.URL.Query().Get("key"), "Message": r.URL.Query().Get("message")})
}

func (s *Server) updateProfilePage(w http.ResponseWriter, r *http.Request) {
	if err := r.ParseForm(); err != nil {
		http.Error(w, "invalid form", http.StatusBadRequest)
		return
	}
	player, ok := s.authorizedProfile(r)
	if !ok {
		http.NotFound(w, r)
		return
	}
	if _, err := s.store.UpdateProfile(player.ID, r.FormValue("name"), r.FormValue("timezone")); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	location := fmt.Sprintf("/profiles/%s?key=%s&message=%s", url.PathEscape(player.ID), url.QueryEscape(player.ProfileSecret), url.QueryEscape("Profile saved"))
	http.Redirect(w, r, location, http.StatusSeeOther)
}

func (s *Server) profileQR(w http.ResponseWriter, r *http.Request) {
	player, ok := s.authorizedProfile(r)
	if !ok {
		http.NotFound(w, r)
		return
	}
	s.writeQR(w, s.profileURL(r, player))
}

func (s *Server) adminPage(w http.ResponseWriter, r *http.Request) {
	players, playerErr := s.store.ListPlayers(true)
	devices, deviceErr := s.store.ListDevices()
	games, gameErr := s.store.ListAllGames()
	if playerErr != nil || deviceErr != nil || gameErr != nil {
		http.Error(w, "admin data unavailable", http.StatusInternalServerError)
		return
	}
	view := adminView{Devices: devices, Games: games, PlayerByID: map[string]string{}}
	for _, player := range players {
		view.Players = append(view.Players, adminPlayerView{Player: player, ProfileURL: s.profileURL(r, player)})
		view.PlayerByID[player.ID] = player.Name
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	_ = adminTemplate.Execute(w, view)
}

func (s *Server) adminGameAction(w http.ResponseWriter, r *http.Request) {
	if err := s.store.AdminGameAction(r.PathValue("id"), strings.ToLower(r.PathValue("action"))); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	http.Redirect(w, r, "/admin", http.StatusSeeOther)
}

func (s *Server) adminPlayerAction(w http.ResponseWriter, r *http.Request) {
	if err := s.store.AdminPlayerAction(r.PathValue("id"), strings.ToLower(r.PathValue("action"))); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	http.Redirect(w, r, "/admin", http.StatusSeeOther)
}

func (s *Server) adminDeviceAction(w http.ResponseWriter, r *http.Request) {
	if err := s.store.AdminDeviceAction(r.PathValue("id"), strings.ToLower(r.PathValue("action"))); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	http.Redirect(w, r, "/admin", http.StatusSeeOther)
}
