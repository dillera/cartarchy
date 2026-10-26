#include <string.h>

#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/gfxmacros.h>
#include <graphics/text.h>
#include <hardware/custom.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include "gfx.h"

#define DEPTH 4
#define CHIPREGS ((struct Custom *)0xDFF000)

/* From support/png2planes.py (art/logo.png): pens are the screen's */
extern const unsigned short logo_width, logo_height, logo_depth;
extern const unsigned char logo_planes[];

struct Window *win;
struct RastPort *rp;

static struct Screen *screen;
static struct RastPort backPort;
static struct BitMap back, base, logo;
static struct TextFont *font;
static struct TextAttr topaz8 = { (STRPTR)"topaz.font", 8, 0, FPF_ROMFONT };

static int ndirty;             /* board areas drawn but not yet shown */
static void mapInit(void);

static const UWORD palette[16] = {
  0x136, 0x024, 0x000, 0xFFF, 0xDCA,
  0xE33, 0x3B3, 0xEC2, 0xA5D, 0x2CC, 0xF82,
  0xCCD, 0x889, 0x445, 0xFFF, 0x9BF,
};

/* The sea darkens towards the poles: (line, colour) stops for pen 0 */
static const UWORD seaStops[][2] = {
  { 0, 0x013 }, { 40, 0x135 }, { 105, 0x258 }, { 160, 0x146 }, { 200, 0x013 },
};

static int allocPlanes(struct BitMap *bm, int w, int h)
{
  int p;

  InitBitMap(bm, DEPTH, w, h);
  for (p = 0; p < DEPTH; p++)
    if (!(bm->Planes[p] = AllocRaster(w, h)))
      return 0;
  return 1;
}

static void freePlanes(struct BitMap *bm, int w, int h)
{
  int p;

  for (p = 0; p < DEPTH; p++)
    if (bm->Planes[p])
      FreeRaster(bm->Planes[p], w, h);
  memset(bm, 0, sizeof *bm);
}

/* ByteRun1, a row of every plane at a time; planes past depth stay clear */
static void unpack(struct BitMap *bm, const unsigned char *src, int depth)
{
  UWORD y, p, done;

  for (y = 0; y < bm->Rows; y++)
    for (p = 0; p < DEPTH; p++) {
      UBYTE *dst = (UBYTE *)bm->Planes[p] + y * bm->BytesPerRow;

      if (p >= depth) {
        memset(dst, 0, bm->BytesPerRow);
        continue;
      }
      for (done = 0; done < bm->BytesPerRow;) {
        signed char n = (signed char)*src++;

        if (n >= 0) {
          CopyMem((APTR)src, dst + done, n + 1);
          src += n + 1;
          done += n + 1;
        } else if (n != -128) {
          memset(dst + done, *src++, 1 - n);
          done += 1 - n;
        }
      }
    }
}

static UWORD mix(UWORD a, UWORD b, int num, int den)
{
  UWORD out = 0;
  int shift;

  for (shift = 8; shift >= 0; shift -= 4) {
    int ca = (a >> shift) & 15, cb = (b >> shift) & 15;

    out |= (UWORD)((ca + (cb - ca) * num / den) << shift);
  }
  return out;
}

static UWORD darker(UWORD c)
{
  UWORD out = 0;
  int shift;

  for (shift = 8; shift >= 0; shift -= 4) {
    int v = ((c >> shift) & 15) - 2;

    out |= (UWORD)((v < 0 ? 0 : v) << shift);
  }
  return out;
}

/* A user copper list grades the sea (pens 0 and 1) down the screen */
static void seaGradient(void)
{
  struct UCopList *ucl = AllocMem(sizeof(struct UCopList), MEMF_PUBLIC | MEMF_CLEAR);
  UWORD last = 0xFFFF;
  int y, s = 0;

  if (!ucl)
    return;
  CINIT(ucl, 3 * SCREEN_H / 2 + 4);
  for (y = 0; y < SCREEN_H; y += 2) {
    UWORD c;

    while (y >= seaStops[s + 1][0])
      s++;
    c = mix(seaStops[s][1], seaStops[s + 1][1], y - seaStops[s][0],
            seaStops[s + 1][0] - seaStops[s][0]);
    if (c == last)
      continue;
    CWAIT(ucl, y, 0);
    CMOVE(ucl, CHIPREGS->color[0], c);
    CMOVE(ucl, CHIPREGS->color[1], darker(c));
    last = c;
  }
  CEND(ucl);
  // CloseScreen() frees it with the viewport's other copper lists
  Forbid();
  screen->ViewPort.UCopIns = ucl;
  Permit();
  RethinkDisplay();
}

int gfxOpen(void)
{
  struct NewScreen ns = {
    0, 0, SCREEN_W, SCREEN_H, DEPTH, PEN_INK, PEN_WHITE, HIRES,
    CUSTOMSCREEN | SCREENQUIET | SCREENBEHIND, &topaz8, NULL, NULL, NULL,
  };
  struct NewWindow nw = {
    0, 0, SCREEN_W, SCREEN_H, PEN_INK, PEN_WHITE,
    MOUSEBUTTONS | RAWKEY | MOUSEMOVE,
    BACKDROP | BORDERLESS | ACTIVATE | RMBTRAP | REPORTMOUSE | NOCAREREFRESH,
    NULL, NULL, NULL, NULL, NULL, SCREEN_W, SCREEN_H, SCREEN_W, SCREEN_H, CUSTOMSCREEN,
  };
  int i;

  if (!(font = OpenFont(&topaz8)))
    return 0;
  if (!allocPlanes(&back, SCREEN_W, SCREEN_H) || !allocPlanes(&base, MAP_W, MAP_H) ||
      !allocPlanes(&logo, logo_width, logo_height))
    return 0;
  unpack(&base, mapPlanes, DEPTH);
  mapInit();
  unpack(&logo, logo_planes, logo_depth);
  InitRastPort(&backPort);
  backPort.BitMap = &back;
  SetFont(&backPort, font);
  SetDrMd(&backPort, JAM1);
  rp = &backPort;
  SetRast(rp, PEN_OCEAN);

  if (!(screen = OpenScreen(&ns)))
    return 0;
  for (i = 0; i < 16; i++)
    SetRGB4(&screen->ViewPort, i, palette[i] >> 8, (palette[i] >> 4) & 15, palette[i] & 15);
  // The pointer: red, black and white show on sea and land alike
  SetRGB4(&screen->ViewPort, 17, 0xE, 0x2, 0x2);
  SetRGB4(&screen->ViewPort, 18, 0x0, 0x0, 0x0);
  SetRGB4(&screen->ViewPort, 19, 0xF, 0xF, 0xE);
  nw.Screen = screen;
  if (!(win = OpenWindow(&nw)))
    return 0;
  seaGradient();
  presentAll();
  ScreenToFront(screen);
  return 1;
}

void gfxClose(void)
{
  if (win)
    CloseWindow(win);
  if (screen)
    CloseScreen(screen);
  win = NULL;
  screen = NULL;
  WaitBlit();
  freePlanes(&back, SCREEN_W, SCREEN_H);
  freePlanes(&base, MAP_W, MAP_H);
  if (logo.Planes[0])
    freePlanes(&logo, logo_width, logo_height);
  if (font)
    CloseFont(font);
  font = NULL;
}

/* Pen 14 breathes, lighting up the territories a move can reach */
void gfxPulse(UWORD frame)
{
  int t = frame % 24, v = t < 12 ? t : 24 - t;

  if (screen)
    SetRGB4(&screen->ViewPort, PEN_PULSE, 4 + v, 4 + v, 4 + v);
}

void present(int x0, int y0, int x1, int y1)
{
  WaitBlit();
  BltBitMap(&back, x0, y0, screen->RastPort.BitMap, x0, y0, x1 - x0 + 1, y1 - y0 + 1,
            0xC0, 0xFF, NULL);
  WaitBlit();
}

void presentAll(void)
{
  WaitTOF();
  present(0, 0, SCREEN_W - 1, SCREEN_H - 1);
  ndirty = 0;
}

void box(int x0, int y0, int x1, int y1, int pen)
{
  SetAPen(rp, pen);
  RectFill(rp, x0, y0, x1, y1);
}

/* Edges two pixels wide across and one line down look alike in hires */
void bevel(int x0, int y0, int x1, int y1, int raised)
{
  int hi = raised ? PEN_WHITE : PEN_DARK, lo = raised ? PEN_DARK : PEN_WHITE;

  box(x0, y0, x1, y0, hi);
  box(x0, y0, x0 + 1, y1, hi);
  box(x0 + 1, y1, x1, y1, lo);
  box(x1 - 1, y0 + 1, x1, y1, lo);
}

void panel(int x0, int y0, int x1, int y1)
{
  box(x0, y0, x1, y1, PEN_FACE);
  bevel(x0, y0, x1, y1, 1);
}

void well(int x0, int y0, int x1, int y1)
{
  box(x0, y0, x1, y1, PEN_LIGHT);
  bevel(x0, y0, x1, y1, 0);
}

void text(int x, int y, const char *s, int pen)
{
  SetAPen(rp, pen);
  Move(rp, x, y + rp->TxBaseline);
  Text(rp, (STRPTR)s, strlen(s));
}

void textBold(int x, int y, const char *s, int pen)
{
  text(x, y, s, pen);
  text(x + 1, y, s, pen);
}

void textShadow(int x, int y, const char *s, int pen)
{
  textBold(x + 2, y + 1, s, PEN_INK);
  textBold(x, y, s, pen);
}

void textCenter(int cx, int y, const char *s, int pen)
{
  text(cx - (int)strlen(s) * 4, y, s, pen);
}

void textRight(int rx, int y, const char *s, int pen)
{
  text(rx - (int)strlen(s) * 8 + 1, y, s, pen);
}

void swatch(int x, int y, int owner)
{
  box(x, y, x + 9, y + 6, PEN_INK);
  box(x + 2, y + 1, x + 7, y + 5, owner < 0 ? PEN_LAND : PEN_PLAYER + owner);
}

void buttonDraw(const Button *b, int pressed)
{
  int x1 = b->x + b->w - 1, y1 = b->y + b->h - 1;

  box(b->x, b->y, x1, y1, pressed ? PEN_DARK : PEN_FACE);
  bevel(b->x, b->y, x1, y1, !pressed);
  textCenter(b->x + b->w / 2, b->y + (b->h - 8) / 2 + 1, b->label,
             !b->enabled ? PEN_DARK : pressed ? PEN_WHITE : PEN_INK);
}

int buttonAt(const Button *b, int n, int x, int y)
{
  int i;

  for (i = 0; i < n; i++)
    if (b[i].enabled && x >= b[i].x && x < b[i].x + b[i].w && y >= b[i].y && y < b[i].y + b[i].h)
      return i;
  return -1;
}

/* --- direct drawing into the back buffer's planes ------------------------

   With sixteen hires colours the display takes nearly every chip RAM cycle
   while the beam is on screen, so each word the CPU touches there is slow:
   the board is redrawn only where it changed, and whole words are written
   without being read first. */

static UWORD rowOff[SCREEN_H];

static void spanPlanes(int y, int x0, int x1, int penA, int penB, int checker)
{
  UWORD pat = !checker ? 0xFFFF : (y & 1) ? 0xAAAA : 0x5555;
  int w0 = x0 >> 4, w1 = x1 >> 4, p, w;
  UWORD m0 = (UWORD)(0xFFFF >> (x0 & 15)), m1 = (UWORD)(0xFFFF << (15 - (x1 & 15)));

  if (w0 == w1)
    m0 = m1 = (UWORD)(m0 & m1);
  for (p = 0; p < DEPTH; p++) {
    UWORD *row = (UWORD *)((UBYTE *)back.Planes[p] + rowOff[y]);
    UWORD bits = (UWORD)(((penA >> p) & 1 ? pat : 0) | ((penB >> p) & 1 ? ~pat : 0));

    row[w0] = (UWORD)((row[w0] & ~m0) | (bits & m0));
    for (w = w0 + 1; w < w1; w++)
      row[w] = bits;
    if (w1 > w0)
      row[w1] = (UWORD)((row[w1] & ~m1) | (bits & m1));
  }
}

/* Set the pixels of an n-bit row (top bit leftmost) at x to pen */
static void bitsPlanes(int x, int y, ULONG bits, int n, int pen)
{
  ULONG m = (bits << (32 - n)) >> (x & 15);
  int w = x >> 4, p;
  UWORD hi = (UWORD)(m >> 16), lo = (UWORD)m;

  for (p = 0; p < DEPTH; p++) {
    UWORD *row = (UWORD *)((UBYTE *)back.Planes[p] + rowOff[y]) + w;

    if ((pen >> p) & 1) {
      row[0] |= hi;
      if (lo)
        row[1] |= lo;
    } else {
      row[0] &= (UWORD)~hi;
      if (lo)
        row[1] &= (UWORD)~lo;
    }
  }
}

/* Army numbers: six pixels wide with double uprights, for hires */
static const UBYTE digits[10][7] = {
  { 0x1E, 0x33, 0x33, 0x33, 0x33, 0x33, 0x1E },
  { 0x0C, 0x1C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E },
  { 0x1E, 0x33, 0x03, 0x0E, 0x18, 0x30, 0x3F },
  { 0x1E, 0x33, 0x03, 0x0E, 0x03, 0x33, 0x1E },
  { 0x06, 0x0E, 0x1E, 0x36, 0x3F, 0x06, 0x06 },
  { 0x3F, 0x30, 0x3E, 0x03, 0x03, 0x33, 0x1E },
  { 0x0E, 0x18, 0x30, 0x3E, 0x33, 0x33, 0x1E },
  { 0x3F, 0x03, 0x06, 0x0C, 0x18, 0x18, 0x18 },
  { 0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E },
  { 0x1E, 0x33, 0x33, 0x1F, 0x03, 0x06, 0x1C },
};

typedef struct {
  WORD x0, y0, x1, y1;
} Rect;

enum { STYLE_PLAIN, STYLE_PICKED, STYLE_TARGET };

static Rect bounds[NTERR];         /* each territory's interior */
static BYTE shownOwner[NTERR];
static LONG shownArmies[NTERR];
static UBYTE shownStyle[NTERR];
static int mapValid;

#define MAX_DIRTY 8
static Rect dirty[MAX_DIRTY];

static int digitCount(long armies)
{
  return armies > 99 ? 3 : armies > 9 ? 2 : 1;
}

/* A pill, centred on the territory's spot but kept on the map */
static Rect badgeRect(int t, long armies)
{
  Rect r;
  int w = digitCount(armies) * 7 - 1 + 8;

  r.x0 = (WORD)(territories[t].badgeX + MAP_X - w / 2);
  if (r.x0 < MAP_X)
    r.x0 = MAP_X;
  if (r.x0 + w > MAP_X + MAP_W)
    r.x0 = (WORD)(MAP_X + MAP_W - w);
  r.x1 = (WORD)(r.x0 + w - 1);
  r.y0 = (WORD)(territories[t].badgeY + MAP_Y - 5);
  r.y1 = (WORD)(r.y0 + 10);
  return r;
}

static void badge(int t, long armies)
{
  static const UBYTE inset[11] = { 2, 1, 0, 0, 0, 0, 0, 0, 0, 1, 2 };
  Rect b = badgeRect(t, armies);
  int n = digitCount(armies), i, r;

  if (armies > 999)
    armies = 999;
  for (r = 0; r < 11; r++) {
    spanPlanes(b.y0 + r, b.x0 + inset[r], b.x1 - inset[r], PEN_INK, PEN_INK, 0);
    if (r > 0 && r < 10)
      spanPlanes(b.y0 + r, b.x0 + inset[r] + 2, b.x1 - 2 - inset[r], PEN_WHITE, PEN_WHITE, 0);
  }
  for (i = n - 1; i >= 0; i--, armies /= 10)
    for (r = 0; r < 7; r++)
      bitsPlanes(b.x0 + 4 + i * 7, b.y0 + 2 + r, digits[armies % 10][r], 6, PEN_INK);
}

static int overlaps(const Rect *a, const Rect *b)
{
  return a->x0 <= b->x1 && b->x0 <= a->x1 && a->y0 <= b->y1 && b->y0 <= a->y1;
}

static void addDirty(Rect r)
{
  int i, merged = 1;

  if (r.x0 < MAP_X)
    r.x0 = MAP_X;
  if (r.y0 < MAP_Y)
    r.y0 = MAP_Y;
  if (r.x1 > MAP_X + MAP_W - 1)
    r.x1 = MAP_X + MAP_W - 1;
  if (r.y1 > MAP_Y + MAP_H - 1)
    r.y1 = MAP_Y + MAP_H - 1;
  if (r.x0 > r.x1 || r.y0 > r.y1)
    return;
  // Overlapping areas become one, so nothing is drawn twice
  while (merged) {
    merged = 0;
    for (i = 0; i < ndirty; i++)
      if (overlaps(&dirty[i], &r)) {
        if (dirty[i].x0 < r.x0) r.x0 = dirty[i].x0;
        if (dirty[i].y0 < r.y0) r.y0 = dirty[i].y0;
        if (dirty[i].x1 > r.x1) r.x1 = dirty[i].x1;
        if (dirty[i].y1 > r.y1) r.y1 = dirty[i].y1;
        dirty[i] = dirty[--ndirty];
        merged = 1;
        break;
      }
  }
  if (ndirty == MAX_DIRTY) {
    for (i = 0; i < ndirty; i++) {
      if (dirty[i].x0 < r.x0) r.x0 = dirty[i].x0;
      if (dirty[i].y0 < r.y0) r.y0 = dirty[i].y0;
      if (dirty[i].x1 > r.x1) r.x1 = dirty[i].x1;
      if (dirty[i].y1 > r.y1) r.y1 = dirty[i].y1;
    }
    ndirty = 0;
  }
  dirty[ndirty++] = r;
}

/* The board inside r from the base map and what is shown */
static void renderRect(const Rect *r)
{
  int t, s;

  WaitBlit();
  BltBitMap(&base, r->x0 - MAP_X, r->y0 - MAP_Y, &back, r->x0, r->y0,
            r->x1 - r->x0 + 1, r->y1 - r->y0 + 1, 0xC0, 0xFF, NULL);
  WaitBlit();
  for (t = 0; t < NTERR; t++) {
    const Territory *tr = &territories[t];
    int pen = shownOwner[t] < 0 ? PEN_LAND : PEN_PLAYER + shownOwner[t];
    int other = shownStyle[t] == STYLE_PICKED ? PEN_WHITE : shownStyle[t] == STYLE_TARGET ? PEN_PULSE : pen;

    if (!overlaps(&bounds[t], r))
      continue;
    for (s = tr->firstSpan; s < tr->firstSpan + tr->nspans; s++) {
      int y = spans[s].y + MAP_Y, x0 = spans[s].x0 + MAP_X, x1 = spans[s].x1 + MAP_X;

      if (y < r->y0 || y > r->y1)
        continue;
      if (x0 < r->x0)
        x0 = r->x0;
      if (x1 > r->x1)
        x1 = r->x1;
      if (x0 <= x1)
        spanPlanes(y, x0, x1, pen, other, shownStyle[t] != STYLE_PLAIN);
    }
  }
  for (t = 0; t < NTERR; t++)
    if (shownArmies[t] > 0) {
      Rect b = badgeRect(t, shownArmies[t]);

      if (overlaps(&b, r))
        badge(t, shownArmies[t]);
    }
}

static void mapInit(void)
{
  int t, s, y;

  for (y = 0; y < SCREEN_H; y++)
    rowOff[y] = (UWORD)(y * back.BytesPerRow);
  for (t = 0; t < NTERR; t++) {
    Rect *b = &bounds[t];

    b->x0 = b->y0 = 0x7FFF;
    b->x1 = b->y1 = -1;
    for (s = territories[t].firstSpan; s < territories[t].firstSpan + territories[t].nspans; s++) {
      if (spans[s].x0 + MAP_X < b->x0) b->x0 = (WORD)(spans[s].x0 + MAP_X);
      if (spans[s].x1 + MAP_X > b->x1) b->x1 = (WORD)(spans[s].x1 + MAP_X);
      if (spans[s].y + MAP_Y < b->y0) b->y0 = (WORD)(spans[s].y + MAP_Y);
      if (spans[s].y + MAP_Y > b->y1) b->y1 = (WORD)(spans[s].y + MAP_Y);
    }
  }
}

void mapInvalidate(void)
{
  mapValid = 0;
}

void mapDraw(const BYTE *owner, const LONG *armies, int sel, const Match *targetsOf)
{
  static const Rect whole = { MAP_X, MAP_Y, MAP_X + MAP_W - 1, MAP_Y + MAP_H - 1 };
  int t, i;

  for (t = 0; t < NTERR; t++) {
    long a = armies ? armies[t] : 0;
    UBYTE style = t == sel ? STYLE_PICKED : targetsOf && isTarget(targetsOf, t) ? STYLE_TARGET : STYLE_PLAIN;

    if (mapValid && owner[t] == shownOwner[t] && a == shownArmies[t] && style == shownStyle[t])
      continue;
    if (mapValid) {
      addDirty(bounds[t]);
      if (shownArmies[t] > 0)
        addDirty(badgeRect(t, shownArmies[t]));
      if (a > 0)
        addDirty(badgeRect(t, a));
    }
    shownOwner[t] = owner[t];
    shownArmies[t] = a;
    shownStyle[t] = style;
  }
  if (!mapValid) {
    ndirty = 0;
    addDirty(whole);
    mapValid = 1;
  }
  for (i = 0; i < ndirty; i++)
    renderRect(&dirty[i]);
}

/* The board under a dialog that went away */
void mapRestore(int x0, int y0, int x1, int y1)
{
  Rect r;

  r.x0 = (WORD)x0;
  r.y0 = (WORD)y0;
  r.x1 = (WORD)x1;
  r.y1 = (WORD)y1;
  addDirty(r);
  if (mapValid)
    renderRect(&dirty[ndirty - 1]);
}

void mapPresent(void)
{
  int i;

  if (ndirty)
    WaitTOF();
  for (i = 0; i < ndirty; i++)
    present(dirty[i].x0, dirty[i].y0, dirty[i].x1, dirty[i].y1);
  ndirty = 0;
}

int mapBounds(int t, int r[4])
{
  r[0] = bounds[t].x0;
  r[1] = bounds[t].y0;
  r[2] = bounds[t].x1;
  r[3] = bounds[t].y1;
  return 1;
}

int mapHit(int x, int y)
{
  int t, s, dx, dy;

  if (x < MAP_X || x >= MAP_X + MAP_W || y < MAP_Y || y >= MAP_Y + MAP_H)
    return -1;
  for (t = NTERR - 1; t >= 0; t--)
    if (shownArmies[t] > 0) {
      Rect b = badgeRect(t, shownArmies[t]);

      if (x >= b.x0 && x <= b.x1 && y >= b.y0 && y <= b.y1)
        return t;
    }
  x -= MAP_X;
  y -= MAP_Y;
  // A click on a border counts for the nearest territory
  for (dy = 0; dy <= 2; dy++)
    for (dx = 0; dx <= 4; dx++)
      for (t = 0; t < NTERR; t++)
        for (s = territories[t].firstSpan; s < territories[t].firstSpan + territories[t].nspans; s++) {
          const Span *sp = &spans[s];

          if ((sp->y == y - dy || sp->y == y + dy) && sp->x0 <= x + dx && sp->x1 >= x - dx)
            return t;
        }
  return -1;
}

/* --- dice and logo ------------------------------------------------------- */

void die(int x, int y, int value, int attacker)
{
  static const UBYTE pips[7][7] = {
    { 0 }, { 1, 4 }, { 2, 0, 8 }, { 3, 0, 4, 8 }, { 4, 0, 2, 6, 8 }, { 5, 0, 2, 4, 6, 8 },
    { 6, 0, 2, 3, 5, 6, 8 },
  };
  int face = attacker ? PEN_PLAYER : PEN_WHITE, pip = attacker ? PEN_WHITE : PEN_INK, i;

  box(x + 1, y, x + 16, y + 9, PEN_INK);
  box(x, y + 1, x + 17, y + 8, PEN_INK);
  box(x + 2, y + 1, x + 15, y + 8, face);
  if (value < 1 || value > 6)
    return;
  for (i = 1; i <= pips[value][0]; i++) {
    int k = pips[value][i], px = x + 4 + (k % 3) * 4, py = y + 2 + (k / 3) * 2;

    box(px, py, px + 1, py, pip);
  }
}

int logoWidth(void)
{
  return logo_width;
}

int logoHeight(void)
{
  return logo_height;
}

void logoDraw(int x, int y)
{
  WaitBlit();
  BltBitMap(&logo, 0, 0, &back, x, y, logo_width, logo_height, 0xC0, 0xFF, NULL);
  WaitBlit();
}
