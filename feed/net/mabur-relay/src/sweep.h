#ifndef SWEEP_H
#define SWEEP_H
#include <stdint.h>
#include "wire.h"

/* The relay-side channel sweep (protocol v4 SCAN): one (pass, channel) at a
 * time -- tune, baseline survey + phy errors, observe, read again; entry =
 * end - baseline. Pure: the radio, the clock and the foreign-frame count
 * come through radio_ops, so the host tests drive it with a fake. The
 * caller's poll loop calls sweep_step() when now >= deadline_ms. */

struct survey_sample { uint32_t active_ms, busy_ms, rx_ms, tx_ms; };
struct radio_ops {
  int (*set_channel)(void *ctx, uint8_t ch, uint8_t sec);          /* 0 ok; blocks until tuned */
  int (*survey)(void *ctx, uint8_t ch, struct survey_sample *out);  /* 0 ok; ch's 20 MHz counters */
  int (*phyerr)(void *ctx, uint32_t *ofdm_err);                     /* 0 ok; cumulative */
  uint64_t (*now_ms)(void *ctx);
  uint32_t (*foreign)(void *ctx);                                   /* relay's cumulative foreign count */
  void *ctx;
};
struct sweep {
  int active;
  struct mr_scan req;
  uint8_t back_ch, back_sec;
  int step;                    /* 0 .. passes*n-1 */
  uint64_t deadline_ms;
  int base_ok;
  struct survey_sample base;
  uint32_t err_base, foreign_base;
  uint8_t status;              /* MR_SCAN_OK / MR_SCAN_NO_RETURN, valid once !active */
  int count;
  struct mr_scan_entry out[MR_SCAN_MAX_ENTRIES];
  uint64_t started_ms;
  uint32_t max_retune_ms, total_ms;
};
void sweep_start(struct sweep *s, const struct mr_scan *req, uint8_t back_ch, uint8_t back_sec,
                 const struct radio_ops *ops);
void sweep_step(struct sweep *s, const struct radio_ops *ops);   /* call when now >= deadline_ms */
uint8_t sweep_channel(const struct sweep *s);                    /* 0 when !active */
#endif
