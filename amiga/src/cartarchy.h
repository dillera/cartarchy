#ifndef CARTARCHY_H
#define CARTARCHY_H

#include <exec/types.h>

#include "mapdata.h"
#include "json.h"

#define MAXPLAYERS 6
#define NCARDS     44          /* a card per territory and two wild cards */
#define WILD_1     NTERR
#define WILD_2     (NTERR + 1)
#define NAME_LEN   24
#define MSG_LEN    128
#define ID_LEN     40

/* The game state every client shares through the server (SavedMatch in
   the iOS app); the strings it is written with are in rules.c */
enum { PH_CLAIM, PH_DEPLOY, PH_REINFORCE, PH_ATTACK, PH_FORTIFY, PH_OVER };
enum { K_HUMAN, K_CAUTIOUS, K_BALANCED, K_AGGRESSIVE };
enum { UNIT_WILD = -1, UNIT_INFANTRY, UNIT_CAVALRY, UNIT_ARTILLERY };

typedef struct {
  UBYTE id;                    /* territory index, or WILD_1/WILD_2 */
  BYTE unit;
} Card;

typedef struct {
  char name[NAME_LEN];
  UBYTE kind, alive, ncards;
  Card cards[NCARDS];
} Player;

typedef struct {
  UBYTE nplayers;
  Player players[MAXPLAYERS];
  BYTE owner[NTERR];           /* -1 unclaimed */
  LONG armies[NTERR];
  UBYTE current, phase;
  WORD reinforcements, turn;
  char message[MSG_LEN];
  UBYTE ndeck, ndiscard;
  Card deck[NCARDS], discard[NCARDS];
  WORD tradeCount;
  UBYTE conquered;
  WORD setupArmies[MAXPLAYERS];
} Match;

/* rules.c: ClassicRules and GameStore from the iOS app */
extern const char *const phaseNames[], *const kindNames[], *const unitNames[];
extern WORD selected;          /* territory picked to attack or move from */
extern UBYTE diceA[3], diceD[2], nDiceA, nDiceD;

void rulesSeed(ULONG seed);
int rnd(int n);
void newMatch(Match *m, int n, const char names[][NAME_LEN], const UBYTE *kinds);
int initialArmies(int players);
int reinforcementCount(const Match *m, int owner);
int tradeValue(int n);
int findSet(const Player *p, int idx[3]);
int adjacent(int a, int b);
int ownedCount(const Match *m, int owner);
long armyCount(const Match *m, int owner);
int isTarget(const Match *m, int t);
/* A move by the current player: the event kind to send ("state.updated" or
   "turn.completed"), or NULL when nothing changed */
const char *actTap(Match *m, int t);
const char *actNextPhase(Match *m);
const char *actTrade(Match *m);
const char *actRandomize(Match *m);
int canTrade(const Match *m);
/* One computer move; 0 when a human is to play */
int aiStep(Match *m);

/* state.c: the JSON every client reads and writes */
int matchFromJson(Match *m, const char *p, const char *end);
void matchToJson(const Match *m, JsonOut *o);

#endif
