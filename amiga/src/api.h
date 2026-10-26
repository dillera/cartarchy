#ifndef API_H
#define API_H

#include "cartarchy.h"

/* The Cartarchy server's /v1 API, as the iOS app uses it */

typedef struct {
  char id[ID_LEN], name[48], status[12], mode[12], joinCode[12];
  char host[ID_LEN], current[ID_LEN], winner[ID_LEN];
  UBYTE nplayers, maxPlayers, hasState;
  char playerIds[MAXPLAYERS][ID_LEN];
  char playerNames[MAXPLAYERS][NAME_LEN];
  long version;
} GameInfo;

/* Saved with the FujiNet's app keys */
typedef struct {
  char server[128], token[80], device[40];
} Prefs;

extern Prefs prefs;
extern char myId[ID_LEN], myName[NAME_LEN];
extern char apiError[80];

void prefsLoad(void);
void prefsSave(void);

int apiInit(void);             /* response buffers; 0 if out of memory */
void apiFree(void);
int apiConfigured(void);
/* Each returns 1 on success; otherwise apiError says why */
int apiRegister(const char *server, const char *code);
int apiWhoAmI(void);
int apiRename(const char *name);
int apiListGames(GameInfo *games, int max, int *count);
int apiCreateGame(const char *name, const char *mode, int maxPlayers, GameInfo *g);
int apiJoin(const char *idOrCode, GameInfo *g);
int apiStart(const char *id, GameInfo *g);
int apiResume(const char *id, GameInfo *g);
int apiResign(const char *id);
int apiFinish(const char *id, const char *winnerId);
/* The game and its state; *changed is 0 (and nothing read past the version)
   when the server still has version known */
int apiFetchGame(const char *id, long known, GameInfo *g, Match *m, int *hasState, int *changed);
/* Send a move; newVersion gets the server's version after it. A 409 (the
   server moved on) returns -1 so the caller reloads. */
int apiPushEvent(const char *id, long version, const char *kind, const char *currentId,
                 const Match *m, long *newVersion);

#endif
