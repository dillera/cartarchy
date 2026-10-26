#include <stdio.h>
#include <string.h>

#include <proto/dos.h>
#include <proto/graphics.h>

#include "net.h"
#include "sound.h"
#include "ui.h"

int mode = M_MENU, quitting;
UWORD frame;
char status[96];
int statusPen = PEN_INK;

enum {
  B_SERVER = 1, B_CAMPAIGN, B_SETUP, B_QUIT, B_PAIR, B_RENAME, B_BACK, B_NEWGAME,
  B_JOINCODE, B_REFRESH, B_PREV, B_NEXT, B_MINUS, B_PLUS, B_MODE, B_CREATE, B_JOIN,
  B_BEGIN, B_ROW = 40, B_KIND = 60,
};

static Button buttons[20];
static int nbuttons;

typedef struct {
  char *buf;
  int max, x, y, w;
  const char *label;
} Field;

static Field fields[3];
static int nfields, activeField = -1;

static char fServer[128] = "https://cartarchy.diller.org", fCode[24], fName[NAME_LEN];
static char fGameName[40] = "Amiga Campaign", fJoin[16];
static int newPlayers = 2, newRealtime, cpuCount = 3;
static UBYTE cpuKinds[5] = { K_CAUTIOUS, K_BALANCED, K_AGGRESSIVE, K_CAUTIOUS, K_BALANCED };
static const char *const kindLabels[] = { "HUMAN", "CAUTIOUS", "BALANCED", "AGGRESSIVE" };

#define ROWS 8
static GameInfo games[24];
static int ngames, gamesPage;
static UWORD gamesStamp;

static BYTE attract[NTERR];

/* Each screen's dialog over the board; its shadow falls 4 right, 3 down */
static const WORD dialogs[][4] = {
  { 88, 16, 407, 176 },        /* M_MENU */
  { 24, 26, 471, 170 },        /* M_SETUP */
  { 8, 16, 487, 180 },         /* M_GAMES */
  { 80, 40, 415, 160 },        /* M_NEWGAME */
  { 120, 60, 375, 140 },       /* M_JOIN */
  { 96, 24, 399, 172 },        /* M_CAMPAIGN */
};
#define SHADOW_X 4
#define SHADOW_Y 3

/* --- the frame ------------------------------------------------------------ */

void drawTop(const char *top)
{
  box(0, 0, SCREEN_W - 1, MAP_Y - 1, PEN_INK);
  textBold(6, 2, "CARTARCHY", PEN_WHITE);
  if (top)
    text(96, 2, top, PEN_LIGHT);
}

void drawBars(const char *top)
{
  drawTop(top);
  box(MAP_X + MAP_W, MAP_Y, PANEL_X - 1, BAR_Y - 1, PEN_INK);
  box(0, MAP_Y + MAP_H, PANEL_X - 1, BAR_Y - 1, PEN_INK);
  panel(0, BAR_Y, SCREEN_W - 1, SCREEN_H - 1);
  text(8, BAR_Y + 3, status, statusPen);
}

void setStatus(const char *s, int pen)
{
  strncpy(status, s, sizeof status - 1);
  statusPen = pen;
  panel(0, BAR_Y, SCREEN_W - 1, SCREEN_H - 1);
  text(8, BAR_Y + 3, status, statusPen);
  present(0, BAR_Y, SCREEN_W - 1, SCREEN_H - 1);
}

static void addButton(int x, int y, int w, int id, int key, const char *label, int enabled)
{
  Button *b = &buttons[nbuttons++];

  b->x = (WORD)x;
  b->y = (WORD)y;
  b->w = (WORD)w;
  b->h = 13;
  b->id = (UBYTE)id;
  b->key = (UBYTE)key;
  b->label = label;
  b->enabled = (UBYTE)enabled;
}

static void drawButtons(void)
{
  int i;

  for (i = 0; i < nbuttons; i++)
    buttonDraw(&buttons[i], 0);
}

void pressButton(const Button *b)
{
  sfx(SFX_CLICK);
  buttonDraw(b, 1);
  present(b->x, b->y, b->x + b->w - 1, b->y + b->h - 1);
  Delay(4);
  buttonDraw(b, 0);
  present(b->x, b->y, b->x + b->w - 1, b->y + b->h - 1);
}

static void addField(int x, int y, int w, char *buf, int max, const char *label)
{
  Field *f = &fields[nfields++];

  f->x = x;
  f->y = y;
  f->w = w;
  f->buf = buf;
  f->max = max;
  f->label = label;
}

static void drawField(int i)
{
  const Field *f = &fields[i];
  int cols = (f->w - 8) / 8, len = (int)strlen(f->buf);
  const char *shown = f->buf + (len >= cols ? len - cols + 1 : 0);

  text(f->x, f->y - 10, f->label, PEN_INK);
  box(f->x, f->y, f->x + f->w - 1, f->y + 11, PEN_WHITE);
  bevel(f->x, f->y, f->x + f->w - 1, f->y + 11, 0);
  text(f->x + 4, f->y + 2, shown, PEN_INK);
  if (i == activeField) {
    int cx = f->x + 4 + (int)strlen(shown) * 8;

    box(cx, f->y + 2, cx + 7, f->y + 9, PEN_PLAYER);
  }
}

static void redrawField(int i)
{
  const Field *f = &fields[i];

  drawField(i);
  present(f->x, f->y, f->x + f->w - 1, f->y + 11);
}

static int clearOfDialog(int t)
{
  const WORD *d = dialogs[mode];
  int b[4];

  mapBounds(t, b);
  return b[2] < d[0] || b[0] > d[2] + SHADOW_X || b[3] < d[1] || b[1] > d[3] + SHADOW_Y;
}

/* The world behind the menus: colours spread like empires, where the
   dialog leaves the board in view */
static void attractStep(void)
{
  int tries, t, n;

  for (tries = 0; tries < 20; tries++) {
    t = rnd(NTERR);
    n = territories[t].neighbors[rnd(territories[t].nneighbors)];
    if (clearOfDialog(n) && attract[n] != attract[t]) {
      attract[n] = attract[t];
      mapDraw(attract, NULL, -1, NULL);
      mapPresent();
      return;
    }
  }
}

void drawSidePanel(void)
{
  char line[24];

  panel(PANEL_X, MAP_Y, PANEL_R, BAR_Y - 1);
  textShadow(PANEL_X + 8, MAP_Y + 6, "AMIGA EDITION", PEN_WHITE);
  text(PANEL_X + 8, MAP_Y + 20, "WORKBENCH 1.3+", PEN_INK);
  text(PANEL_X + 8, MAP_Y + 30, "VIA FUJINET NIO", PEN_INK);
  box(PANEL_X + 6, MAP_Y + 44, PANEL_R - 6, MAP_Y + 44, PEN_DARK);
  text(PANEL_X + 8, MAP_Y + 50, "SERVER", PEN_DARK);
  if (apiConfigured()) {
    const char *host = strstr(prefs.server, "://");

    host = host ? host + 3 : prefs.server;
    sprintf(line, "%.16s", host);
    text(PANEL_X + 8, MAP_Y + 60, line, PEN_INK);
  } else {
    text(PANEL_X + 8, MAP_Y + 60, "NOT PAIRED", PEN_INK);
  }
  text(PANEL_X + 8, MAP_Y + 74, "COMMANDER", PEN_DARK);
  sprintf(line, "%.16s", myName[0] ? myName : "-");
  text(PANEL_X + 8, MAP_Y + 84, line, PEN_INK);
  box(PANEL_X + 6, MAP_Y + 98, PANEL_R - 6, MAP_Y + 98, PEN_DARK);
  text(PANEL_X + 8, MAP_Y + 104, "LEFT BUTTON:", PEN_DARK);
  text(PANEL_X + 8, MAP_Y + 114, " PICK A LAND", PEN_INK);
  text(PANEL_X + 8, MAP_Y + 126, "RIGHT BUTTON:", PEN_DARK);
  text(PANEL_X + 8, MAP_Y + 136, " CANCEL", PEN_INK);
  text(PANEL_X + 8, MAP_Y + 152, "MAP: RISK BOARD", PEN_DARK);
  text(PANEL_X + 8, MAP_Y + 161, "BY GR0GMINT", PEN_DARK);
}

static void dialog(const char *title)
{
  const WORD *d = dialogs[mode];

  box(d[0] + SHADOW_X, d[1] + SHADOW_Y, d[2] + SHADOW_X, d[3] + SHADOW_Y, PEN_INK);
  panel(d[0], d[1], d[2], d[3]);
  if (title)
    textShadow((d[0] + d[2]) / 2 - (int)strlen(title) * 4, d[1] + 6, title, PEN_WHITE);
}

/* --- screens -------------------------------------------------------------- */

static void drawMenu(void)
{
  int cx = MAP_X + MAP_W / 2, y = 26 + logoHeight();

  dialog(NULL);
  logoDraw(cx - logoWidth() / 2, 22);
  textCenter(cx, y, "A GAME OF GLOBAL STRATEGY", PEN_INK);
  y += 16;
  addButton(cx - 110, y, 220, B_SERVER, 's', "PLAY ON THE SERVER", 1);
  addButton(cx - 110, y + 17, 220, B_CAMPAIGN, 'c', "CAMPAIGN VS COMPUTER", 1);
  addButton(cx - 110, y + 34, 220, B_SETUP, 'p', "PAIR WITH A SERVER", 1);
  addButton(cx - 110, y + 51, 220, B_QUIT, 'q', "QUIT", 1);
}

static void drawSetup(void)
{
  dialog("GAME SERVER");
  addField(40, 52, 416, fServer, sizeof fServer, "SERVER ADDRESS");
  addField(40, 80, 160, fCode, sizeof fCode, "PAIRING CODE");
  addField(216, 80, 240, fName, sizeof fName, "COMMANDER NAME");
  text(40, 100, "THE PAIRING CODE IS ON THE SERVER'S", PEN_DARK);
  text(40, 110, "WEB PAGE. RENAME ONCE PAIRED.", PEN_DARK);
  addButton(40, 126, 128, B_PAIR, 'p', "PAIR", fServer[0] && fCode[0]);
  addButton(184, 126, 128, B_RENAME, 'r', "RENAME", apiConfigured() && fName[0]);
  addButton(328, 126, 128, B_BACK, KEY_ESC, "BACK", 1);
  if (apiConfigured()) {
    char line[56];

    sprintf(line, "PAIRED: YOU ARE %.24s HERE.", myName[0] ? myName : "A COMMANDER");
    text(40, 148, line, PEN_INK);
  }
}

static int isMember(const GameInfo *g)
{
  int i;

  for (i = 0; i < g->nplayers; i++)
    if (strcmp(g->playerIds[i], myId) == 0)
      return 1;
  return 0;
}

static const char *rowAction(const GameInfo *g, int *enabled)
{
  int member = isMember(g);

  *enabled = 1;
  if (member && (!strcmp(g->status, "active") || !strcmp(g->status, "paused")))
    return g->hasState ? "RESUME" : "PLAY";
  if (member && !strcmp(g->status, "finished"))
    return "VIEW";
  if (!strcmp(g->status, "waiting")) {
    if (member && !strcmp(g->host, myId) && g->nplayers >= 2)
      return "START";
    if (!member && g->nplayers < g->maxPlayers)
      return "JOIN";
  }
  *enabled = 0;
  return member ? "WAITING" : "FULL";
}

static void drawGames(void)
{
  char line[64], who[NAME_LEN + 8];
  int i, y = 44;

  dialog("SERVER GAMES");
  if (ngames == 0)
    textCenter(248, 80, "NO GAMES YET: START ONE!", PEN_INK);
  well(16, 32, 479, 32 + ROWS * 14 + 4);
  text(24, 35, "GAME", PEN_DARK);
  text(220, 35, "SEATS", PEN_DARK);
  text(276, 35, "TURN", PEN_DARK);
  for (i = 0; i < ROWS && gamesPage * ROWS + i < ngames; i++, y += 14) {
    const GameInfo *g = &games[gamesPage * ROWS + i];
    int enabled, k;
    const char *action = rowAction(g, &enabled);

    sprintf(line, "%.24s", g->name);
    text(24, y + 2, line, PEN_INK);
    sprintf(line, "%d/%d", g->nplayers, g->maxPlayers);
    text(228, y + 2, line, PEN_INK);
    who[0] = 0;
    if (!strcmp(g->status, "waiting"))
      strcpy(who, "LOBBY");
    else if (!strcmp(g->status, "finished"))
      strcpy(who, "OVER");
    else if (!strcmp(g->current, myId))
      strcpy(who, "YOURS!");
    else
      for (k = 0; k < g->nplayers; k++)
        if (!strcmp(g->playerIds[k], g->current))
          sprintf(who, "%.11s", g->playerNames[k]);
    text(276, y + 2, who, !strcmp(g->current, myId) ? PEN_PLAYER : PEN_INK);
    addButton(392, y, 80, B_ROW + i, 0, action, enabled);
  }
  y = 160;
  addButton(16, y, 104, B_NEWGAME, 'n', "NEW GAME", 1);
  addButton(128, y, 104, B_JOINCODE, 'j', "JOIN CODE", 1);
  addButton(240, y, 88, B_REFRESH, 'r', "REFRESH", 1);
  addButton(336, y, 32, B_PREV, '<', "<", gamesPage > 0);
  addButton(372, y, 32, B_NEXT, '>', ">", (gamesPage + 1) * ROWS < ngames);
  addButton(412, y, 68, B_BACK, KEY_ESC, "BACK", 1);
}

static void drawNewGame(void)
{
  char line[16];

  dialog("NEW SERVER GAME");
  addField(96, 66, 304, fGameName, sizeof fGameName, "NAME");
  text(96, 88, "SEATS", PEN_INK);
  addButton(160, 85, 24, B_MINUS, '-', "-", newPlayers > 2);
  sprintf(line, "%d", newPlayers);
  textBold(196, 88, line, PEN_INK);
  addButton(216, 85, 24, B_PLUS, '+', "+", newPlayers < 6);
  text(96, 106, "PLAY", PEN_INK);
  addButton(160, 103, 136, B_MODE, 'm', newRealtime ? "ANY TIME" : "TURN BY TURN", 1);
  addButton(96, 134, 136, B_CREATE, KEY_ENTER, "CREATE", fGameName[0] != 0);
  addButton(264, 134, 136, B_BACK, KEY_ESC, "CANCEL", 1);
}

static void drawJoin(void)
{
  dialog("JOIN A GAME");
  addField(136, 88, 224, fJoin, sizeof fJoin, "JOIN CODE (FROM THE HOST)");
  addButton(136, 112, 104, B_JOIN, KEY_ENTER, "JOIN", fJoin[0] != 0);
  addButton(256, 112, 104, B_BACK, KEY_ESC, "CANCEL", 1);
}

static void drawCampaign(void)
{
  char line[24];
  int i;

  dialog("NEW CAMPAIGN");
  text(112, 44, "COMPUTER COMMANDERS", PEN_INK);
  addButton(272, 41, 24, B_MINUS, '-', "-", cpuCount > 1);
  sprintf(line, "%d", cpuCount);
  textBold(308, 44, line, PEN_INK);
  addButton(328, 41, 24, B_PLUS, '+', "+", cpuCount < 5);
  swatch(112, 62, 0);
  text(128, 62, "YOU", PEN_INK);
  text(272, 62, "HUMAN", PEN_DARK);
  for (i = 0; i < cpuCount; i++) {
    swatch(112, 78 + i * 15, i + 1);
    sprintf(line, "CPU %d", i + 1);
    text(128, 78 + i * 15, line, PEN_INK);
    addButton(240, 75 + i * 15, 112, B_KIND + i, '1' + i, kindLabels[cpuKinds[i]], 1);
  }
  addButton(112, 154, 112, B_BEGIN, KEY_ENTER, "BEGIN", 1);
  addButton(272, 154, 112, B_BACK, KEY_ESC, "BACK", 1);
}

static const char *const tops[] = {
  "A GAME OF GLOBAL STRATEGY", "PAIR WITH A GAME SERVER", "PLAY ON THE SERVER",
  "PLAY ON THE SERVER", "PLAY ON THE SERVER", "AGAINST THE COMPUTER",
};

/* The dialog, its buttons and fields, into the back buffer */
static void drawDialog(void)
{
  int i;

  nbuttons = 0;
  nfields = 0;
  switch (mode) {
    case M_MENU: drawMenu(); break;
    case M_SETUP: drawSetup(); break;
    case M_GAMES: drawGames(); break;
    case M_NEWGAME: drawNewGame(); break;
    case M_JOIN: drawJoin(); break;
    case M_CAMPAIGN: drawCampaign(); break;
  }
  if (activeField >= nfields)
    activeField = nfields ? 0 : -1;
  drawButtons();
  for (i = 0; i < nfields; i++)
    drawField(i);
}

static void presentDialog(int m)
{
  const WORD *d = dialogs[m];

  present(d[0], d[1], d[2] + SHADOW_X, d[3] + SHADOW_Y);
}

/* Everything again: on start, and coming back from a game */
void uiRedraw(void)
{
  if (mode == M_PLAY) {
    mapInvalidate();
    drawBars(NULL);
    playDraw();
    presentAll();
    return;
  }
  mapInvalidate();
  mapDraw(attract, NULL, -1, NULL);
  drawBars(tops[mode]);
  drawSidePanel();
  drawDialog();
  presentAll();
}

/* Only the dialog, the panel beside the board and the status line */
static void uiRefresh(void)
{
  drawTop(tops[mode]);
  drawSidePanel();
  drawDialog();
  panel(0, BAR_Y, SCREEN_W - 1, SCREEN_H - 1);
  text(8, BAR_Y + 3, status, statusPen);
  WaitTOF();
  present(0, 0, SCREEN_W - 1, MAP_Y - 1);
  present(PANEL_X, MAP_Y, PANEL_R, BAR_Y - 1);
  present(0, BAR_Y, SCREEN_W - 1, SCREEN_H - 1);
  presentDialog(mode);
}

void uiShow(int newMode)
{
  int old = mode, i;

  if (mode != newMode)
    activeField = -1;
  mode = newMode;
  if (mode != M_PLAY && attract[0] == attract[1] && attract[1] == attract[2])
    for (i = 0; i < NTERR; i++)
      attract[i] = (BYTE)rnd(MAXPLAYERS);
  if (mode == M_SETUP || mode == M_NEWGAME || mode == M_JOIN)
    activeField = 0;
  if (old == M_PLAY || mode == M_PLAY) {
    uiRedraw();
    return;
  }
  // The board comes back where the old dialog was
  if (old != mode) {
    const WORD *d = dialogs[old];

    mapRestore(d[0], d[1], d[2] + SHADOW_X, d[3] + SHADOW_Y);
  }
  drawTop(tops[mode]);
  drawSidePanel();
  drawDialog();
  mapPresent();
  present(0, 0, SCREEN_W - 1, MAP_Y - 1);
  present(PANEL_X, MAP_Y, PANEL_R, BAR_Y - 1);
  presentDialog(mode);
}

/* --- actions -------------------------------------------------------------- */

static int busy(const char *what)
{
  setStatus(what, PEN_INK);
  return 1;
}

static void failed(void)
{
  sfx(SFX_ERROR);
  setStatus(apiError, PEN_PLAYER);
}

static void refreshGames(void)
{
  busy("ASKING THE SERVER FOR GAMES...");
  if (!myId[0] && !apiWhoAmI()) {
    failed();
    if (!apiConfigured())
      uiShow(M_SETUP);
    return;
  }
  if (!apiListGames(games, sizeof games / sizeof games[0], &ngames)) {
    failed();
    return;
  }
  if (gamesPage * ROWS >= ngames)
    gamesPage = 0;
  gamesStamp = frame;
  sprintf(status, "%d GAME%s ON THE SERVER. YOU ARE %.20s.", ngames, ngames == 1 ? "" : "S", myName);
  statusPen = PEN_INK;
  uiRefresh();
}

static void openGame(GameInfo *g)
{
  int enabled;
  const char *action = rowAction(g, &enabled);
  static GameInfo updated;

  if (!enabled)
    return;
  if (!strcmp(action, "JOIN")) {
    busy("JOINING...");
    if (!apiJoin(g->id, &updated))
      failed();
    refreshGames();
    return;
  }
  if (!strcmp(action, "START")) {
    busy("STARTING THE GAME...");
    if (!apiStart(g->id, &updated)) {
      failed();
      return;
    }
    *g = updated;
  }
  if (playRemote(g))
    uiShow(M_PLAY);
  else
    failed();
}

static void action(int id)
{
  static GameInfo g;

  if (id >= B_ROW && id < B_ROW + ROWS) {
    openGame(&games[gamesPage * ROWS + id - B_ROW]);
    return;
  }
  if (id >= B_KIND && id < B_KIND + 5) {
    UBYTE *k = &cpuKinds[id - B_KIND];

    *k = (UBYTE)(*k == K_AGGRESSIVE ? K_CAUTIOUS : *k + 1);
    uiRefresh();
    return;
  }
  switch (id) {
    case B_SERVER:
      if (!apiConfigured()) {
        setStatus("PAIR WITH A SERVER FIRST.", PEN_INK);
        uiShow(M_SETUP);
        return;
      }
      uiShow(M_GAMES);
      refreshGames();
      return;
    case B_CAMPAIGN:
      uiShow(M_CAMPAIGN);
      return;
    case B_SETUP:
      if (prefs.server[0])
        strcpy(fServer, prefs.server);
      strcpy(fName, myName);
      uiShow(M_SETUP);
      return;
    case B_QUIT:
      quitting = 1;
      return;
    case B_PAIR:
      busy("PAIRING...");
      if (!apiRegister(fServer, fCode)) {
        failed();
        uiRefresh();
        return;
      }
      fCode[0] = 0;
      strcpy(fName, myName);
      sfx(SFX_TURN);
      sprintf(status, "PAIRED! YOU ARE %.20s ON THIS SERVER.", myName);
      statusPen = PEN_INK;
      uiRefresh();
      return;
    case B_RENAME:
      busy("RENAMING...");
      if (!apiRename(fName)) {
        failed();
        return;
      }
      sprintf(status, "YOU ARE NOW %.30s.", myName);
      statusPen = PEN_INK;
      uiRefresh();
      return;
    case B_BACK:
      uiShow(mode == M_NEWGAME || mode == M_JOIN ? M_GAMES : M_MENU);
      return;
    case B_NEWGAME:
      uiShow(M_NEWGAME);
      return;
    case B_JOINCODE:
      uiShow(M_JOIN);
      return;
    case B_REFRESH:
      refreshGames();
      return;
    case B_PREV:
      gamesPage--;
      uiRefresh();
      return;
    case B_NEXT:
      gamesPage++;
      uiRefresh();
      return;
    case B_MINUS:
    case B_PLUS:
      if (mode == M_NEWGAME)
        newPlayers += id == B_PLUS ? 1 : -1;
      else
        cpuCount += id == B_PLUS ? 1 : -1;
      uiRefresh();
      return;
    case B_MODE:
      newRealtime = !newRealtime;
      uiRefresh();
      return;
    case B_CREATE:
      busy("CREATING THE GAME...");
      if (!apiCreateGame(fGameName, newRealtime ? "realtime" : "turn-based", newPlayers, &g)) {
        failed();
        return;
      }
      sprintf(status, "GAME CREATED. JOIN CODE: %s", g.joinCode);
      mode = M_GAMES;
      refreshGames();
      sprintf(status, "GAME CREATED. FRIENDS JOIN WITH CODE %s", g.joinCode);
      setStatus(status, PEN_INK);
      return;
    case B_JOIN:
      busy("JOINING...");
      if (!apiJoin(fJoin, &g)) {
        failed();
        return;
      }
      fJoin[0] = 0;
      mode = M_GAMES;
      refreshGames();
      return;
    case B_BEGIN:
      playCampaign(cpuCount, cpuKinds);
      uiShow(M_PLAY);
      return;
  }
}

void uiClick(int x, int y)
{
  int i;

  if (mode == M_PLAY) {
    playClick(x, y);
    return;
  }
  if ((i = buttonAt(buttons, nbuttons, x, y)) >= 0) {
    pressButton(&buttons[i]);
    action(buttons[i].id);
    return;
  }
  for (i = 0; i < nfields; i++) {
    const Field *f = &fields[i];

    if (x >= f->x && x < f->x + f->w && y >= f->y && y < f->y + 12) {
      int old = activeField;

      activeField = i;
      if (old >= 0)
        redrawField(old);
      redrawField(i);
      return;
    }
  }
}

void uiKey(int c)
{
  int i;

  if (mode == M_PLAY) {
    playKey(c);
    return;
  }
  if (activeField >= 0 && activeField < nfields) {
    Field *f = &fields[activeField];
    int len = (int)strlen(f->buf);

    if (c == KEY_TAB) {
      activeField = (activeField + 1) % nfields;
      uiRefresh();
      return;
    }
    if (c == KEY_BACK || c == KEY_DEL) {
      if (len)
        f->buf[len - 1] = 0;
      uiRefresh();
      return;
    }
    // Ctrl-X empties the field, as in Amiga string gadgets
    if (c == 0x18) {
      f->buf[0] = 0;
      uiRefresh();
      return;
    }
    if (c >= 32 && c != KEY_DEL && len < f->max - 1) {
      f->buf[len] = (char)c;
      f->buf[len + 1] = 0;
      // Buttons enable as fields fill, so draw them all again
      uiRefresh();
      return;
    }
    if (c == KEY_ENTER && activeField + 1 < nfields) {
      activeField++;
      uiRefresh();
      return;
    }
  }
  if (c == KEY_ENTER && mode == M_SETUP) {
    for (i = 0; i < nbuttons; i++)
      if (buttons[i].id == B_PAIR && buttons[i].enabled) {
        pressButton(&buttons[i]);
        action(B_PAIR);
      }
    return;
  }
  if (c >= 'A' && c <= 'Z')
    c += 'a' - 'A';
  for (i = 0; i < nbuttons; i++)
    if (buttons[i].key == c && buttons[i].enabled) {
      pressButton(&buttons[i]);
      action(buttons[i].id);
      return;
    }
}

void uiMove(int x, int y)
{
  if (mode == M_PLAY)
    playMove(x, y);
}

void uiTick(void)
{
  static UWORD lastStep;

  if (mode == M_PLAY) {
    playTick();
    return;
  }
  if ((UWORD)(frame - lastStep) >= 25) {
    lastStep = frame;
    attractStep();
    uiRefresh();
  }
  if (mode == M_GAMES && (UWORD)(frame - gamesStamp) > 50 * 20)
    refreshGames();
}
