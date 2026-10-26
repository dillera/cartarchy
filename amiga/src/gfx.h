#ifndef GFX_H
#define GFX_H

#include <exec/types.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>

#include "cartarchy.h"

/* A 640x200 hires screen, 16 colours. Every frame is drawn into a back
   buffer (rp) and shown with present(). */
#define SCREEN_W 640
#define SCREEN_H 200
#define MAP_X    0
#define MAP_Y    12
#define PANEL_X  500
#define PANEL_R  639
#define BAR_Y    186

enum {
  PEN_OCEAN, PEN_SHADOW, PEN_INK, PEN_WHITE, PEN_LAND,
  PEN_PLAYER,                  /* 5-10: a colour per seat */
  PEN_LIGHT = 11, PEN_FACE, PEN_DARK, PEN_PULSE, PEN_ROUTE
};

typedef struct {
  WORD x, y, w, h;
  UBYTE id, key, enabled;
  const char *label;
} Button;

extern struct Window *win;
extern struct RastPort *rp;

int gfxOpen(void);
void gfxClose(void);
void gfxPulse(UWORD frame);
void present(int x0, int y0, int x1, int y1);
void presentAll(void);

void box(int x0, int y0, int x1, int y1, int pen);
void bevel(int x0, int y0, int x1, int y1, int raised);
void panel(int x0, int y0, int x1, int y1);
void well(int x0, int y0, int x1, int y1);
void text(int x, int y, const char *s, int pen);
void textBold(int x, int y, const char *s, int pen);
void textShadow(int x, int y, const char *s, int pen);
void textCenter(int cx, int y, const char *s, int pen);
void textRight(int rx, int y, const char *s, int pen);
void swatch(int x, int y, int owner);
void buttonDraw(const Button *b, int pressed);
int buttonAt(const Button *b, int n, int x, int y);
void die(int x, int y, int value, int attacker);
void logoDraw(int x, int y);
int logoWidth(void);
int logoHeight(void);

/* The board: owners per territory (-1 unclaimed), armies or NULL for no
   badges, the territory picked and whether to light up its targets. Only
   what changed since the last call is drawn, and mapPresent() shows it;
   after mapInvalidate() it is all drawn again. */
void mapDraw(const BYTE *owner, const LONG *armies, int selected, const Match *targetsOf);
void mapInvalidate(void);
void mapRestore(int x0, int y0, int x1, int y1);
void mapPresent(void);
int mapBounds(int t, int r[4]);
int mapHit(int x, int y);

#endif
