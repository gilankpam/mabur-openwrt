#include "phyerr.h"
#include <stdlib.h>
#include <string.h>

int phyerr_sum(const char *text, uint32_t *out) {
  uint32_t sum = 0;
  int rows = 0;
  const char *p = text;
  while (*p) {
    const char *eol = strchr(p, '\n');
    if (!eol) eol = p + strlen(p);
    const char *s = p;
    while (s < eol && *s == ' ') s++;
    if ((strncmp(s, "OFDM-", 5) == 0 || strncmp(s, "HT-", 3) == 0)) {
      const char *c = memchr(s, ':', (size_t)(eol - s));
      if (c) { sum += (uint32_t)strtoul(c + 1, NULL, 10); rows++; }
    }
    p = *eol ? eol + 1 : eol;
  }
  if (!rows) return -1;
  *out = sum;
  return 0;
}
