#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t.h"
#include "phyerr.h"

void t_phyerr(const char *fx) {
  char path[512], buf[4096];
  snprintf(path, sizeof path, "%s/phy_err.txt", fx);
  FILE *f = fopen(path, "r");
  CHECK(f != NULL);
  if (!f) return;
  size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0; fclose(f);
  uint32_t s = 0;
  CHECK_EQ(phyerr_sum(buf, &s), 0);
  CHECK_EQ(s, 2 + 1 + 149 + 5 + 3);          /* OFDM-* and HT-* rows only; CCK/SERVICE excluded */
  CHECK_EQ(phyerr_sum("SPECTRAL ERR : 3\n", &s), -1);   /* no counted row */
  CHECK_EQ(phyerr_sum("", &s), -1);
}
