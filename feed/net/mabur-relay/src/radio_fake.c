/* Host-test radio (see radio.h): scripted through files in $IW_STUB_DIR. */
#include "radio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static struct { char dir[256]; uint64_t since[256]; uint32_t err; } F;

static uint64_t f_now(void *c) {
  (void)c;
  struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}

static int read_int(const char *name, int dflt) {
  char p[320]; snprintf(p, sizeof p, "%s/%s", F.dir, name);
  FILE *f = fopen(p, "r"); if (!f) return dflt;
  int v = dflt; if (fscanf(f, "%d", &v) != 1) v = dflt; fclose(f); return v;
}

static int listed(const char *name, unsigned ch) {
  char p[320]; snprintf(p, sizeof p, "%s/%s", F.dir, name);
  FILE *f = fopen(p, "r"); if (!f) return 0;
  unsigned v; int hit = 0;
  while (fscanf(f, "%u", &v) == 1) if (v == ch) hit = 1;
  fclose(f); return hit;
}

static int f_set(void *c, uint8_t ch, uint8_t sec) {
  (void)c;
  static const char *s[] = {"HT20", "HT40+", "HT40-"};
  char p[320]; snprintf(p, sizeof p, "%s/calls", F.dir);
  FILE *f = fopen(p, "a");
  if (f) { fprintf(f, "nl %u %s\n", (unsigned)ch, sec <= 2 ? s[sec] : "?"); fclose(f); }
  if (listed("nl_fail", ch)) return -1;
  F.since[ch] = f_now(NULL);
  return 0;
}

static int f_survey(void *c, uint8_t ch, struct survey_sample *o) {
  (void)c;
  uint64_t now = f_now(NULL);
  if (!F.since[ch]) F.since[ch] = now;
  char name[32]; snprintf(name, sizeof name, "busy_%u", (unsigned)ch);
  uint32_t act = (uint32_t)(now - F.since[ch]);
  uint32_t pct = (uint32_t)read_int(name, 10);
  o->active_ms = act; o->busy_ms = act * pct / 100; o->rx_ms = o->busy_ms / 2; o->tx_ms = 0;
  return 0;
}

static int f_err(void *c, uint32_t *e) { (void)c; F.err += 7; *e = F.err; return 0; }

static struct radio_ops OPS = {f_set, f_survey, f_err, f_now, NULL, NULL};

const struct radio_ops *radio_open(const char *mon) {
  (void)mon;
  const char *d = getenv("IW_STUB_DIR");
  if (!d) return NULL;
  snprintf(F.dir, sizeof F.dir, "%s", d);
  return &OPS;
}
