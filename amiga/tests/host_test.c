/* Host checks for the portable sources: rules, state JSON and the JSON
   reader. Build and run with `make -C amiga test`. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartarchy.h"

static char buf[65536], buf2[65536];
static int failures;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void cardsAccounted(const Match *m)
{
  int seen[NCARDS] = { 0 }, i, k, total = 0;

  for (i = 0; i < m->nplayers; i++)
    for (k = 0; k < m->players[i].ncards; k++, total++)
      seen[m->players[i].cards[k].id]++;
  for (k = 0; k < m->ndeck; k++, total++)
    seen[m->deck[k].id]++;
  for (k = 0; k < m->ndiscard; k++, total++)
    seen[m->discard[k].id]++;
  CHECK(total == NCARDS);
  for (k = 0; k < NCARDS; k++)
    CHECK(seen[k] == 1);
}

static void roundTrip(const Match *m)
{
  JsonOut o;
  Match back;

  jsonInit(&o, buf, sizeof buf);
  matchToJson(m, &o);
  CHECK(!o.overflow);
  CHECK(matchFromJson(&back, buf, buf + o.len));
  jsonInit(&o, buf2, sizeof buf2);
  matchToJson(&back, &o);
  CHECK(strcmp(buf, buf2) == 0);
}

static void aiGames(int games)
{
  static const char names[MAXPLAYERS][NAME_LEN] = { "A", "B", "C", "D", "E", "F" };
  int g, finished = 0;

  for (g = 0; g < games; g++) {
    UBYTE kinds[MAXPLAYERS];
    Match m;
    int n = 2 + g % 5, i, steps = 0;

    for (i = 0; i < n; i++)
      kinds[i] = (UBYTE)(K_CAUTIOUS + (g + i) % 3);
    newMatch(&m, n, names, kinds);
    while (aiStep(&m) && steps < 20000) {
      steps++;
      if (steps % 97 == 0) {
        cardsAccounted(&m);
        roundTrip(&m);
      }
    }
    if (m.phase == PH_OVER)
      finished++;
    cardsAccounted(&m);
    for (i = 0; i < NTERR; i++)
      CHECK(m.owner[i] >= 0 && m.armies[i] >= 1);
  }
  // Computer players alone can stall with two stacks face to face (as in the
  // iOS app); a human always breaks it, so only the bookkeeping is checked
  printf("ai: %d of %d games won outright\n", finished, games);
}

/* A human seat clicking at random: every move must keep the state sound */
static void humanClicks(void)
{
  static const char names[MAXPLAYERS][NAME_LEN] = { "ME", "CPU 1", "CPU 2" };
  static const UBYTE kinds[] = { K_HUMAN, K_BALANCED, K_AGGRESSIVE };
  Match m;
  int i, moves = 0;

  newMatch(&m, 3, names, kinds);
  for (i = 0; i < 200000 && m.phase != PH_OVER; i++) {
    const char *kind;

    if (m.players[m.current].kind != K_HUMAN) {
      aiStep(&m);
      continue;
    }
    switch (rnd(12)) {
      case 0:
        kind = actNextPhase(&m);
        break;
      case 1:
        kind = actTrade(&m);
        break;
      default:
        kind = actTap(&m, rnd(NTERR));
        break;
    }
    if (kind) {
      moves++;
      CHECK(strcmp(kind, "state.updated") == 0 || strcmp(kind, "turn.completed") == 0);
    }
    if (i % 101 == 0) {
      cardsAccounted(&m);
      roundTrip(&m);
    }
  }
  printf("human: %d moves, phase %s\n", moves, phaseNames[m.phase]);
  CHECK(moves > 100);
}

static void reader(void)
{
  static const char doc[] =
    "{\"id\":\"g1\",\"name\":\"Caf\\u00e9 \\\"War\\\"\",\"playerIds\":[\"a\",\"b\"],"
    "\"version\":17,\"players\":[{\"id\":\"a\",\"name\":\"Juno\"}],\"state\":{\"x\":[1,2,{\"y\":\"}\"}]}}";
  const char *end = doc + sizeof doc - 1, *v;
  char s[64];

  CHECK(jsonInt(jsonGet(doc, end, "version"), end, -1) == 17);
  CHECK(jsonStr(jsonGet(doc, end, "name"), end, s, sizeof s) == 10);
  CHECK(strcmp(s, "Caf\xe9 \"War\"") == 0);
  v = jsonFirst(jsonGet(doc, end, "playerIds"), end);
  CHECK(v && jsonStr(v, end, s, sizeof s) == 1 && s[0] == 'a');
  v = jsonNext(v, end);
  CHECK(v && jsonStr(v, end, s, sizeof s) == 1 && s[0] == 'b');
  CHECK(!jsonNext(v, end));
  CHECK(jsonGet(doc, end, "state") != NULL);
  CHECK(jsonGet(doc, end, "missing") == NULL);
  // Cut short inside "state": the fields before it still read
  end = strstr(doc, "{\"y\"");
  CHECK(jsonInt(jsonGet(doc, end, "version"), end, -1) == 17);
  CHECK(!jsonSkip(jsonGet(doc, end, "state"), end));
}

int main(int argc, char **argv)
{
  rulesSeed(4242);
  reader();
  aiGames(60);
  humanClicks();
  if (argc > 1) {
    // A mid-game state for the Swift decoder check
    static const char names[MAXPLAYERS][NAME_LEN] = { "JUNO", "ODIN", "FREYA" };
    static const UBYTE kinds[] = { K_HUMAN, K_HUMAN, K_HUMAN };
    Match m;
    JsonOut o;
    FILE *f;
    int i;

    newMatch(&m, 3, names, kinds);
    actRandomize(&m);
    for (i = 0; i < 3; i++)
      m.players[i].kind = K_CAUTIOUS;
    for (i = 0; i < 300 && aiStep(&m); i++)
      ;
    for (i = 0; i < 3; i++)
      m.players[i].kind = K_HUMAN;
    jsonInit(&o, buf, sizeof buf);
    matchToJson(&m, &o);
    f = fopen(argv[1], "w");
    fputs(buf, f);
    fclose(f);
  }
  printf(failures ? "%d failures\n" : "all passed\n", failures);
  return failures != 0;
}
