#ifndef NET_H
#define NET_H

/* FujiNet NIO: the device, and HTTP with the headers the Cartarchy API
   needs (fn_open() sends none). */

extern const char *netError;

int netOpen(void);
void netClose(void);

/* One request at a time: open sends the body (bodyLen bytes, may be 0)
   with a bearer token; read fills buf until max bytes or the end of the
   reply. Closing early is fine, and is how callers fetch only the start of
   a game. Each returns 0 when FujiNet or the server could not be reached,
   and netError says why. */
int httpOpen(int method, const char *url, const char *token, const char *body, long bodyLen);
long httpRead(char *buf, long max);
int httpStatus(void);
void httpClose(void);

/* All of that: the reply's first max-1 bytes into buf, NUL-terminated.
   Returns the HTTP status. */
int httpRequest(int method, const char *url, const char *token,
                const char *body, long bodyLen, char *buf, long max, long *got);

#endif
