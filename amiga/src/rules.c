#include <stdio.h>
#include <string.h>

#include "cartarchy.h"

/* The labels the other clients write and read (SavedMatch's raw values) */
const char *const phaseNames[] = {
  "CLAIM TERRITORIES", "PLACE ARMIES", "REINFORCE", "ATTACK", "FORTIFY", "GAME OVER",
};
const char *const kindNames[] = { "Human", "Cautious AI", "Balanced AI", "Aggressive AI" };
const char *const unitNames[] = { "infantry", "cavalry", "artillery" };

WORD selected = -1;
UBYTE diceA[3], diceD[2], nDiceA, nDiceD;

static ULONG seed = 12345;

void rulesSeed(ULONG s)
{
  seed = s ? s : 12345;
}

int rnd(int n)
{
  seed = seed * 1103515245UL + 12345UL;
  return (int)((seed >> 16) % (ULONG)n);
}

static void shuffle(Card *cards, int n)
{
  int i;

  for (i = n - 1; i > 0; i--) {
    int j = rnd(i + 1);
    Card t = cards[i];

    cards[i] = cards[j];
    cards[j] = t;
  }
}

int initialArmies(int players)
{
  static const UBYTE counts[] = { 40, 35, 30, 25 };

  return players >= 2 && players <= 5 ? counts[players - 2] : 20;
}

int adjacent(int a, int b)
{
  int i;

  for (i = 0; i < territories[a].nneighbors; i++)
    if (territories[a].neighbors[i] == b)
      return 1;
  return 0;
}

int ownedCount(const Match *m, int owner)
{
  int t, n = 0;

  for (t = 0; t < NTERR; t++)
    n += m->owner[t] == owner;
  return n;
}

long armyCount(const Match *m, int owner)
{
  long n = 0;
  int t;

  for (t = 0; t < NTERR; t++)
    if (m->owner[t] == owner)
      n += m->armies[t];
  return n;
}

int reinforcementCount(const Match *m, int owner)
{
  int total = ownedCount(m, owner) / 3, c, t;

  if (total < 3)
    total = 3;
  for (c = 0; c < NCONTINENTS; c++) {
    for (t = 0; t < NTERR; t++)
      if (territories[t].continent == c && m->owner[t] != owner)
        break;
    if (t == NTERR)
      total += continents[c].bonus;
  }
  return total;
}

int tradeValue(int n)
{
  static const UBYTE opening[] = { 4, 6, 8, 10, 12, 15 };

  return n < 6 ? opening[n] : 15 + (n - 6 + 1) * 5;
}

static int validSet(const Card *a, const Card *b, const Card *c)
{
  if (a->unit < 0 || b->unit < 0 || c->unit < 0)
    return 1;
  return (a->unit == b->unit && b->unit == c->unit) ||
         (a->unit != b->unit && b->unit != c->unit && a->unit != c->unit);
}

int findSet(const Player *p, int idx[3])
{
  int a, b, c;

  for (a = 0; a + 2 < p->ncards; a++)
    for (b = a + 1; b + 1 < p->ncards; b++)
      for (c = b + 1; c < p->ncards; c++)
        if (validSet(&p->cards[a], &p->cards[b], &p->cards[c])) {
          idx[0] = a;
          idx[1] = b;
          idx[2] = c;
          return 1;
        }
  return 0;
}

void newMatch(Match *m, int n, const char names[][NAME_LEN], const UBYTE *kinds)
{
  int i;

  if (n < 2)
    n = 2;
  if (n > MAXPLAYERS)
    n = MAXPLAYERS;
  memset(m, 0, sizeof *m);
  m->nplayers = (UBYTE)n;
  for (i = 0; i < n; i++) {
    strncpy(m->players[i].name, names[i], NAME_LEN - 1);
    m->players[i].kind = kinds[i];
    m->players[i].alive = 1;
    m->setupArmies[i] = (WORD)initialArmies(n);
  }
  for (i = 0; i < NTERR; i++)
    m->owner[i] = -1;
  // Territory cards cycle through the units in map order, then two wilds
  for (i = 0; i < NCARDS; i++) {
    m->deck[i].id = (UBYTE)i;
    m->deck[i].unit = (BYTE)(i < NTERR ? i % 3 : UNIT_WILD);
  }
  m->ndeck = NCARDS;
  shuffle(m->deck, NCARDS);
  m->phase = PH_CLAIM;
  strcpy(m->message, "Claim one unoccupied territory.");
  selected = -1;
}

int isTarget(const Match *m, int t)
{
  if (selected < 0 || !adjacent(selected, t))
    return 0;
  if (m->phase == PH_ATTACK)
    return m->owner[t] != m->current;
  if (m->phase == PH_FORTIFY)
    return m->owner[t] == m->current;
  return 0;
}

static void advanceSetupTurn(Match *m)
{
  int t, i;

  if (m->phase == PH_CLAIM) {
    for (t = 0; t < NTERR && m->owner[t] >= 0; t++)
      ;
    if (t == NTERR)
      m->phase = PH_DEPLOY;
  }
  if (m->phase == PH_DEPLOY) {
    for (i = 0; i < m->nplayers && m->setupArmies[i] == 0; i++)
      ;
    if (i == m->nplayers) {
      m->current = 0;
      m->phase = PH_REINFORCE;
      m->turn = 1;
      m->reinforcements = (WORD)reinforcementCount(m, 0);
      strcpy(m->message, "Setup complete. Deploy reinforcements.");
      return;
    }
  }
  do
    m->current = (UBYTE)((m->current + 1) % m->nplayers);
  while (m->phase == PH_DEPLOY && m->setupArmies[m->current] == 0);
  if (m->phase == PH_CLAIM)
    sprintf(m->message, "%s claims a territory.", m->players[m->current].name);
  else
    sprintf(m->message, "%s places an army. %d remain.", m->players[m->current].name,
            m->setupArmies[m->current]);
}

static void endTurn(Match *m)
{
  Player *p = &m->players[m->current];

  selected = -1;
  if (m->conquered) {
    if (m->ndeck == 0) {
      memcpy(m->deck, m->discard, m->ndiscard * sizeof(Card));
      m->ndeck = m->ndiscard;
      m->ndiscard = 0;
      shuffle(m->deck, m->ndeck);
    }
    if (m->ndeck > 0 && p->ncards < NCARDS)
      p->cards[p->ncards++] = m->deck[--m->ndeck];
  }
  do
    m->current = (UBYTE)((m->current + 1) % m->nplayers);
  while (!m->players[m->current].alive);
  if (m->current == 0)
    m->turn++;
  m->phase = PH_REINFORCE;
  m->reinforcements = (WORD)reinforcementCount(m, m->current);
  m->conquered = 0;
  sprintf(m->message, "%s reinforces.", m->players[m->current].name);
}

static void eliminateAndCheckVictory(Match *m)
{
  Player *winner = &m->players[m->current];
  int i, alive = 0, last = 0;

  for (i = 0; i < m->nplayers; i++) {
    Player *p = &m->players[i];

    if (!p->alive || ownedCount(m, i) > 0)
      continue;
    if (i != m->current) {
      int k;

      for (k = 0; k < p->ncards && winner->ncards < NCARDS; k++)
        winner->cards[winner->ncards++] = p->cards[k];
      p->ncards = 0;
    }
    p->alive = 0;
  }
  for (i = 0; i < m->nplayers; i++)
    if (m->players[i].alive) {
      alive++;
      last = i;
    }
  if (alive == 1) {
    m->phase = PH_OVER;
    sprintf(m->message, "%s conquers the world.", m->players[last].name);
  }
}

static void sortDown(UBYTE *d, int n)
{
  int i, j;

  for (i = 0; i < n; i++)
    for (j = i + 1; j < n; j++)
      if (d[j] > d[i]) {
        UBYTE t = d[i];

        d[i] = d[j];
        d[j] = t;
      }
}

static void attack(Match *m, int src, int dst)
{
  long a = m->armies[src] - 1, d = m->armies[dst];
  int i, lossA = 0, lossD = 0;
  char *s;

  nDiceA = (UBYTE)(a < 3 ? a : 3);
  nDiceD = (UBYTE)(d < 2 ? d : 2);
  for (i = 0; i < nDiceA; i++)
    diceA[i] = (UBYTE)(1 + rnd(6));
  for (i = 0; i < nDiceD; i++)
    diceD[i] = (UBYTE)(1 + rnd(6));
  sortDown(diceA, nDiceA);
  sortDown(diceD, nDiceD);
  for (i = 0; i < nDiceA && i < nDiceD; i++) {
    if (diceA[i] > diceD[i])
      lossD++;
    else
      lossA++;
  }
  m->armies[src] -= lossA;
  m->armies[dst] -= lossD;

  s = m->message + sprintf(m->message, "Rolled ");
  for (i = 0; i < nDiceA; i++)
    s += sprintf(s, i ? ",%d" : "%d", diceA[i]);
  s += sprintf(s, " vs ");
  for (i = 0; i < nDiceD; i++)
    s += sprintf(s, i ? ",%d" : "%d", diceD[i]);
  strcpy(s, ".");

  if (m->armies[dst] == 0) {
    long move = m->armies[src] - 1 < nDiceA ? m->armies[src] - 1 : nDiceA;

    m->armies[src] -= move;
    m->owner[dst] = (BYTE)m->current;
    m->armies[dst] = move;
    m->conquered = 1;
    strcat(m->message, " Territory captured!");
    eliminateAndCheckVictory(m);
  }
}

static void performTrade(Match *m)
{
  Player *p = &m->players[m->current];
  int idx[3], i, k;

  if (!findSet(p, idx))
    return;
  m->reinforcements += tradeValue(m->tradeCount);
  m->tradeCount++;
  for (i = 0; i < 3; i++) {
    int t = p->cards[idx[i]].id;

    if (t < NTERR && m->owner[t] == m->current) {
      m->armies[t] += 2;
      break;
    }
  }
  for (i = 2; i >= 0; i--) {
    if (m->ndiscard < NCARDS)
      m->discard[m->ndiscard++] = p->cards[idx[i]];
    for (k = idx[i]; k + 1 < p->ncards; k++)
      p->cards[k] = p->cards[k + 1];
    p->ncards--;
  }
}

int canTrade(const Match *m)
{
  int idx[3];

  return m->phase == PH_REINFORCE && findSet(&m->players[m->current], idx);
}

const char *actTap(Match *m, int t)
{
  int cur = m->current;

  if (m->players[cur].kind != K_HUMAN)
    return NULL;
  switch (m->phase) {
    case PH_CLAIM:
      if (m->owner[t] != -1)
        return NULL;
      m->owner[t] = (BYTE)cur;
      m->armies[t] = 1;
      m->setupArmies[cur]--;
      advanceSetupTurn(m);
      break;
    case PH_DEPLOY:
      if (m->owner[t] != cur || m->setupArmies[cur] <= 0)
        return NULL;
      m->armies[t]++;
      m->setupArmies[cur]--;
      advanceSetupTurn(m);
      break;
    case PH_REINFORCE:
      if (m->players[cur].ncards >= 5 && canTrade(m)) {
        strcpy(m->message, "You hold five cards. Trade a set before deploying.");
        return "state.updated";
      }
      if (m->owner[t] != cur || m->reinforcements <= 0)
        return NULL;
      m->armies[t]++;
      if (--m->reinforcements == 0) {
        strcpy(m->message, "Reinforcement complete. Begin attacks.");
        m->phase = PH_ATTACK;
      } else {
        sprintf(m->message, "%d armies remain.", m->reinforcements);
      }
      break;
    case PH_ATTACK:
      // Picking a territory is ours alone; only the roll goes to the server
      if (m->owner[t] == cur && m->armies[t] > 1) {
        selected = (WORD)t;
        strcpy(m->message, "Choose an adjacent enemy.");
        return NULL;
      }
      if (selected < 0 || !adjacent(selected, t) || m->owner[t] == cur ||
          m->armies[selected] < 2)
        return NULL;
      attack(m, selected, t);
      if (m->armies[selected] < 2)
        selected = -1;
      break;
    case PH_FORTIFY:
      if (selected >= 0 && m->owner[t] == cur && adjacent(selected, t)) {
        m->armies[t] += m->armies[selected] - 1;
        m->armies[selected] = 1;
        endTurn(m);
        break;
      }
      if (m->owner[t] == cur && m->armies[t] > 1) {
        selected = (WORD)t;
        strcpy(m->message, "Choose a connected territory.");
      }
      return NULL;
    default:
      return NULL;
  }
  return "state.updated";
}

const char *actNextPhase(Match *m)
{
  if (m->players[m->current].kind != K_HUMAN)
    return NULL;
  if (m->phase == PH_ATTACK) {
    m->phase = PH_FORTIFY;
    selected = -1;
    strcpy(m->message, "Move one army, or end your turn.");
    return "state.updated";
  }
  if (m->phase == PH_FORTIFY) {
    endTurn(m);
    return "turn.completed";
  }
  return NULL;
}

const char *actTrade(Match *m)
{
  if (m->players[m->current].kind != K_HUMAN || !canTrade(m))
    return NULL;
  performTrade(m);
  sprintf(m->message, "Card set traded. Deploy %d armies.", m->reinforcements);
  return "state.updated";
}

const char *actRandomize(Match *m)
{
  BYTE order[NTERR];
  int i, p, start = initialArmies(m->nplayers);

  if (m->phase != PH_CLAIM && m->phase != PH_DEPLOY)
    return NULL;
  for (i = 0; i < NTERR; i++)
    order[i] = (BYTE)i;
  for (i = NTERR - 1; i > 0; i--) {
    int j = rnd(i + 1);
    BYTE t = order[i];

    order[i] = order[j];
    order[j] = t;
  }
  for (i = 0; i < NTERR; i++) {
    m->owner[(int)order[i]] = (BYTE)(i % m->nplayers);
    m->armies[(int)order[i]] = 1;
  }
  for (p = 0; p < m->nplayers; p++) {
    BYTE mine[NTERR];
    int n = 0, k;

    for (i = 0; i < NTERR; i++)
      if (m->owner[(int)order[i]] == p)
        mine[n++] = order[i];
    for (k = n; k < start; k++)
      m->armies[(int)mine[rnd(n)]]++;
    m->setupArmies[p] = 0;
  }
  m->current = 0;
  m->phase = PH_REINFORCE;
  m->turn = 1;
  m->reinforcements = (WORD)reinforcementCount(m, 0);
  m->conquered = 0;
  sprintf(m->message, "Armies placed randomly. %s reinforces.", m->players[0].name);
  selected = -1;
  return "state.updated";
}

static int isBorder(const Match *m, int t)
{
  int i;

  for (i = 0; i < territories[t].nneighbors; i++)
    if (m->owner[territories[t].neighbors[i]] != m->owner[t])
      return 1;
  return 0;
}

/* A random territory of owner's, on a border if one is: picks the k-th */
static int pickOwned(const Match *m, int owner, int bordersOnly)
{
  int t, n = 0, k;

  for (t = 0; t < NTERR; t++)
    if (m->owner[t] == owner && (!bordersOnly || isBorder(m, t)))
      n++;
  if (n == 0)
    return bordersOnly ? pickOwned(m, owner, 0) : -1;
  k = rnd(n);
  for (t = 0; t < NTERR; t++)
    if (m->owner[t] == owner && (!bordersOnly || isBorder(m, t)) && k-- == 0)
      return t;
  return -1;
}

int aiStep(Match *m)
{
  int owner = m->current, t, best = -1, src = -1, dst = -1, margin;
  const Player *p = &m->players[owner];

  if (m->phase == PH_OVER || p->kind == K_HUMAN)
    return 0;
  switch (m->phase) {
    case PH_CLAIM:
      t = pickOwned(m, -1, 0);
      if (t >= 0) {
        m->owner[t] = (BYTE)owner;
        m->armies[t] = 1;
        m->setupArmies[owner]--;
      }
      advanceSetupTurn(m);
      return 1;
    case PH_DEPLOY:
      t = pickOwned(m, owner, 1);
      if (t >= 0) {
        m->armies[t]++;
        m->setupArmies[owner]--;
      }
      advanceSetupTurn(m);
      return 1;
    case PH_REINFORCE:
      while (canTrade(m))
        performTrade(m);
      // Pile everything on the strongest border
      for (t = 0; t < NTERR; t++)
        if (m->owner[t] == owner && isBorder(m, t) && (best < 0 || m->armies[t] > m->armies[best]))
          best = t;
      if (best < 0)
        best = pickOwned(m, owner, 0);
      m->armies[best] += m->reinforcements;
      m->reinforcements = 0;
      m->phase = PH_ATTACK;
      sprintf(m->message, "%s reinforces.", p->name);
      return 1;
    default:
      break;
  }
  margin = p->kind == K_CAUTIOUS ? 3 : 1;
  for (t = 0; t < NTERR; t++) {
    int i;

    if (m->owner[t] != owner)
      continue;
    for (i = 0; i < territories[t].nneighbors; i++) {
      int n = territories[t].neighbors[i];

      if (m->owner[n] != owner && m->armies[t] > 1 && m->armies[t] >= m->armies[n] + margin &&
          (src < 0 || m->armies[t] > m->armies[src])) {
        src = t;
        dst = n;
      }
    }
  }
  if (m->phase == PH_ATTACK && src >= 0) {
    attack(m, src, dst);
  } else {
    m->phase = PH_FORTIFY;
    endTurn(m);
  }
  return 1;
}
