#ifndef JSON_H
#define JSON_H

/* Just enough JSON for the Cartarchy API, read in place.

   A value is a pointer to its first byte; every reader also takes the end of
   the buffer. Responses may be cut short on purpose (only the start of a
   game's JSON is fetched), so a value that runs past the end reads as
   missing (NULL) rather than as an error. */

const char *jsonSkip(const char *p, const char *end);
const char *jsonGet(const char *obj, const char *end, const char *key);
const char *jsonFirst(const char *arr, const char *end);
const char *jsonNext(const char *value, const char *end);
/* Object members in order: key into keyBuf, value returned */
const char *jsonFirstMember(const char *obj, const char *end, char *keyBuf, int keyMax);
const char *jsonNextMember(const char *value, const char *end, char *keyBuf, int keyMax);

long jsonInt(const char *v, const char *end, long fallback);
int jsonBool(const char *v, const char *end, int fallback);
int jsonIsNull(const char *v, const char *end);
/* Copies a string value as Latin-1 (what topaz draws); -1 if not a string */
int jsonStr(const char *v, const char *end, char *out, int max);

typedef struct {
  char *buf;
  long len, max;
  int overflow;
} JsonOut;

void jsonInit(JsonOut *o, char *buf, long max);
void jsonPut(JsonOut *o, const char *s);
void jsonPutStr(JsonOut *o, const char *s);
void jsonPutInt(JsonOut *o, long n);

#endif
