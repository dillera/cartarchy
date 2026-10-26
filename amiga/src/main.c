#include <stdlib.h>
#include <string.h>

#include <devices/conunit.h>
#include <devices/inputevent.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <exec/io.h>
#include <intuition/intuition.h>
#include <clib/alib_protos.h>
#include <proto/console.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include "net.h"
#include "sound.h"
#include "ui.h"

/* Version string for the Version command */
static const char version[] __attribute__((used)) = "$VER: Cartarchy 0.1 (25.10.2026)";

struct Device *ConsoleDevice;
static struct IOStdReq consoleReq;
static struct MsgPort *timerPort;
static struct timerequest *timerReq;
static int timerOpen, timerBusy;

static void startTimer(void)
{
  timerReq->tr_node.io_Command = TR_ADDREQUEST;
  timerReq->tr_time.tv_secs = 0;
  timerReq->tr_time.tv_micro = 20000;
  SendIO((struct IORequest *)timerReq);
  timerBusy = 1;
}

static void shutdown(void)
{
  if (timerBusy) {
    AbortIO((struct IORequest *)timerReq);
    WaitIO((struct IORequest *)timerReq);
  }
  if (timerOpen)
    CloseDevice((struct IORequest *)timerReq);
  if (timerReq)
    DeleteExtIO((struct IORequest *)timerReq);
  if (timerPort)
    DeletePort(timerPort);
  if (ConsoleDevice)
    CloseDevice((struct IORequest *)&consoleReq);
  soundClose();
  gfxClose();
  netClose();
  apiFree();
}

static void fail(const char *why)
{
  struct IntuiText line = { 0, 1, JAM2, 12, 8, NULL, NULL, NULL };
  struct IntuiText ok = { 0, 1, JAM2, 6, 3, NULL, (UBYTE *)"OK", NULL };

  shutdown();
  line.IText = (UBYTE *)why;
  AutoRequest(NULL, &line, NULL, &ok, 0, 0, 360, 60);
  exit(20);
}

/* Kickstart 1.3 drops keys that map to no single character from
   VANILLAKEY windows, so take RAWKEY and use the keymap here */
static int convertKey(UWORD code, UWORD qualifier, APTR deadKeys)
{
  struct InputEvent ie;
  char buf[8];

  if ((code & IECODE_UP_PREFIX) || !ConsoleDevice)
    return 0;
  if (code == 0x46)
    return KEY_DEL;
  if (code == 0x5F)
    return KEY_HELP;
  memset(&ie, 0, sizeof ie);
  ie.ie_Class = IECLASS_RAWKEY;
  ie.ie_Code = code;
  ie.ie_Qualifier = qualifier;
  ie.ie_position.ie_addr = deadKeys;
  if (RawKeyConvert(&ie, (STRPTR)buf, sizeof buf, NULL) == 1)
    return (unsigned char)buf[0];
  return 0;
}

/* Fiftieths of a second since start: timer requests come late when a
   frame takes long to draw, so count real time rather than requests */
static UWORD clock50(const struct DateStamp *start)
{
  struct DateStamp now;

  DateStamp(&now);
  return (UWORD)(((now.ds_Days - start->ds_Days) * 1440L + now.ds_Minute - start->ds_Minute) *
                     (60L * TICKS_PER_SECOND) + now.ds_Tick - start->ds_Tick);
}

int main(void)
{
  struct DateStamp now;
  ULONG winSig, timerSig;

  DateStamp(&now);
  rulesSeed((ULONG)now.ds_Tick * 7919UL + (ULONG)now.ds_Minute * 104729UL +
            (*(volatile UWORD *)0xDFF006));
  if (!apiInit())
    fail("Cartarchy needs about 120K of free memory.");
  if (!gfxOpen())
    fail("Cartarchy needs about 180K of free chip memory.");
  if (OpenDevice((UBYTE *)"console.device", CONU_LIBRARY, (struct IORequest *)&consoleReq, 0) == 0)
    ConsoleDevice = consoleReq.io_Device;
  if (!(timerPort = CreatePort(NULL, 0)) ||
      !(timerReq = (struct timerequest *)CreateExtIO(timerPort, sizeof(struct timerequest))) ||
      OpenDevice((UBYTE *)TIMERNAME, UNIT_VBLANK, (struct IORequest *)timerReq, 0))
    fail("Cartarchy could not open timer.device.");
  timerOpen = 1;
  soundOpen();

  setStatus("LOOKING FOR FUJINET...", PEN_INK);
  uiShow(M_MENU);
  if (netOpen()) {
    prefsLoad();
    if (apiConfigured()) {
      setStatus("ASKING THE SERVER WHO YOU ARE...", PEN_INK);
      if (apiWhoAmI())
        setStatus("FUJINET READY.", PEN_INK);
      else
        setStatus(apiError, PEN_PLAYER);
    } else {
      setStatus("FUJINET READY. PAIR WITH A SERVER TO PLAY ONLINE.", PEN_INK);
    }
  } else {
    setStatus(netError, PEN_PLAYER);
  }
  uiShow(M_MENU);

  winSig = 1UL << win->UserPort->mp_SigBit;
  timerSig = 1UL << timerPort->mp_SigBit;
  startTimer();
  while (!quitting) {
    ULONG sigs = Wait(winSig | timerSig | SIGBREAKF_CTRL_C);
    struct IntuiMessage *msg;
    int moved = 0;
    WORD mx = 0, my = 0;

    if (sigs & SIGBREAKF_CTRL_C)
      break;
    if ((sigs & timerSig) && CheckIO((struct IORequest *)timerReq)) {
      WaitIO((struct IORequest *)timerReq);
      timerBusy = 0;
      frame = clock50(&now);
      if (mode == M_PLAY)
        gfxPulse(frame);
      uiTick();
      startTimer();
    }
    while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
      ULONG cls = msg->Class;
      UWORD code = msg->Code, qualifier = msg->Qualifier;
      WORD x = msg->MouseX, y = msg->MouseY;
      APTR deadKeys = cls == RAWKEY && msg->IAddress ? *(APTR *)msg->IAddress : NULL;
      int c;

      ReplyMsg((struct Message *)msg);
      if (cls == MOUSEMOVE) {
        // Only where the pointer ends up matters
        moved = 1;
        mx = x;
        my = y;
      } else if (cls == MOUSEBUTTONS && code == SELECTDOWN) {
        uiClick(x, y);
      } else if (cls == MOUSEBUTTONS && code == MENUDOWN) {
        if (mode == M_PLAY)
          playRightClick();
        else
          uiKey(KEY_ESC);
      } else if (cls == RAWKEY && (c = convertKey(code, qualifier, deadKeys)) != 0) {
        uiKey(c);
      }
      if (quitting)
        break;
    }
    if (moved && !quitting)
      uiMove(mx, my);
  }
  shutdown();
  return 0;
}
