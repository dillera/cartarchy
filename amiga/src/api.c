#include <stdio.h>
#include <string.h>

#include <exec/memory.h>
#include <proto/exec.h>

#include "fujinet-nio.h"
#include "fn_legacy_appkey.h"
#include "api.h"
#include "net.h"

/* App keys: creator 0xCA27 ("CArtarchy"), app 1, one key per setting */
#define CREATOR 0xCA27
#define APP_ID 1
#define KEY_SERVER 1
#define KEY_TOKEN  2
#define KEY_DEVICE 3

/* A game list carries every game's state; one game's start is enough to
   reach the end of its state, which comes before its event history */
#define RESP_MAX  49152L
#define GAME_MAX  24576L
#define VERSION_PREFIX 1536L
#define BODY_MAX  16384L

Prefs prefs;
char myId[ID_LEN], myName[NAME_LEN], apiError[80];
static char myZone[64] = "UTC";
static char *resp, *body;
static long respLen;

int apiInit(void)
{
  resp = AllocMem(RESP_MAX, MEMF_ANY);
  body = AllocMem(BODY_MAX, MEMF_ANY);
  return resp && body;
}

void apiFree(void)
{
  if (resp)
    FreeMem(resp, RESP_MAX);
  if (body)
    FreeMem(body, BODY_MAX);
  resp = body = NULL;
}

static void readKey(uint8_t key, char *out, int max)
{
  static uint8_t tmp[260];
  uint16_t count = 0;

  fuji_set_appkey_details(CREATOR, APP_ID, SIZE_256);
  if (!fuji_read_appkey(key, &count, tmp) || count >= max)
    count = 0;
  memcpy(out, tmp, count);
  out[count] = 0;
}

static void writeKey(uint8_t key, const char *value)
{
  fuji_set_appkey_details(CREATOR, APP_ID, SIZE_256);
  fuji_write_appkey(key, (uint16_t)strlen(value), (uint8_t *)value);
}

void prefsLoad(void)
{
  if (!netOpen())
    return;
  readKey(KEY_SERVER, prefs.server, sizeof prefs.server);
  readKey(KEY_TOKEN, prefs.token, sizeof prefs.token);
  readKey(KEY_DEVICE, prefs.device, sizeof prefs.device);
}

void prefsSave(void)
{
  if (!netOpen())
    return;
  writeKey(KEY_SERVER, prefs.server);
  writeKey(KEY_TOKEN, prefs.token);
  writeKey(KEY_DEVICE, prefs.device);
}

int apiConfigured(void)
{
  return prefs.server[0] && prefs.token[0];
}

static void upper(char *s)
{
  for (; *s; s++)
    if (*s >= 'a' && *s <= 'z')
      *s -= 'a' - 'A';
}

static void setError(int status)
{
  char msg[64];

  if (!status) {
    strncpy(apiError, netError, sizeof apiError - 1);
  } else if (jsonStr(jsonGet(resp, resp + respLen, "error"), resp + respLen, msg, sizeof msg) > 0) {
    upper(msg);
    sprintf(apiError, "SERVER: %.66s", msg);
  } else {
    sprintf(apiError, "THE SERVER ANSWERED %d", status);
  }
}

static int makeUrl(char *url, int size, const char *path)
{
  if ((int)(strlen(prefs.server) + strlen(path)) >= size) {
    strcpy(apiError, "SERVER ADDRESS TOO LONG");
    return 0;
  }
  strcpy(url, prefs.server);
  strcat(url, path);
  return 1;
}

/* What of a reply is worth reading over a 19200-baud link: a game's JSON
   has its version first, then its state, then its whole history */
typedef int (*Enough)(const char *p, const char *end);

static int hasVersion(const char *p, const char *end)
{
  return jsonSkip(jsonGet(p, end, "version"), end) != NULL;
}

static int stateRead(const char *p, const char *end)
{
  const char *state = jsonGet(p, end, "state");

  return (state && jsonSkip(state, end)) || jsonGet(p, end, "events");
}

/* Read on into resp until enough (NULL: all of it), the end, or max */
static void readOn(long max, Enough enough)
{
  while (respLen < max - 1 && !(enough && enough(resp, resp + respLen))) {
    long room = max - 1 - respLen, n = httpRead(resp + respLen, room > 1024 ? 1024 : room);

    if (n <= 0)
      break;
    respLen += n;
    resp[respLen] = 0;
  }
}

/* One call, its reply (as far as enough) in resp; returns the status when
   it is the one wanted, else 0 with apiError set */
static int call(int method, const char *path, const char *json, long max, int want, Enough enough)
{
  char url[256];
  int status = 0;

  if (!makeUrl(url, sizeof url, path))
    return 0;
  respLen = 0;
  resp[0] = 0;
  if (httpOpen(method, url, prefs.token, json, json ? (long)strlen(json) : 0)) {
    readOn(max, enough);
    status = httpStatus();
  }
  httpClose();
  if (status == want)
    return status;
  setError(status);
  if (status == 401) {
    // The server forgot this Amiga: pair again
    prefs.token[0] = 0;
    strcpy(apiError, "NOT PAIRED WITH THIS SERVER: PAIR AGAIN");
  }
  return 0;
}

static void copyStr(const char *v, const char *end, char *out, int max)
{
  if (jsonStr(v, end, out, max) < 0)
    out[0] = 0;
}

static void parseGame(const char *obj, const char *end, GameInfo *g)
{
  const char *v, *state;
  int i;

  memset(g, 0, sizeof *g);
  copyStr(jsonGet(obj, end, "id"), end, g->id, ID_LEN);
  copyStr(jsonGet(obj, end, "joinCode"), end, g->joinCode, sizeof g->joinCode);
  copyStr(jsonGet(obj, end, "name"), end, g->name, sizeof g->name);
  copyStr(jsonGet(obj, end, "mode"), end, g->mode, sizeof g->mode);
  copyStr(jsonGet(obj, end, "status"), end, g->status, sizeof g->status);
  copyStr(jsonGet(obj, end, "hostId"), end, g->host, ID_LEN);
  copyStr(jsonGet(obj, end, "currentPlayerId"), end, g->current, ID_LEN);
  copyStr(jsonGet(obj, end, "winnerId"), end, g->winner, ID_LEN);
  g->maxPlayers = (UBYTE)jsonInt(jsonGet(obj, end, "maxPlayers"), end, 2);
  g->version = jsonInt(jsonGet(obj, end, "version"), end, 0);
  i = 0;
  for (v = jsonFirst(jsonGet(obj, end, "playerIds"), end); v && i < MAXPLAYERS; v = jsonNext(v, end))
    copyStr(v, end, g->playerIds[i++], ID_LEN);
  g->nplayers = (UBYTE)i;
  i = 0;
  for (v = jsonFirst(jsonGet(obj, end, "players"), end); v && i < MAXPLAYERS; v = jsonNext(v, end))
    copyStr(jsonGet(v, end, "name"), end, g->playerNames[i++], NAME_LEN);
  state = jsonGet(obj, end, "state");
  g->hasState = state && !jsonIsNull(state, end);
}

int apiRegister(const char *server, const char *code)
{
  JsonOut o;
  const char *player, *end;
  int len;

  strncpy(prefs.server, server, sizeof prefs.server - 1);
  len = (int)strlen(prefs.server);
  while (len && prefs.server[len - 1] == '/')
    prefs.server[--len] = 0;
  if (!prefs.device[0])
    sprintf(prefs.device, "amiga-%04x%04x%04x", rnd(0x10000), rnd(0x10000), rnd(0x10000));
  prefs.token[0] = 0;

  jsonInit(&o, body, BODY_MAX);
  jsonPut(&o, "{\"DeviceID\":");
  jsonPutStr(&o, prefs.device);
  jsonPut(&o, ",\"Name\":\"Amiga\",\"PairingCode\":");
  jsonPutStr(&o, code);
  jsonPut(&o, "}");
  if (!call(FN_METHOD_POST, "/v1/devices/register", body, 4096, 201, NULL))
    return 0;
  end = resp + respLen;
  copyStr(jsonGet(resp, end, "token"), end, prefs.token, sizeof prefs.token);
  player = jsonGet(resp, end, "player");
  copyStr(jsonGet(player, end, "id"), end, myId, ID_LEN);
  copyStr(jsonGet(player, end, "name"), end, myName, NAME_LEN);
  if (!prefs.token[0]) {
    strcpy(apiError, "THE SERVER SENT NO TOKEN");
    return 0;
  }
  prefsSave();
  return 1;
}

int apiWhoAmI(void)
{
  const char *end;

  if (!call(FN_METHOD_GET, "/v1/stats", NULL, 4096, 200, NULL)) {
    if (!prefs.token[0])
      prefsSave();
    return 0;
  }
  end = resp + respLen;
  copyStr(jsonGet(resp, end, "id"), end, myId, ID_LEN);
  copyStr(jsonGet(resp, end, "name"), end, myName, NAME_LEN);
  copyStr(jsonGet(resp, end, "timezone"), end, myZone, sizeof myZone);
  return myId[0] != 0;
}

int apiRename(const char *name)
{
  JsonOut o;
  const char *end;

  jsonInit(&o, body, BODY_MAX);
  jsonPut(&o, "{\"Name\":");
  jsonPutStr(&o, name);
  jsonPut(&o, ",\"Timezone\":");
  jsonPutStr(&o, myZone[0] ? myZone : "UTC");
  jsonPut(&o, "}");
  if (!call(FN_METHOD_PUT, "/v1/profile", body, 4096, 200, NULL))
    return 0;
  end = resp + respLen;
  copyStr(jsonGet(resp, end, "name"), end, myName, NAME_LEN);
  return 1;
}

int apiListGames(GameInfo *games, int max, int *count)
{
  const char *v, *end;

  *count = 0;
  if (!call(FN_METHOD_GET, "/v1/games", NULL, RESP_MAX, 200, NULL))
    return 0;
  end = resp + respLen;
  for (v = jsonFirst(resp, end); v && *count < max; v = jsonNext(v, end))
    parseGame(v, end, &games[(*count)++]);
  return 1;
}

static int gameCall(int method, const char *fmt, const char *id, const char *json, int want, GameInfo *g)
{
  char path[96];

  if (strlen(id) > 48) {
    strcpy(apiError, "GAME ID TOO LONG");
    return 0;
  }
  sprintf(path, fmt, id);
  if (!call(method, path, json, GAME_MAX, want, stateRead))
    return 0;
  if (g)
    parseGame(resp, resp + respLen, g);
  return 1;
}

int apiCreateGame(const char *name, const char *mode, int maxPlayers, GameInfo *g)
{
  JsonOut o;

  jsonInit(&o, body, BODY_MAX);
  jsonPut(&o, "{\"Name\":");
  jsonPutStr(&o, name);
  jsonPut(&o, ",\"Mode\":");
  jsonPutStr(&o, mode);
  jsonPut(&o, ",\"MaxPlayers\":");
  jsonPutInt(&o, maxPlayers);
  jsonPut(&o, "}");
  return gameCall(FN_METHOD_POST, "/v1/games%s", "", body, 201, g);
}

int apiJoin(const char *idOrCode, GameInfo *g)
{
  return gameCall(FN_METHOD_POST, "/v1/games/%s/join", idOrCode, "{}", 200, g);
}

int apiStart(const char *id, GameInfo *g)
{
  return gameCall(FN_METHOD_POST, "/v1/games/%s/start", id, "{}", 200, g);
}

int apiResume(const char *id, GameInfo *g)
{
  return gameCall(FN_METHOD_POST, "/v1/games/%s/resume", id, "{}", 200, g);
}

int apiResign(const char *id)
{
  return gameCall(FN_METHOD_POST, "/v1/games/%s/resign", id, "{}", 200, NULL);
}

int apiFinish(const char *id, const char *winnerId)
{
  JsonOut o;

  jsonInit(&o, body, BODY_MAX);
  jsonPut(&o, "{\"WinnerID\":");
  jsonPutStr(&o, winnerId);
  jsonPut(&o, "}");
  return gameCall(FN_METHOD_POST, "/v1/games/%s/finish", id, body, 200, NULL);
}

int apiFetchGame(const char *id, long known, GameInfo *g, Match *m, int *hasState, int *changed)
{
  char url[256], path[96];
  const char *end, *state;
  int status;

  *changed = 1;
  *hasState = 0;
  if (strlen(id) > 48) {
    strcpy(apiError, "GAME ID TOO LONG");
    return 0;
  }
  sprintf(path, "/v1/games/%s", id);
  if (!makeUrl(url, sizeof url, path))
    return 0;
  respLen = 0;
  resp[0] = 0;
  if (!httpOpen(FN_METHOD_GET, url, prefs.token, NULL, 0)) {
    setError(0);
    return 0;
  }
  // The version comes early: an unchanged game costs only this much
  readOn(VERSION_PREFIX, hasVersion);
  status = httpStatus();
  if (status == 200 && known >= 0 &&
      jsonInt(jsonGet(resp, resp + respLen, "version"), resp + respLen, -1) == known) {
    httpClose();
    *changed = 0;
    return 1;
  }
  if (status == 200) {
    readOn(GAME_MAX, stateRead);
    status = httpStatus();
  }
  httpClose();
  if (status != 200) {
    setError(status);
    return 0;
  }
  end = resp + respLen;
  parseGame(resp, end, g);
  if (!g->id[0] || g->nplayers == 0) {
    strcpy(apiError, "THE SERVER SENT A GAME WITHOUT PLAYERS");
    return 0;
  }
  state = jsonGet(resp, end, "state");
  if (state && !jsonIsNull(state, end)) {
    if (!matchFromJson(m, state, end)) {
      strcpy(apiError, "COULD NOT READ THE GAME STATE");
      return 0;
    }
    *hasState = 1;
  }
  return 1;
}

int apiPushEvent(const char *id, long version, const char *kind, const char *currentId,
                 const Match *m, long *newVersion)
{
  char url[256], path[96];
  JsonOut o;
  int status;

  if (strlen(id) > 48)
    return 0;
  jsonInit(&o, body, BODY_MAX);
  jsonPut(&o, "{\"ExpectedVersion\":");
  jsonPutInt(&o, version);
  jsonPut(&o, ",\"Kind\":");
  jsonPutStr(&o, kind);
  jsonPut(&o, ",\"Payload\":{\"currentPlayerId\":");
  jsonPutStr(&o, currentId);
  jsonPut(&o, "},\"State\":");
  matchToJson(m, &o);
  jsonPut(&o, "}");
  if (o.overflow) {
    strcpy(apiError, "GAME STATE TOO LARGE");
    return 0;
  }
  sprintf(path, "/v1/games/%s/events", id);
  if (!makeUrl(url, sizeof url, path))
    return 0;
  respLen = 0;
  resp[0] = 0;
  status = 0;
  if (httpOpen(FN_METHOD_POST, url, prefs.token, body, o.len)) {
    readOn(VERSION_PREFIX, hasVersion);
    status = httpStatus();
  }
  httpClose();
  if (status == 200) {
    *newVersion = jsonInt(jsonGet(resp, resp + respLen, "version"), resp + respLen, version + 1);
    return 1;
  }
  setError(status);
  return status == 409 ? -1 : 0;
}
