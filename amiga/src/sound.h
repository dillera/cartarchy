#ifndef SOUND_H
#define SOUND_H

enum { SFX_CLICK, SFX_DICE, SFX_CAPTURE, SFX_TURN, SFX_ERROR, SFX_COUNT };

void soundOpen(void);
void soundClose(void);
void sfx(int which);

#endif
