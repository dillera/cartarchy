#include <string.h>

#include "json.h"

static const char *ws(const char *p, const char *end)
{
  while (p && p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
    p++;
  return p && p < end ? p : NULL;
}

static const char *skipString(const char *p, const char *end)
{
  for (p++; p < end; p++) {
    if (*p == '\\')
      p++;
    else if (*p == '"')
      return p + 1;
  }
  return NULL;
}

const char *jsonSkip(const char *p, const char *end)
{
  int depth = 0;

  if (!(p = ws(p, end)))
    return NULL;
  if (*p == '"')
    return skipString(p, end);
  if (*p != '{' && *p != '[') {
    while (p < end && (strchr("+-.eE", *p) || (*p >= '0' && *p <= '9') ||
                       (*p >= 'a' && *p <= 'z')))
      p++;
    // A number at the very end may have been cut short
    return p < end ? p : NULL;
  }
  for (; p < end; p++) {
    if (*p == '"') {
      if (!(p = skipString(p, end)))
        return NULL;
      p--;
    } else if (*p == '{' || *p == '[') {
      depth++;
    } else if (*p == '}' || *p == ']') {
      if (--depth == 0)
        return p + 1;
    }
  }
  return NULL;
}

/* p at a member's key: copy it, return its value */
static const char *member(const char *p, const char *end, char *keyBuf, int keyMax)
{
  const char *k;
  int n = 0;

  if (!(p = ws(p, end)) || *p != '"')
    return NULL;
  for (k = p + 1; k < end && *k != '"'; k++) {
    if (*k == '\\')
      k++;
    if (n < keyMax - 1)
      keyBuf[n++] = *k;
  }
  keyBuf[n] = 0;
  if (k >= end || !(p = ws(k + 1, end)) || *p != ':')
    return NULL;
  return ws(p + 1, end);
}

const char *jsonFirstMember(const char *obj, const char *end, char *keyBuf, int keyMax)
{
  if (!(obj = ws(obj, end)) || *obj != '{')
    return NULL;
  if (!(obj = ws(obj + 1, end)) || *obj == '}')
    return NULL;
  return member(obj, end, keyBuf, keyMax);
}

const char *jsonNextMember(const char *value, const char *end, char *keyBuf, int keyMax)
{
  const char *p = ws(jsonSkip(value, end), end);

  if (!p || *p != ',')
    return NULL;
  return member(p + 1, end, keyBuf, keyMax);
}

const char *jsonGet(const char *obj, const char *end, const char *key)
{
  char k[32];
  const char *v;

  for (v = jsonFirstMember(obj, end, k, sizeof k); v; v = jsonNextMember(v, end, k, sizeof k))
    if (strcmp(k, key) == 0)
      return v;
  return NULL;
}

const char *jsonFirst(const char *arr, const char *end)
{
  if (!(arr = ws(arr, end)) || *arr != '[')
    return NULL;
  if (!(arr = ws(arr + 1, end)) || *arr == ']')
    return NULL;
  return arr;
}

const char *jsonNext(const char *value, const char *end)
{
  const char *p = ws(jsonSkip(value, end), end);

  if (!p || *p != ',')
    return NULL;
  return ws(p + 1, end);
}

long jsonInt(const char *v, const char *end, long fallback)
{
  long n = 0;
  int neg = 0;

  if (!(v = ws(v, end)) || !jsonSkip(v, end))
    return fallback;
  if (*v == '-') {
    neg = 1;
    v++;
  }
  if (v >= end || *v < '0' || *v > '9')
    return fallback;
  while (v < end && *v >= '0' && *v <= '9')
    n = n * 10 + (*v++ - '0');
  return neg ? -n : n;
}

int jsonBool(const char *v, const char *end, int fallback)
{
  if (!(v = ws(v, end)) || !jsonSkip(v, end))
    return fallback;
  if (*v == 't')
    return 1;
  if (*v == 'f')
    return 0;
  return fallback;
}

int jsonIsNull(const char *v, const char *end)
{
  return !(v = ws(v, end)) || *v == 'n';
}

static int hex(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return 0;
}

int jsonStr(const char *v, const char *end, char *out, int max)
{
  const unsigned char *p;
  int n = 0;

  if (!(v = ws(v, end)) || *v != '"' || !jsonSkip(v, end))
    return -1;
  for (p = (const unsigned char *)v + 1; *p != '"'; p++) {
    unsigned c = *p;

    if (c == '\\') {
      c = *++p;
      if (c == 'n')
        c = '\n';
      else if (c == 't')
        c = '\t';
      else if (c == 'r' || c == 'b' || c == 'f')
        c = ' ';
      else if (c == 'u') {
        c = hex(p[1]) << 12 | hex(p[2]) << 8 | hex(p[3]) << 4 | hex(p[4]);
        p += 4;
        if (c > 0xFF)
          c = '?';
      }
    } else if (c >= 0xC0) {
      // UTF-8: Latin-1 survives, anything wider becomes '?'
      unsigned lead = c;

      c = (lead == 0xC2 || lead == 0xC3) ? ((lead & 3) << 6 | (p[1] & 0x3F)) : '?';
      while ((p[1] & 0xC0) == 0x80)
        p++;
    }
    if (n < max - 1)
      out[n++] = (char)c;
  }
  out[n] = 0;
  return n;
}

void jsonInit(JsonOut *o, char *buf, long max)
{
  o->buf = buf;
  o->len = 0;
  o->max = max;
  o->overflow = 0;
  buf[0] = 0;
}

static void putc_(JsonOut *o, char c)
{
  if (o->len < o->max - 1) {
    o->buf[o->len++] = c;
    o->buf[o->len] = 0;
  } else {
    o->overflow = 1;
  }
}

void jsonPut(JsonOut *o, const char *s)
{
  while (*s)
    putc_(o, *s++);
}

void jsonPutStr(JsonOut *o, const char *s)
{
  static const char digits[] = "0123456789abcdef";
  const unsigned char *p = (const unsigned char *)s;

  putc_(o, '"');
  for (; *p; p++) {
    if (*p == '"' || *p == '\\') {
      putc_(o, '\\');
      putc_(o, (char)*p);
    } else if (*p < 0x20 || *p >= 0x7F) {
      // Latin-1 goes out as \u00XX, so the JSON stays valid UTF-8
      jsonPut(o, "\\u00");
      putc_(o, digits[*p >> 4]);
      putc_(o, digits[*p & 15]);
    } else {
      putc_(o, (char)*p);
    }
  }
  putc_(o, '"');
}

void jsonPutInt(JsonOut *o, long n)
{
  char tmp[12];
  int i = 0;
  unsigned long u = n < 0 ? -(unsigned long)n : (unsigned long)n;

  if (n < 0)
    putc_(o, '-');
  do {
    tmp[i++] = (char)('0' + u % 10);
    u /= 10;
  } while (u);
  while (i)
    putc_(o, tmp[--i]);
}
