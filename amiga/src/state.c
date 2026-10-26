#include <string.h>

#include "cartarchy.h"

/* SavedMatch as JSON, in the form the iOS and Android clients exchange
   through the server: wild cards carry null territoryID and unit. */

static int territoryIndex(const char *id)
{
  int t;

  for (t = 0; t < NTERR; t++)
    if (strcmp(territories[t].id, id) == 0)
      return t;
  return -1;
}

static int lookup(const char *const *names, int n, const char *s)
{
  int i;

  for (i = 0; i < n; i++)
    if (strcmp(names[i], s) == 0)
      return i;
  return -1;
}

static int readCards(Card *cards, const char *arr, const char *end)
{
  const char *v;
  char s[24];
  int n = 0;

  for (v = jsonFirst(arr, end); v && n < NCARDS; v = jsonNext(v, end)) {
    Card *c = &cards[n];
    int t = -1;

    if (jsonStr(jsonGet(v, end, "territoryID"), end, s, sizeof s) > 0 ||
        jsonStr(jsonGet(v, end, "territoryId"), end, s, sizeof s) > 0)
      t = territoryIndex(s);
    if (t < 0 && jsonStr(jsonGet(v, end, "id"), end, s, sizeof s) > 0)
      t = strcmp(s, "wild-2") == 0 ? WILD_2 : territoryIndex(s);
    c->id = (UBYTE)(t >= 0 ? t : WILD_1);
    c->unit = UNIT_WILD;
    if (jsonStr(jsonGet(v, end, "unit"), end, s, sizeof s) > 0) {
      // Android's own save format has upper-case units
      char *q;

      for (q = s; *q; q++)
        if (*q >= 'A' && *q <= 'Z')
          *q += 'a' - 'A';
      c->unit = (BYTE)lookup(unitNames, 3, s);
    }
    n++;
  }
  return n;
}

/* A wild card without a known id: make it whichever wild is not in play */
static void fixWilds(Match *m)
{
  static Card *all[NCARDS * (MAXPLAYERS + 2)];
  int n = 0, i, k, seen = 0;

  for (i = 0; i < m->nplayers; i++)
    for (k = 0; k < m->players[i].ncards; k++)
      all[n++] = &m->players[i].cards[k];
  for (k = 0; k < m->ndeck; k++)
    all[n++] = &m->deck[k];
  for (k = 0; k < m->ndiscard; k++)
    all[n++] = &m->discard[k];
  for (i = 0; i < n; i++)
    if (all[i]->id >= NTERR) {
      if (seen & (1 << (all[i]->id - NTERR)))
        all[i]->id = all[i]->id == WILD_1 ? WILD_2 : WILD_1;
      seen |= 1 << (all[i]->id - NTERR);
    }
}

int matchFromJson(Match *m, const char *p, const char *end)
{
  const char *v, *players = jsonGet(p, end, "players");
  char s[MSG_LEN], key[24];
  int i;

  if (!players || !jsonSkip(p, end))
    return 0;
  memset(m, 0, sizeof *m);
  for (v = jsonFirst(players, end); v && m->nplayers < MAXPLAYERS; v = jsonNext(v, end)) {
    Player *pl = &m->players[m->nplayers++];

    jsonStr(jsonGet(v, end, "name"), end, pl->name, NAME_LEN);
    pl->kind = K_HUMAN;
    if (jsonStr(jsonGet(v, end, "kind"), end, s, sizeof s) > 0 && (i = lookup(kindNames, 4, s)) >= 0)
      pl->kind = (UBYTE)i;
    pl->alive = (UBYTE)jsonBool(jsonGet(v, end, "isAlive"), end, 1);
    pl->ncards = (UBYTE)readCards(pl->cards, jsonGet(v, end, "cards"), end);
  }
  if (m->nplayers < 2)
    return 0;

  for (i = 0; i < NTERR; i++)
    m->owner[i] = -1;
  for (v = jsonFirstMember(jsonGet(p, end, "territories"), end, key, sizeof key); v;
       v = jsonNextMember(v, end, key, sizeof key)) {
    int t = territoryIndex(key);

    if (t >= 0) {
      m->owner[t] = (BYTE)jsonInt(jsonGet(v, end, "owner"), end, -1);
      m->armies[t] = jsonInt(jsonGet(v, end, "armies"), end, 0);
      if (m->owner[t] >= m->nplayers)
        m->owner[t] = -1;
    }
  }

  m->current = (UBYTE)jsonInt(jsonGet(p, end, "currentPlayer"), end, 0);
  if (m->current >= m->nplayers)
    m->current = 0;
  m->phase = PH_CLAIM;
  if (jsonStr(jsonGet(p, end, "phase"), end, s, sizeof s) > 0 && (i = lookup(phaseNames, 6, s)) >= 0)
    m->phase = (UBYTE)i;
  m->reinforcements = (WORD)jsonInt(jsonGet(p, end, "reinforcements"), end, 0);
  m->turn = (WORD)jsonInt(jsonGet(p, end, "turn"), end, 0);
  jsonStr(jsonGet(p, end, "message"), end, m->message, MSG_LEN);
  m->ndeck = (UBYTE)readCards(m->deck, jsonGet(p, end, "deck"), end);
  m->ndiscard = (UBYTE)readCards(m->discard, jsonGet(p, end, "discard"), end);
  m->tradeCount = (WORD)jsonInt(jsonGet(p, end, "tradeCount"), end, 0);
  m->conquered = (UBYTE)jsonBool(jsonGet(p, end, "conqueredThisTurn"), end, 0);
  i = 0;
  for (v = jsonFirst(jsonGet(p, end, "setupArmies"), end); v && i < MAXPLAYERS; v = jsonNext(v, end))
    m->setupArmies[i++] = (WORD)jsonInt(v, end, 0);
  fixWilds(m);
  return 1;
}

static void putCards(JsonOut *o, const Card *cards, int n)
{
  int i;

  jsonPut(o, "[");
  for (i = 0; i < n; i++) {
    const Card *c = &cards[i];

    jsonPut(o, i ? ",{\"id\":" : "{\"id\":");
    if (c->id < NTERR) {
      jsonPutStr(o, territories[c->id].id);
      jsonPut(o, ",\"territoryID\":");
      jsonPutStr(o, territories[c->id].id);
    } else {
      jsonPutStr(o, c->id == WILD_1 ? "wild-1" : "wild-2");
      jsonPut(o, ",\"territoryID\":null");
    }
    jsonPut(o, ",\"unit\":");
    if (c->unit >= 0)
      jsonPutStr(o, unitNames[c->unit]);
    else
      jsonPut(o, "null");
    jsonPut(o, "}");
  }
  jsonPut(o, "]");
}

void matchToJson(const Match *m, JsonOut *o)
{
  int i;

  jsonPut(o, "{\"players\":[");
  for (i = 0; i < m->nplayers; i++) {
    const Player *p = &m->players[i];

    jsonPut(o, i ? ",{\"id\":" : "{\"id\":");
    jsonPutInt(o, i);
    jsonPut(o, ",\"name\":");
    jsonPutStr(o, p->name);
    jsonPut(o, ",\"kind\":");
    jsonPutStr(o, kindNames[p->kind]);
    jsonPut(o, p->alive ? ",\"isAlive\":true,\"cards\":" : ",\"isAlive\":false,\"cards\":");
    putCards(o, p->cards, p->ncards);
    jsonPut(o, "}");
  }
  jsonPut(o, "],\"territories\":{");
  for (i = 0; i < NTERR; i++) {
    if (i)
      jsonPut(o, ",");
    jsonPutStr(o, territories[i].id);
    jsonPut(o, ":{\"owner\":");
    jsonPutInt(o, m->owner[i]);
    jsonPut(o, ",\"armies\":");
    jsonPutInt(o, m->armies[i]);
    jsonPut(o, "}");
  }
  jsonPut(o, "},\"currentPlayer\":");
  jsonPutInt(o, m->current);
  jsonPut(o, ",\"phase\":");
  jsonPutStr(o, phaseNames[m->phase]);
  jsonPut(o, ",\"reinforcements\":");
  jsonPutInt(o, m->reinforcements);
  jsonPut(o, ",\"turn\":");
  jsonPutInt(o, m->turn);
  jsonPut(o, ",\"message\":");
  jsonPutStr(o, m->message);
  jsonPut(o, ",\"deck\":");
  putCards(o, m->deck, m->ndeck);
  jsonPut(o, ",\"discard\":");
  putCards(o, m->discard, m->ndiscard);
  jsonPut(o, ",\"tradeCount\":");
  jsonPutInt(o, m->tradeCount);
  jsonPut(o, m->conquered ? ",\"conqueredThisTurn\":true" : ",\"conqueredThisTurn\":false");
  jsonPut(o, ",\"setupArmies\":[");
  for (i = 0; i < m->nplayers; i++) {
    if (i)
      jsonPut(o, ",");
    jsonPutInt(o, m->setupArmies[i]);
  }
  jsonPut(o, "]}");
}
