#include <stdio.h>
#include <string.h>

#include <proto/dos.h>

#include "sound.h"
#include "ui.h"

/* A game on the board: against the computer, or a server game where every
   move is sent as the whole state, as the iOS and Android apps do. */

enum { P_ACTION = 1, P_LEAVE, P_RESIGN, P_RESUME };

static Match match;
static int remote, localIndex, hover = -1, confirmResign;
static GameInfo game;
static char seatIds[MAXPLAYERS][ID_LEN];
static int nseats;
static UWORD lastPoll, aiWait, pendingSince;
/* A move not yet sent: clicks on our own turn go to the server together */
static const char *pendingKind;
static Button pb[4];
static int npb;

#define ROWS_Y  (MAP_Y + 42)
#define INFO_Y  (MAP_Y + 104)
#define CARDS_Y (MAP_Y + 134)
#define BUTTONS_Y (MAP_Y + 144)
#define LANDS_R (PANEL_X + 104)

static int finished(void)
{
  return match.phase == PH_OVER || (remote && !strcmp(game.status, "finished"));
}

static int localTurn(void)
{
  if (finished())
    return 0;
  if (!remote)
    return match.players[match.current].kind == K_HUMAN;
  return !strcmp(game.status, "active") && match.current == localIndex;
}

static void upperName(char *out, const char *in)
{
  int i;

  for (i = 0; in[i] && i < NAME_LEN - 1; i++)
    out[i] = (char)(in[i] >= 'a' && in[i] <= 'z' ? in[i] - 32 : in[i]);
  out[i] = 0;
}

void playCampaign(int cpus, const UBYTE *kinds)
{
  char names[MAXPLAYERS][NAME_LEN];
  UBYTE all[MAXPLAYERS];
  int i;

  strcpy(names[0], "COMMANDER");
  all[0] = K_HUMAN;
  for (i = 0; i < cpus; i++) {
    sprintf(names[i + 1], "CPU %d", i + 1);
    all[i + 1] = kinds[i];
  }
  newMatch(&match, cpus + 1, (const char (*)[NAME_LEN])names, all);
  remote = 0;
  localIndex = 0;
  hover = -1;
  aiWait = frame;
  setStatus("", PEN_INK);
}

/* --- the server ------------------------------------------------------------ */

static void seatsFrom(const GameInfo *g)
{
  int i;

  nseats = g->nplayers;
  localIndex = -1;
  for (i = 0; i < nseats; i++) {
    strcpy(seatIds[i], g->playerIds[i]);
    if (!strcmp(seatIds[i], myId))
      localIndex = i;
  }
}

/* A newer server copy: take its state, and mark seats that resigned */
static void applyRemote(const GameInfo *g, const Match *m, int hasState)
{
  int i, k;

  game = *g;
  if (hasState)
    match = *m;
  if (nseats > g->nplayers) {
    for (i = 0; i < nseats && i < match.nplayers; i++) {
      for (k = 0; k < g->nplayers && strcmp(g->playerIds[k], seatIds[i]); k++)
        ;
      if (k == g->nplayers)
        match.players[i].alive = 0;
    }
    for (i = 0; i < nseats; i++)
      if (!strcmp(seatIds[i], g->current))
        match.current = (UBYTE)i;
  }
}

static void push(const char *kind)
{
  long newVersion;
  int r, hasState, changed;
  static Match fresh;
  static GameInfo g;

  setStatus("SENDING YOUR MOVE...", PEN_INK);
  r = apiPushEvent(game.id, game.version, kind,
                   match.current < nseats ? seatIds[match.current] : "", &match, &newVersion);
  if (r == 1) {
    game.version = newVersion;
    if (match.phase == PH_OVER && match.current == localIndex)
      apiFinish(game.id, myId);
    setStatus("", PEN_INK);
    return;
  }
  // Out of step with the server: take its copy back
  sfx(SFX_ERROR);
  if (apiFetchGame(game.id, -1, &g, &fresh, &hasState, &changed))
    applyRemote(&g, &fresh, hasState);
  setStatus(r < 0 ? "THE GAME MOVED ON: HERE IS THE LATEST." : apiError, PEN_PLAYER);
}

/* Send what is waiting; turn.completed outranks state.updated, since the
   server advances the turn on it */
static void flush(void)
{
  const char *kind = pendingKind;

  if (kind) {
    pendingKind = NULL;
    push(kind);
  }
}

static void queue(const char *kind)
{
  if (!pendingKind || strcmp(pendingKind, "turn.completed"))
    pendingKind = kind;
  pendingSince = frame;
  if (match.current != localIndex || match.phase == PH_OVER)
    flush();
}

int playRemote(const GameInfo *g)
{
  static Match fresh;
  static GameInfo info;
  static char names[MAXPLAYERS][NAME_LEN];
  int hasState, changed, i;

  setStatus("OPENING THE GAME...", PEN_INK);
  if (!strcmp(g->status, "paused") && !apiResume(g->id, &info))
    return 0;
  if (!apiFetchGame(g->id, -1, &info, &fresh, &hasState, &changed))
    return 0;
  remote = 1;
  hover = -1;
  selected = -1;
  pendingKind = NULL;
  game = info;
  seatsFrom(&info);
  if (hasState) {
    match = fresh;
  } else {
    UBYTE kinds[MAXPLAYERS];

    for (i = 0; i < info.nplayers; i++) {
      upperName(names[i], info.playerNames[i]);
      kinds[i] = K_HUMAN;
    }
    newMatch(&match, info.nplayers, (const char (*)[NAME_LEN])names, kinds);
    // The first player deals the board for everyone
    if (!strcmp(info.current, myId) && !strcmp(info.status, "active"))
      push("state.initialized");
  }
  lastPoll = frame;
  setStatus("", PEN_INK);
  return 1;
}

static void poll(void)
{
  static Match fresh;
  static GameInfo g;
  int hasState, changed, wasMine = localTurn();

  lastPoll = frame;
  if (!apiFetchGame(game.id, game.version, &g, &fresh, &hasState, &changed)) {
    setStatus(apiError, PEN_PLAYER);
    return;
  }
  if (!changed)
    return;
  applyRemote(&g, &fresh, hasState);
  // The board was dealt by nobody yet, and now it's our turn to
  if (!hasState && localTurn() && match.phase == PH_CLAIM)
    push("state.initialized");
  if (!wasMine && localTurn())
    sfx(SFX_TURN);
  playDraw();
}

/* --- drawing --------------------------------------------------------------- */

static void drawInfo(void)
{
  char line[40];
  int t = hover >= 0 ? hover : selected, y = INFO_Y;

  box(PANEL_X + 4, y, PANEL_R - 4, CARDS_Y - 2, PEN_FACE);
  if (t < 0) {
    text(PANEL_X + 8, y, localTurn() ? "YOUR MOVE:" : "", PEN_DARK);
    text(PANEL_X + 8, y + 10, localTurn() ? "PICK A LAND" : "", PEN_INK);
    return;
  }
  sprintf(line, "%.16s", territories[t].name);
  textBold(PANEL_X + 8, y, line, PEN_INK);
  sprintf(line, "%s +%d", continents[territories[t].continent].name,
          continents[territories[t].continent].bonus);
  text(PANEL_X + 8, y + 10, line, PEN_DARK);
  if (match.owner[t] >= 0) {
    swatch(PANEL_X + 8, y + 21, match.owner[t]);
    sprintf(line, "%.10s %ld", match.players[(int)match.owner[t]].name, (long)match.armies[t]);
    text(PANEL_X + 22, y + 20, line, PEN_INK);
  } else {
    text(PANEL_X + 8, y + 20, "UNCLAIMED", PEN_INK);
  }
}

static void drawPanel(void)
{
  static char tradeLabel[20];
  const Player *cur = &match.players[match.current];
  char line[40];
  int i, y, idx[3];

  panel(PANEL_X, MAP_Y, PANEL_R, BAR_Y - 1);
  swatch(PANEL_X + 8, MAP_Y + 6, match.current);
  sprintf(line, "%.12s", cur->name);
  textBold(PANEL_X + 22, MAP_Y + 5, line, PEN_INK);
  switch (match.phase) {
    case PH_CLAIM: sprintf(line, "CLAIM A LAND"); break;
    case PH_DEPLOY: sprintf(line, "PLACE ARMY  %d", match.setupArmies[match.current]); break;
    case PH_REINFORCE: sprintf(line, "REINFORCE   %d", match.reinforcements); break;
    default: strcpy(line, phaseNames[match.phase]); break;
  }
  text(PANEL_X + 8, MAP_Y + 17, line, PEN_INK);
  box(PANEL_X + 6, MAP_Y + 29, PANEL_R - 6, MAP_Y + 29, PEN_DARK);

  text(PANEL_X + 18, MAP_Y + 32, "WHO", PEN_DARK);
  textRight(LANDS_R, MAP_Y + 32, "LND", PEN_DARK);
  textRight(PANEL_R - 6, MAP_Y + 32, "ARM", PEN_DARK);
  for (i = 0, y = ROWS_Y; i < match.nplayers; i++, y += 9) {
    const Player *p = &match.players[i];
    int pen = p->alive ? PEN_INK : PEN_DARK;

    swatch(PANEL_X + 5, y + 1, i);
    sprintf(line, "%.8s", p->name);
    text(PANEL_X + 18, y, line, i == match.current ? PEN_WHITE : pen);
    if (p->alive) {
      sprintf(line, "%d", ownedCount(&match, i));
      textRight(LANDS_R, y, line, pen);
      sprintf(line, "%ld", armyCount(&match, i));
      textRight(PANEL_R - 6, y, line, pen);
    } else {
      textRight(PANEL_R - 6, y, "OUT", pen);
    }
  }
  box(PANEL_X + 6, INFO_Y - 4, PANEL_R - 6, INFO_Y - 4, PEN_DARK);
  drawInfo();

  if (localIndex >= 0 && localIndex < match.nplayers) {
    const Player *me = &match.players[localIndex];

    sprintf(line, "CARDS %d%s", me->ncards, findSet(me, idx) ? "  SET!" : "");
    text(PANEL_X + 8, CARDS_Y, line, PEN_INK);
  }

  npb = 0;
  pb[0].x = PANEL_X + 6;
  pb[0].y = BUTTONS_Y;
  pb[0].w = PANEL_R - PANEL_X - 11;
  pb[0].h = 13;
  pb[0].id = P_ACTION;
  pb[0].enabled = (UBYTE)localTurn();
  switch (match.phase) {
    case PH_CLAIM:
    case PH_DEPLOY:
      pb[0].label = "RANDOM SETUP";
      pb[0].key = 'r';
      break;
    case PH_REINFORCE:
      sprintf(tradeLabel, "TRADE SET +%d", tradeValue(match.tradeCount));
      pb[0].label = tradeLabel;
      pb[0].key = 't';
      pb[0].enabled = (UBYTE)(localTurn() && canTrade(&match));
      break;
    case PH_ATTACK:
      pb[0].label = "END ATTACKS";
      pb[0].key = 'e';
      break;
    case PH_FORTIFY:
      pb[0].label = "END TURN";
      pb[0].key = 'e';
      break;
    default:
      pb[0].label = remote ? "GAME OVER" : "NEW CAMPAIGN";
      pb[0].key = 'n';
      pb[0].enabled = !remote;
      break;
  }
  if (remote && !strcmp(game.status, "paused")) {
    pb[0].label = "RESUME GAME";
    pb[0].id = P_RESUME;
    pb[0].enabled = 1;
  }
  npb = 1;
  pb[1] = pb[0];
  pb[1].y = BUTTONS_Y + 15;
  pb[1].id = P_LEAVE;
  pb[1].key = 'g';
  pb[1].enabled = 1;
  pb[1].label = remote ? "GAMES" : "MENU";
  if (remote && !finished()) {
    pb[1].w = 60;
    pb[2] = pb[1];
    pb[2].x = (WORD)(PANEL_X + 70);
    pb[2].w = PANEL_R - PANEL_X - 75;
    pb[2].id = P_RESIGN;
    pb[2].key = 0;
    pb[2].label = confirmResign ? "SURE?" : "RESIGN";
    npb = 3;
  } else {
    npb = 2;
  }
  for (i = 0; i < npb; i++)
    buttonDraw(&pb[i], 0);
}

static void drawBottom(void)
{
  char right[40];
  int i;

  panel(0, BAR_Y, SCREEN_W - 1, SCREEN_H - 1);
  if (status[0]) {
    text(8, BAR_Y + 3, status, statusPen);
    return;
  }
  text(8, BAR_Y + 3, match.message, PEN_INK);
  // The last roll, while attacks go on: the attacker's red dice
  if (nDiceA && match.phase == PH_ATTACK && !strncmp(match.message, "Rolled", 6)) {
    for (i = 0; i < nDiceA; i++)
      die(372 + i * 20, BAR_Y + 2, diceA[i], 1);
    for (i = 0; i < nDiceD; i++)
      die(440 + i * 20, BAR_Y + 2, diceD[i], 0);
  }
  if (finished())
    strcpy(right, "GAME OVER");
  else if (localTurn())
    strcpy(right, "YOUR TURN");
  else if (remote)
    sprintf(right, "WAITING FOR %.12s", match.players[match.current].name);
  else
    sprintf(right, "%.12s IS THINKING", match.players[match.current].name);
  textRight(SCREEN_W - 10, BAR_Y + 3, right, localTurn() ? PEN_PLAYER : PEN_DARK);
}

void playDraw(void)
{
  char top[80];

  mapDraw(match.owner, match.armies, selected, localTurn() ? &match : NULL);
  if (match.turn == 0)
    sprintf(top, "SETUP     %s", remote ? game.name : "CAMPAIGN");
  else
    sprintf(top, "TURN %d    %s", match.turn, remote ? game.name : "CAMPAIGN");
  drawTop(top);
  drawPanel();
  drawBottom();
  mapPresent();
  present(0, 0, SCREEN_W - 1, MAP_Y - 1);
  present(PANEL_X, MAP_Y, PANEL_R, BAR_Y - 1);
  present(0, BAR_Y, SCREEN_W - 1, SCREEN_H - 1);
}

/* --- moves ----------------------------------------------------------------- */

static void after(const char *kind, int target, BYTE ownerBefore)
{
  if (!kind) {
    playDraw();
    return;
  }
  if (match.phase == PH_ATTACK || match.phase == PH_OVER) {
    if (target >= 0 && match.owner[target] != ownerBefore)
      sfx(SFX_CAPTURE);
    else if (target >= 0 && strncmp(match.message, "Rolled", 6) == 0)
      sfx(SFX_DICE);
  }
  status[0] = 0;
  if (remote)
    queue(kind);
  playDraw();
  aiWait = frame + 15;
}

static void leave(void)
{
  flush();
  selected = -1;
  confirmResign = 0;
  status[0] = 0;
  nDiceA = nDiceD = 0;
  if (remote) {
    uiShow(M_GAMES);
    uiKey('r');
  } else {
    uiShow(M_MENU);
  }
}

static void button(int id)
{
  const char *kind = NULL;
  static GameInfo g;

  if (id != P_RESIGN)
    confirmResign = 0;
  switch (id) {
    case P_ACTION:
      if (!localTurn() && !finished())
        return;
      if (match.phase == PH_OVER && !remote) {
        uiShow(M_CAMPAIGN);
        return;
      }
      if (match.phase == PH_CLAIM || match.phase == PH_DEPLOY)
        kind = actRandomize(&match);
      else if (match.phase == PH_REINFORCE)
        kind = actTrade(&match);
      else
        kind = actNextPhase(&match);
      nDiceA = nDiceD = 0;
      after(kind, -1, 0);
      return;
    case P_LEAVE:
      leave();
      return;
    case P_RESUME:
      setStatus("RESUMING...", PEN_INK);
      if (apiResume(game.id, &g))
        game = g;
      status[0] = 0;
      playDraw();
      return;
    case P_RESIGN:
      if (!confirmResign) {
        confirmResign = 1;
        playDraw();
        return;
      }
      pendingKind = NULL;
      setStatus("RESIGNING...", PEN_INK);
      if (!apiResign(game.id)) {
        setStatus(apiError, PEN_PLAYER);
        return;
      }
      leave();
      return;
  }
}

void playClick(int x, int y)
{
  int i = buttonAt(pb, npb, x, y), t;
  BYTE before;
  const char *kind;

  if (i >= 0) {
    pressButton(&pb[i]);
    button(pb[i].id);
    return;
  }
  if ((t = mapHit(x, y)) < 0)
    return;
  if (!localTurn()) {
    if (!finished())
      setStatus("NOT YOUR TURN YET.", PEN_DARK);
    return;
  }
  confirmResign = 0;
  before = match.owner[t];
  if (match.phase != PH_ATTACK)
    nDiceA = nDiceD = 0;
  kind = actTap(&match, t);
  hover = -1;
  after(kind, kind && match.phase >= PH_ATTACK ? t : -1, before);
}

void playRightClick(void)
{
  if (selected >= 0) {
    selected = -1;
    playDraw();
  }
}

/* Help: how long the board takes to draw, in fiftieths of a second */
static void benchmark(void)
{
  struct DateStamp a, b;
  char line[64];
  long map, all;
  int i;

  DateStamp(&a);
  for (i = 0; i < 10; i++)
    mapDraw(match.owner, match.armies, selected, localTurn() ? &match : NULL);
  DateStamp(&b);
  map = (b.ds_Minute - a.ds_Minute) * 3000L + b.ds_Tick - a.ds_Tick;
  DateStamp(&a);
  for (i = 0; i < 10; i++)
    playDraw();
  DateStamp(&b);
  all = (b.ds_Minute - a.ds_Minute) * 3000L + b.ds_Tick - a.ds_Tick;
  sprintf(line, "10 BOARDS %ld/50 S, 10 SCREENS %ld/50 S", map, all);
  setStatus(line, PEN_INK);
}

void playKey(int c)
{
  int i;

  if (c == KEY_HELP) {
    benchmark();
    return;
  }
  if (c == KEY_ESC) {
    if (selected >= 0)
      playRightClick();
    else
      button(P_LEAVE);
    return;
  }
  if (c == ' ')
    c = 'e';
  if (c >= 'A' && c <= 'Z')
    c += 'a' - 'A';
  for (i = 0; i < npb; i++)
    if (pb[i].key == c && pb[i].enabled) {
      pressButton(&pb[i]);
      button(pb[i].id);
      return;
    }
}

void playMove(int x, int y)
{
  int t = mapHit(x, y);

  if (t == hover)
    return;
  hover = t;
  drawInfo();
  present(PANEL_X, INFO_Y, PANEL_R, CARDS_Y - 2);
}

void playTick(void)
{
  if (remote) {
    UWORD wait = localTurn() ? 50 * 15 : 50 * 4;

    if (pendingKind) {
      if ((UWORD)(frame - pendingSince) > 75) {
        flush();
        playDraw();
      }
      return;
    }
    if (!finished() && (UWORD)(frame - lastPoll) > wait)
      poll();
    return;
  }
  if (finished() || localTurn() || (WORD)(frame - aiWait) < 0)
    return;
  aiStep(&match);
  aiWait = frame + (match.phase <= PH_DEPLOY ? 2 : match.phase == PH_ATTACK ? 12 : 30);
  if (localTurn())
    sfx(SFX_TURN);
  else if (match.phase == PH_OVER)
    sfx(SFX_CAPTURE);
  playDraw();
}
