#include <string.h>
#include "t.h"
#include "sweep.h"
#include "wire.h"

struct fake {
  uint64_t now;
  uint8_t cur_ch, cur_sec;
  uint64_t since[256];          /* when ch was last tuned (counters reset there) */
  int busy_pct[256];
  int fail_ch;                  /* set_channel to this fails (0 = none) */
  uint32_t err, foreign;
  char log[512];
};

static int f_set(void *c, uint8_t ch, uint8_t sec) {
  struct fake *f = c;
  char e[16]; snprintf(e, sizeof e, "%u/%u ", ch, sec); strncat(f->log, e, sizeof f->log - strlen(f->log) - 1);
  f->now += 18;
  if (ch == f->fail_ch) return -1;
  f->cur_ch = ch; f->cur_sec = sec; f->since[ch] = f->now;
  return 0;
}
static int f_survey(void *c, uint8_t ch, struct survey_sample *o) {
  struct fake *f = c;
  uint32_t act = (uint32_t)(f->now - f->since[ch]) + 3;   /* +3: the tune artifact, removed by the baseline */
  o->active_ms = act; o->busy_ms = 3 + (uint32_t)((act - 3) * (uint32_t)f->busy_pct[ch] / 100);
  o->rx_ms = (o->busy_ms - 3) / 2; o->tx_ms = 0;
  return 0;
}
static int f_err(void *c, uint32_t *e) { struct fake *f = c; f->err += 5; *e = f->err; return 0; }
static uint64_t f_now(void *c) { return ((struct fake *)c)->now; }
static uint32_t f_foreign(void *c) { return ((struct fake *)c)->foreign; }

static struct radio_ops ops_for(struct fake *f) {
  struct radio_ops o = {f_set, f_survey, f_err, f_now, f_foreign, f};
  return o;
}

static void run(struct sweep *s, struct fake *f, const struct radio_ops *o) {
  for (int guard = 0; s->active && guard < 1000; guard++) {
    f->now = s->deadline_ms;           /* the poll loop wakes exactly at the deadline */
    f->foreign += 2;                   /* two foreign frames heard per dwell */
    sweep_step(s, o);
  }
}

static void sweep_orders_passes_and_subtracts_the_baseline(void) {
  static struct fake f; memset(&f, 0, sizeof f); f.now = 1000; f.busy_pct[64] = 50; f.busy_pct[144] = 100;
  struct radio_ops o = ops_for(&f);
  struct mr_scan req = {7, 2, 20, 2, {64, 144}};
  static struct sweep s;
  sweep_start(&s, &req, 112, 0, &o);
  CHECK(s.active); CHECK_EQ(sweep_channel(&s), 64);
  run(&s, &f, &o);
  CHECK(!s.active); CHECK_EQ(s.status, MR_SCAN_OK); CHECK_EQ(s.count, 4);
  CHECK_EQ(s.out[0].ch, 64);  CHECK_EQ(s.out[0].pass, 0);
  CHECK_EQ(s.out[1].ch, 144); CHECK_EQ(s.out[1].pass, 0);
  CHECK_EQ(s.out[2].ch, 64);  CHECK_EQ(s.out[2].pass, 1);
  CHECK_EQ(s.out[3].ch, 144); CHECK_EQ(s.out[3].pass, 1);
  CHECK_EQ(s.out[0].valid, 1);
  CHECK_EQ(s.out[0].active_ms, 20);          /* artifact gone: exactly the observe */
  CHECK_EQ(s.out[0].busy_ms, 10);            /* 50 % of 20 */
  CHECK_EQ(s.out[1].busy_ms, 20);            /* 100 % */
  CHECK_EQ(s.out[0].rx_ms, 5);
  CHECK_EQ(s.out[0].ofdm_err, 5);            /* one phyerr read between baseline and end */
  CHECK_EQ(s.out[0].foreign, 2);
  CHECK(strcmp(f.log, "64/0 144/0 64/0 144/0 112/0 ") == 0);   /* back to op at the end */
  CHECK_EQ(s.max_retune_ms, 18);
}

static void sweep_returns_to_ht40_tuning(void) {       /* Review Focus 5 */
  static struct fake f; memset(&f, 0, sizeof f); f.now = 1000;
  struct radio_ops o = ops_for(&f);
  struct mr_scan req = {8, 1, 20, 1, {64}};
  static struct sweep s;
  sweep_start(&s, &req, 112, 2, &o);
  run(&s, &f, &o);
  CHECK_EQ(f.cur_ch, 112); CHECK_EQ(f.cur_sec, 2);
  CHECK_EQ(s.status, MR_SCAN_OK);
}

static void sweep_tune_failure_marks_entry_invalid(void) {
  static struct fake f; memset(&f, 0, sizeof f); f.now = 1000; f.fail_ch = 144;
  struct radio_ops o = ops_for(&f);
  struct mr_scan req = {9, 1, 20, 2, {64, 144}};
  static struct sweep s;
  sweep_start(&s, &req, 112, 0, &o);
  run(&s, &f, &o);
  CHECK_EQ(s.count, 2);
  CHECK_EQ(s.out[0].valid, 1);
  CHECK_EQ(s.out[1].valid, 0); CHECK_EQ(s.out[1].ch, 144);
  CHECK_EQ(s.status, MR_SCAN_OK);
}

static void sweep_return_failure_reports_no_return(void) {
  static struct fake f; memset(&f, 0, sizeof f); f.now = 1000; f.fail_ch = 112;
  struct radio_ops o = ops_for(&f);
  struct mr_scan req = {10, 1, 20, 1, {64}};
  static struct sweep s;
  sweep_start(&s, &req, 112, 0, &o);
  run(&s, &f, &o);
  CHECK_EQ(s.status, MR_SCAN_NO_RETURN);
  CHECK_EQ(sweep_channel(&s), 0);
}

void t_sweep(const char *fx) {
  (void)fx;
  sweep_orders_passes_and_subtracts_the_baseline();
  sweep_returns_to_ht40_tuning();
  sweep_tune_failure_marks_entry_invalid();
  sweep_return_failure_reports_no_return();
}
