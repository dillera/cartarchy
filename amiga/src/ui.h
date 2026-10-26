#ifndef UI_H
#define UI_H

#include "api.h"
#include "gfx.h"

enum { M_MENU, M_SETUP, M_GAMES, M_NEWGAME, M_JOIN, M_CAMPAIGN, M_PLAY };

/* Keys that are not characters */
#define KEY_ESC   27
#define KEY_ENTER 13
#define KEY_BACK  8
#define KEY_TAB   9
#define KEY_DEL   127
#define KEY_HELP  0x100

extern int mode, quitting;
extern UWORD frame;
extern char status[96];
extern int statusPen;

/* ui.c: the frame and every screen but the game */
void uiShow(int newMode);
void uiRedraw(void);
void uiClick(int x, int y);
void uiKey(int c);
void uiMove(int x, int y);
void uiTick(void);
void setStatus(const char *s, int pen);
void drawTop(const char *top);
void drawBars(const char *top);
void drawSidePanel(void);
void pressButton(const Button *b);

/* play.c: a game on the board, against the computer or on the server */
void playCampaign(int cpus, const UBYTE *kinds);
int playRemote(const GameInfo *g);
void playDraw(void);
void playClick(int x, int y);
void playKey(int c);
void playMove(int x, int y);
void playTick(void);
void playRightClick(void);

#endif
