#include <devices/audio.h>
#include <exec/memory.h>
#include <clib/alib_protos.h>
#include <proto/exec.h>

#include "sound.h"

/* A few effects, synthesised into chip RAM at start and played on any one
   free channel. Without audio.device the game is simply silent. */

#define RATE    11025
#define PERIOD  322            /* 3546895 Hz PAL clock / RATE */

static struct MsgPort *port;
static struct IOAudio *alloc, *play;
static BYTE *samples;
static ULONG offset[SFX_COUNT], length[SFX_COUNT], total;
static int audioOpen, busy;

static ULONG seed = 0x2545F491;

static int noise(void)
{
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return (int)(seed & 0xFF) - 128;
}

/* Square-ish tone of freq Hz for len samples, fading out over its length */
static void tone(BYTE *out, ULONG len, int freq, int volume)
{
  ULONG i, half = RATE / (2 * freq);

  for (i = 0; i < len; i++) {
    int v = (int)(volume * (len - i) / len);

    out[i] = (BYTE)(((i / half) & 1) ? v : -v);
  }
}

static ULONG build(BYTE *out, int which)
{
  ULONG n = 0, k;
  int i;

  switch (which) {
    case SFX_CLICK:
      tone(out, 220, 1400, 70);
      return 220;
    case SFX_DICE:
      // Three knocks of rattle
      for (i = 0; i < 3; i++) {
        for (k = 0; k < 700; k++)
          out[n + k] = (BYTE)(noise() * (int)(700 - k) / 900);
        for (k = 700; k < 1100; k++)
          out[n + k] = 0;
        n += 1100;
      }
      return n;
    case SFX_CAPTURE: {
      static const int notes[] = { 523, 659, 784, 1047 };

      for (i = 0; i < 4; i++, n += 1300)
        tone(out + n, 1300, notes[i], 90);
      return n;
    }
    case SFX_TURN:
      tone(out, 2000, 880, 80);
      tone(out + 2000, 3000, 1175, 80);
      return 5000;
    case SFX_ERROR:
      tone(out, 3000, 110, 90);
      return 3000;
  }
  return 0;
}

void soundOpen(void)
{
  static UBYTE channels[] = { 1, 2, 4, 8 };
  static BYTE scratch[8000];
  int i;

  for (i = 0; i < SFX_COUNT; i++) {
    length[i] = build(scratch, i) & ~1UL;
    offset[i] = total;
    total += length[i];
  }
  if (!(samples = AllocMem(total, MEMF_CHIP)))
    return;
  for (i = 0; i < SFX_COUNT; i++)
    build(samples + offset[i], i);
  if (!(port = CreatePort(NULL, 0)))
    return;
  alloc = (struct IOAudio *)CreateExtIO(port, sizeof(struct IOAudio));
  play = (struct IOAudio *)CreateExtIO(port, sizeof(struct IOAudio));
  if (!alloc || !play)
    return;
  alloc->ioa_Request.io_Message.mn_Node.ln_Pri = 0;
  alloc->ioa_Request.io_Command = ADCMD_ALLOCATE;
  alloc->ioa_Request.io_Flags = ADIOF_NOWAIT;
  alloc->ioa_Data = channels;
  alloc->ioa_Length = sizeof channels;
  if (OpenDevice((UBYTE *)AUDIONAME, 0, (struct IORequest *)alloc, 0))
    return;
  CopyMem(alloc, play, sizeof(struct IOAudio));
  audioOpen = 1;
}

void soundClose(void)
{
  if (audioOpen) {
    if (busy) {
      AbortIO((struct IORequest *)play);
      WaitIO((struct IORequest *)play);
    }
    CloseDevice((struct IORequest *)alloc);
  }
  audioOpen = busy = 0;
  if (play)
    DeleteExtIO((struct IORequest *)play);
  if (alloc)
    DeleteExtIO((struct IORequest *)alloc);
  if (port)
    DeletePort(port);
  if (samples)
    FreeMem(samples, total);
  play = alloc = NULL;
  port = NULL;
  samples = NULL;
}

void sfx(int which)
{
  if (!audioOpen || which < 0 || which >= SFX_COUNT)
    return;
  if (busy) {
    AbortIO((struct IORequest *)play);
    WaitIO((struct IORequest *)play);
  }
  play->ioa_Request.io_Command = CMD_WRITE;
  play->ioa_Request.io_Flags = ADIOF_PERVOL;
  play->ioa_Data = (UBYTE *)samples + offset[which];
  play->ioa_Length = length[which];
  play->ioa_Period = PERIOD;
  play->ioa_Volume = 64;
  play->ioa_Cycles = 1;
  BeginIO((struct IORequest *)play);
  busy = 1;
}
