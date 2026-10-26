#include <string.h>

#include <exec/execbase.h>
#include <exec/resident.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "fujinet-nio.h"
#include "fn_internal.h"
#include "net.h"

extern struct ExecBase *SysBase;

const char *netError = "";

static const char deviceName[] = "fujinet-nio.device";
static int connected;

/* What fujinet-load-resident does: LoadSeg, find the ROMTag, InitResident.
   Running the loader through Execute() hangs when started from Workbench on
   1.3, which has no CLI for it. */
static int loadDevice(const char *path)
{
  BPTR seglist = LoadSeg((UBYTE *)path);
  UWORD *p, *end;

  if (!seglist)
    return 0;
  p = (UWORD *)((ULONG *)BADDR(seglist) + 1);
  end = (UWORD *)((UBYTE *)BADDR(seglist) + ((ULONG *)BADDR(seglist))[-1] - 4);
  for (; p + sizeof(struct Resident) / 2 <= end; ++p) {
    struct Resident *r = (struct Resident *)p;

    if (r->rt_MatchWord == RTC_MATCHWORD && r->rt_MatchTag == r &&
        r->rt_Type == NT_DEVICE && r->rt_Name &&
        strcmp((const char *)r->rt_Name, deviceName) == 0) {
      InitResident(r, seglist);
      return FindName(&SysBase->DeviceList, (UBYTE *)deviceName) != NULL;
    }
  }
  UnLoadSeg(seglist);
  return 0;
}

static int tryInit(void)
{
  if (fn_init() == FN_OK && fn_is_ready())
    return 1;
  fn_shutdown();
  return 0;
}

int netOpen(void)
{
  static const char *const paths[] = {
    "DEVS:fujinet-nio.device", "NIO:fujinet-nio.device", "fujinet-nio.device",
  };
  unsigned i;

  if (connected)
    return 1;
  if (tryInit())
    return connected = 1;
  if (!FindName(&SysBase->DeviceList, (UBYTE *)deviceName)) {
    for (i = 0; i < sizeof paths / sizeof paths[0]; ++i) {
      BPTR lock = Lock((UBYTE *)paths[i], ACCESS_READ);

      if (!lock)
        continue;
      UnLock(lock);
      if (loadDevice(paths[i]) && tryInit())
        return connected = 1;
    }
    netError = "NO FUJINET-NIO.DEVICE IN DEVS: OR NIO:";
    return 0;
  }
  netError = "FUJINET IS NOT ANSWERING";
  return 0;
}

void netClose(void)
{
  if (connected)
    fn_shutdown();
  connected = 0;
}

static uint16_t putU16(uint8_t *b, uint16_t o, uint16_t v)
{
  b[o] = (uint8_t)v;
  b[o + 1] = (uint8_t)(v >> 8);
  return (uint16_t)(o + 2);
}

static uint16_t putLp(uint8_t *b, uint16_t o, const char *s)
{
  uint16_t n = (uint16_t)strlen(s);

  o = putU16(b, o, n);
  memcpy(b + o, s, n);
  return (uint16_t)(o + n);
}

/* The NetworkDevice Open request with request headers and a body length,
   which the library's fn_open() leaves empty: version, method, flags, url,
   header pairs, body length, response-header names */
static uint8_t openWithHeaders(fn_handle_t *handle, uint8_t method, const char *url,
                               const char *token, uint32_t bodyLen)
{
  static char auth[96];
  uint8_t *b = _fn_req_buf;
  uint16_t o, total;
  uint8_t flags, rc;

  if ((rc = fn_open_prepare(handle, url, FN_OPEN_FOLLOW_REDIR, &flags)) != FN_OK)
    return rc;
  if (strlen(token) > sizeof auth - 8 || strlen(url) + strlen(token) + 96 > FN_MAX_PACKET_SIZE)
    return FN_ERR_URL_TOO_LONG;
  strcpy(auth, "Bearer ");
  strcat(auth, token);

  o = FN_HEADER_SIZE;
  b[o++] = FN_PROTOCOL_VERSION;
  b[o++] = method;
  b[o++] = flags;
  o = putLp(b, o, url);
  o = putU16(b, o, (uint16_t)((*token ? 1 : 0) + (bodyLen ? 1 : 0)));
  if (*token) {
    o = putLp(b, o, "Authorization");
    o = putLp(b, o, auth);
  }
  if (bodyLen) {
    o = putLp(b, o, "Content-Type");
    o = putLp(b, o, "application/json");
  }
  b[o++] = (uint8_t)bodyLen;
  b[o++] = (uint8_t)(bodyLen >> 8);
  b[o++] = (uint8_t)(bodyLen >> 16);
  b[o++] = (uint8_t)(bodyLen >> 24);
  o = putU16(b, o, 0);
  total = o;
  fn_build_header(b, FN_DEVICE_NETWORK, FN_CMD_OPEN, total);
  b[FN_CHECKSUM_OFFSET] = fn_calc_packet_checksum(b, total);
  return fn_open_exchange(handle, total);
}

/* Delay() ticks are 1/50 s: give a slow TLS fetch ~15 s */
#define WAIT_LIMIT 750
/* A read's reply, headers and all, has to fit one 1024-byte packet */
#define READ_CHUNK 512

static fn_handle_t handle = FN_INVALID_HANDLE;
static uint32_t readOffset;
static int atEnd, readFailed;

int httpOpen(int method, const char *url, const char *token, const char *body, long bodyLen)
{
  uint32_t offset = 0;
  uint16_t n;
  uint8_t rc;

  httpClose();
  if (!netOpen())
    return 0;
  if ((rc = openWithHeaders(&handle, (uint8_t)method, url, token ? token : "",
                            (uint32_t)bodyLen)) != FN_OK) {
    handle = FN_INVALID_HANDLE;
    netError = rc == FN_ERR_URL_TOO_LONG ? "SERVER ADDRESS TOO LONG" : "COULD NOT OPEN THE URL";
    return 0;
  }
  readOffset = 0;
  atEnd = readFailed = 0;
  while ((long)offset < bodyLen) {
    long chunk = bodyLen - offset > 512 ? 512 : bodyLen - offset;

    if (fn_write(handle, offset, (const uint8_t *)body + offset, (uint16_t)chunk, &n) != FN_OK ||
        n == 0) {
      httpClose();
      netError = "SENDING TO THE SERVER FAILED";
      return 0;
    }
    offset += n;
  }
  return 1;
}

long httpRead(char *buf, long max)
{
  long got = 0;
  uint16_t n;
  uint8_t flags, rc;
  int waits = 0;

  while (got < max && !atEnd && handle != FN_INVALID_HANDLE) {
    long room = max - got;

    rc = fn_read(handle, readOffset, (uint8_t *)buf + got,
                 (uint16_t)(room > READ_CHUNK ? READ_CHUNK : room), &n, &flags);
    if (rc == FN_ERR_NOT_READY || rc == FN_ERR_BUSY) {
      if (++waits > WAIT_LIMIT) {
        netError = "THE SERVER STOPPED ANSWERING";
        break;
      }
      Delay(1);
      continue;
    }
    if (rc != FN_OK) {
      atEnd = readFailed = 1;
      netError = "READING FROM THE SERVER FAILED";
      break;
    }
    waits = 0;
    got += n;
    readOffset += n;
    if ((flags & FN_READ_EOF) || n == 0)
      atEnd = 1;
  }
  return got;
}

int httpStatus(void)
{
  uint32_t length;
  uint16_t status;
  uint8_t flags, rc;
  int waits;

  // A reply that broke off is no reply
  if (readFailed)
    return 0;
  for (waits = 0; waits < WAIT_LIMIT && handle != FN_INVALID_HANDLE; waits++) {
    rc = fn_info(handle, &status, &length, &flags);
    if (rc == FN_OK && (flags & FN_INFO_HAS_STATUS))
      return status;
    if (rc != FN_OK && rc != FN_ERR_NOT_READY && rc != FN_ERR_BUSY)
      break;
    Delay(1);
  }
  netError = "NO ANSWER FROM THE SERVER";
  return 0;
}

void httpClose(void)
{
  if (handle != FN_INVALID_HANDLE)
    fn_close(handle);
  handle = FN_INVALID_HANDLE;
}

int httpRequest(int method, const char *url, const char *token,
                const char *body, long bodyLen, char *buf, long max, long *got)
{
  int status = 0;

  *got = 0;
  buf[0] = 0;
  if (httpOpen(method, url, token, body, bodyLen)) {
    *got = httpRead(buf, max - 1);
    buf[*got] = 0;
    status = httpStatus();
  }
  httpClose();
  return status;
}
