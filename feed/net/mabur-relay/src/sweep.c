#include "sweep.h"
#include <string.h>

static uint16_t sat16(uint32_t v) { return v > 0xFFFF ? 0xFFFF : (uint16_t)v; }
static uint32_t sub0(uint32_t a, uint32_t b) { return a > b ? a - b : 0; }

static void tune_current(struct sweep *s, const struct radio_ops *ops) {
  uint8_t ch = s->req.ch[s->step % s->req.n];
  struct mr_scan_entry *e = &s->out[s->count];
  memset(e, 0, sizeof *e);
  e->ch = ch;
  e->pass = (uint8_t)(s->step / s->req.n);
  uint64_t t0 = ops->now_ms(ops->ctx);
  int ok = ops->set_channel(ops->ctx, ch, 0) == 0;
  uint64_t t1 = ops->now_ms(ops->ctx);
  if (t1 - t0 > s->max_retune_ms) s->max_retune_ms = (uint32_t)(t1 - t0);
  /* Baseline right after the tune: ath9k resets a channel's survey counters
   * when the radio tunes onto it, and the first ~3 ms read busy (bench
   * 2026-10-05) -- the delta removes both. */
  s->base_ok = ok && ops->survey(ops->ctx, ch, &s->base) == 0 &&
               ops->phyerr(ops->ctx, &s->err_base) == 0;
  s->foreign_base = ops->foreign(ops->ctx);
  s->deadline_ms = ops->now_ms(ops->ctx) + (s->base_ok ? s->req.observe_ms : 0);
}

void sweep_start(struct sweep *s, const struct mr_scan *req, uint8_t back_ch, uint8_t back_sec,
                 const struct radio_ops *ops) {
  memset(s, 0, sizeof *s);
  s->active = 1;
  s->req = *req;
  s->back_ch = back_ch; s->back_sec = back_sec;
  s->started_ms = ops->now_ms(ops->ctx);
  tune_current(s, ops);
}

void sweep_step(struct sweep *s, const struct radio_ops *ops) {
  if (!s->active) return;
  struct mr_scan_entry *e = &s->out[s->count];
  if (s->base_ok) {
    struct survey_sample cur;
    uint32_t err;
    if (ops->survey(ops->ctx, e->ch, &cur) == 0 && ops->phyerr(ops->ctx, &err) == 0) {
      e->valid = 1;
      e->active_ms = sat16(sub0(cur.active_ms, s->base.active_ms));
      e->busy_ms = sat16(sub0(cur.busy_ms, s->base.busy_ms));
      e->rx_ms = sat16(sub0(cur.rx_ms, s->base.rx_ms));
      e->ofdm_err = sat16(err - s->err_base);
      e->foreign = sat16(ops->foreign(ops->ctx) - s->foreign_base);
    }
  }
  s->count++;
  s->step++;
  if (s->step < s->req.passes * s->req.n) { tune_current(s, ops); return; }
  s->status = ops->set_channel(ops->ctx, s->back_ch, s->back_sec) == 0 ? MR_SCAN_OK : MR_SCAN_NO_RETURN;
  s->total_ms = (uint32_t)(ops->now_ms(ops->ctx) - s->started_ms);
  s->active = 0;
}

uint8_t sweep_channel(const struct sweep *s) {
  return s->active ? s->out[s->count].ch : 0;
}
