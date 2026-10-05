#include "wire.h"
#include <string.h>

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_hdr(uint8_t *out, uint8_t type) {
  put16(out, MR_MAGIC); out[2] = MR_VER; out[3] = type;
}

size_t mr_pack_frame_hdr(uint8_t *out, const struct mr_frame_meta *m) {
  put_hdr(out, MR_FRAME); put32(out + 4, m->seq);
  out[8] = m->rx_channel; out[9] = m->sec; out[10] = m->flags; out[11] = m->mcs;
  out[12] = (uint8_t)m->rssi[0]; out[13] = (uint8_t)m->rssi[1];
  out[14] = (uint8_t)m->noise[0]; out[15] = (uint8_t)m->noise[1];
  put32(out + 16, m->tsf_lo);
  return MR_FRAME_HDR_LEN;
}

size_t mr_pack_hello(uint8_t *out) { put_hdr(out, MR_HELLO); return MR_HDR_LEN; }

size_t mr_pack_tune(uint8_t *out, const struct mr_tune *t) {
  put_hdr(out, MR_TUNE); put16(out + 4, t->tune_id); out[6] = t->channel; out[7] = t->sec;
  return MR_TUNE_LEN;
}

size_t mr_pack_status(uint8_t *out, const struct mr_status *s) {
  put_hdr(out, MR_STATUS); put16(out + 4, s->tune_id);
  out[6] = s->state; out[7] = s->channel; out[8] = s->sec; out[9] = s->owner; out[10] = s->you_own;
  put32(out + 11, s->rx); put32(out + 15, s->fwd); put32(out + 19, s->foreign);
  put32(out + 23, s->bad_fcs); put32(out + 27, s->your_drops); put32(out + 31, s->uptime_s);
  put32(out + 35, s->tx); put32(out + 39, s->tx_fail); put32(out + 43, s->tx_refused);
  put32(out + 47, s->tx_scan_drop);
  return MR_STATUS_LEN;
}

size_t mr_pack_tx_hdr(uint8_t *out, uint8_t mcs, uint8_t flags) {
  put_hdr(out, MR_TX); out[4] = mcs; out[5] = flags;
  return MR_TX_HDR_LEN;
}

int mr_parse_header(const uint8_t *b, size_t n, uint8_t *type) {
  if (n < MR_HDR_LEN) return MR_ESHORT;
  if (get16(b) != MR_MAGIC) return MR_EMAGIC;
  if (b[2] != MR_VER) return MR_EVER;
  if (b[3] < MR_FRAME || b[3] > MR_SCAN_RESULT) return MR_ETYPE;
  *type = b[3];
  return MR_OK;
}

static int expect(const uint8_t *b, size_t n, uint8_t want, size_t len) {
  uint8_t type;
  int rc = mr_parse_header(b, n, &type);
  if (rc != MR_OK) return rc;
  if (type != want) return MR_ETYPE;
  return n < len ? MR_ESHORT : MR_OK;
}

int mr_parse_frame_hdr(const uint8_t *b, size_t n, struct mr_frame_meta *m) {
  int rc = expect(b, n, MR_FRAME, MR_FRAME_HDR_LEN);
  if (rc != MR_OK) return rc;
  m->seq = get32(b + 4); m->rx_channel = b[8]; m->sec = b[9]; m->flags = b[10]; m->mcs = b[11];
  m->rssi[0] = (int8_t)b[12]; m->rssi[1] = (int8_t)b[13];
  m->noise[0] = (int8_t)b[14]; m->noise[1] = (int8_t)b[15];
  m->tsf_lo = get32(b + 16);
  return MR_OK;
}

int mr_parse_tune(const uint8_t *b, size_t n, struct mr_tune *t) {
  int rc = expect(b, n, MR_TUNE, MR_TUNE_LEN);
  if (rc != MR_OK) return rc;
  t->tune_id = get16(b + 4); t->channel = b[6]; t->sec = b[7];
  return MR_OK;
}

int mr_parse_status(const uint8_t *b, size_t n, struct mr_status *s) {
  int rc = expect(b, n, MR_STATUS, MR_STATUS_LEN);
  if (rc != MR_OK) return rc;
  s->tune_id = get16(b + 4);
  s->state = b[6]; s->channel = b[7]; s->sec = b[8]; s->owner = b[9]; s->you_own = b[10];
  s->rx = get32(b + 11); s->fwd = get32(b + 15); s->foreign = get32(b + 19);
  s->bad_fcs = get32(b + 23); s->your_drops = get32(b + 27); s->uptime_s = get32(b + 31);
  s->tx = get32(b + 35); s->tx_fail = get32(b + 39); s->tx_refused = get32(b + 43);
  s->tx_scan_drop = get32(b + 47);
  return MR_OK;
}

int mr_parse_tx(const uint8_t *b, size_t n, struct mr_tx *t) {
  int rc = expect(b, n, MR_TX, MR_TX_HDR_LEN);
  if (rc != MR_OK) return rc;
  size_t dl = n - MR_TX_HDR_LEN;
  if (b[4] > 7 || (b[5] & ~MR_TX_FLAGS_ALL) || dl < MR_TX_DOT11_MIN || dl > MR_TX_DOT11_MAX)
    return MR_EINVAL;
  t->mcs = b[4]; t->flags = b[5]; t->d = b + MR_TX_HDR_LEN; t->dl = dl;
  return MR_OK;
}

size_t mr_pack_survey(uint8_t *out, const struct mr_survey *s) {
  put_hdr(out, MR_SURVEY); out[4] = s->channel; out[5] = s->sec; put16(out + 6, s->gen);
  put32(out + 8, s->active_ms); put32(out + 12, s->busy_ms); put32(out + 16, s->rx_ms);
  put32(out + 20, s->tx_ms); put32(out + 24, s->ofdm_err); put32(out + 28, s->foreign);
  return MR_SURVEY_LEN;
}

int mr_parse_survey(const uint8_t *b, size_t n, struct mr_survey *s) {
  int rc = expect(b, n, MR_SURVEY, MR_SURVEY_LEN);
  if (rc != MR_OK) return rc;
  s->channel = b[4]; s->sec = b[5]; s->gen = get16(b + 6);
  s->active_ms = get32(b + 8); s->busy_ms = get32(b + 12); s->rx_ms = get32(b + 16);
  s->tx_ms = get32(b + 20); s->ofdm_err = get32(b + 24); s->foreign = get32(b + 28);
  return MR_OK;
}

size_t mr_pack_scan(uint8_t *out, const struct mr_scan *s) {
  put_hdr(out, MR_SCAN); put16(out + 4, s->scan_id);
  out[6] = s->passes; out[7] = s->observe_ms; out[8] = s->n;
  memcpy(out + MR_SCAN_HDR_LEN, s->ch, s->n);
  return MR_SCAN_HDR_LEN + s->n;
}

int mr_parse_scan(const uint8_t *b, size_t n, struct mr_scan *s) {
  int rc = expect(b, n, MR_SCAN, MR_SCAN_HDR_LEN);
  if (rc != MR_OK) return rc;
  uint8_t passes = b[6], obs = b[7], cnt = b[8];
  if (passes < 1 || passes > 4 || obs < 5 || obs > 100 || cnt < 1 || cnt > MR_SCAN_MAX_CH)
    return MR_EINVAL;
  if (n < (size_t)MR_SCAN_HDR_LEN + cnt) return MR_ESHORT;
  s->scan_id = get16(b + 4); s->passes = passes; s->observe_ms = obs; s->n = cnt;
  memcpy(s->ch, b + MR_SCAN_HDR_LEN, cnt);
  return MR_OK;
}

size_t mr_pack_scan_result(uint8_t *out, const struct mr_scan_result_hdr *h,
                           const struct mr_scan_entry *e) {
  put_hdr(out, MR_SCAN_RESULT); put16(out + 4, h->scan_id);
  out[6] = h->status; out[7] = h->back_channel; out[8] = h->back_sec; out[9] = h->count;
  uint8_t *p = out + MR_SCAN_RESULT_HDR_LEN;
  for (int i = 0; i < h->count; i++, p += MR_SCAN_ENTRY_LEN) {
    p[0] = e[i].ch; p[1] = e[i].pass; p[2] = e[i].valid;
    put16(p + 3, e[i].active_ms); put16(p + 5, e[i].busy_ms); put16(p + 7, e[i].rx_ms);
    put16(p + 9, e[i].foreign); put16(p + 11, e[i].ofdm_err);
  }
  return MR_SCAN_RESULT_HDR_LEN + (size_t)h->count * MR_SCAN_ENTRY_LEN;
}

int mr_parse_scan_result(const uint8_t *b, size_t n, struct mr_scan_result_hdr *h,
                         struct mr_scan_entry *e) {
  int rc = expect(b, n, MR_SCAN_RESULT, MR_SCAN_RESULT_HDR_LEN);
  if (rc != MR_OK) return rc;
  uint8_t cnt = b[9];
  if (cnt > MR_SCAN_MAX_ENTRIES) return MR_EINVAL;
  if (n < MR_SCAN_RESULT_HDR_LEN + (size_t)cnt * MR_SCAN_ENTRY_LEN) return MR_ESHORT;
  h->scan_id = get16(b + 4); h->status = b[6]; h->back_channel = b[7]; h->back_sec = b[8];
  h->count = cnt;
  const uint8_t *p = b + MR_SCAN_RESULT_HDR_LEN;
  for (int i = 0; i < cnt; i++, p += MR_SCAN_ENTRY_LEN) {
    e[i].ch = p[0]; e[i].pass = p[1]; e[i].valid = p[2];
    e[i].active_ms = get16(p + 3); e[i].busy_ms = get16(p + 5); e[i].rx_ms = get16(p + 7);
    e[i].foreign = get16(p + 9); e[i].ofdm_err = get16(p + 11);
  }
  return MR_OK;
}
