#include <string.h>
#include "t.h"
#include "wire.h"

static void golden_frame_hdr(void) {
  struct mr_frame_meta m = {0x01020304u, 136, 2,
                            MR_FLAG_BADFCS | MR_FLAG_PHY_VALID | MR_FLAG_STBC, 4,
                            {-37, -44}, {-95, -95}, 0xA1B2C3D4u}, u;
  uint8_t b[MR_FRAME_HDR_LEN];
  CHECK_EQ(mr_pack_frame_hdr(b, &m), MR_FRAME_HDR_LEN);
  static const uint8_t want[20] = {0x4D, 0x52, 0x04, 0x01, 0x04, 0x03, 0x02, 0x01,
                                   136, 2, 0x15, 4, 0xDB, 0xD4, 0xA1, 0xA1,
                                   0xD4, 0xC3, 0xB2, 0xA1};
  CHECK(memcmp(b, want, sizeof want) == 0);
  CHECK_EQ(mr_parse_frame_hdr(b, sizeof b, &u), MR_OK);
  CHECK_EQ(u.seq, 0x01020304u); CHECK_EQ(u.rx_channel, 136); CHECK_EQ(u.sec, 2);
  CHECK_EQ(u.flags, 0x15); CHECK_EQ(u.mcs, 4);
  CHECK_EQ(u.rssi[0], -37); CHECK_EQ(u.rssi[1], -44);
  CHECK_EQ(u.noise[0], -95); CHECK_EQ(u.noise[1], -95); CHECK_EQ(u.tsf_lo, 0xA1B2C3D4u);
  CHECK_EQ(mr_parse_frame_hdr(b, 19, &u), MR_ESHORT);
}

static void golden_tune(void) {
  struct mr_tune t = {0xBEEF, 149, 1}, u;
  uint8_t b[MR_TUNE_LEN];
  CHECK_EQ(mr_pack_tune(b, &t), MR_TUNE_LEN);
  static const uint8_t want[] = {0x4D, 0x52, 0x04, 0x03, 0xEF, 0xBE, 149, 1};
  CHECK(memcmp(b, want, sizeof want) == 0);
  CHECK_EQ(mr_parse_tune(b, sizeof b, &u), MR_OK);
  CHECK_EQ(u.tune_id, 0xBEEF); CHECK_EQ(u.channel, 149); CHECK_EQ(u.sec, 1);
}

static void golden_status(void) {
  struct mr_status s = {7, 0, 136, 2, 1, 1, 0x11223344u, 2, 3, 4, 5, 0xA0B0C0D0u,
                        0x01020304u, 6, 0xE0F00010u, 0x0A0B0C0Du}, u;
  uint8_t b[MR_STATUS_LEN];
  CHECK_EQ(MR_STATUS_LEN, 51);
  CHECK_EQ(mr_pack_status(b, &s), MR_STATUS_LEN);
  static const uint8_t head[] = {0x4D, 0x52, 0x04, 0x04, 7, 0, 0, 136, 2, 1, 1,
                                 0x44, 0x33, 0x22, 0x11};
  CHECK(memcmp(b, head, sizeof head) == 0);
  static const uint8_t tail[] = {0xD0, 0xC0, 0xB0, 0xA0,     /* uptime_s */
                                 0x04, 0x03, 0x02, 0x01,     /* tx */
                                 0x06, 0x00, 0x00, 0x00,     /* tx_fail */
                                 0x10, 0x00, 0xF0, 0xE0,     /* tx_refused */
                                 0x0D, 0x0C, 0x0B, 0x0A};    /* tx_scan_drop */
  CHECK(memcmp(b + MR_STATUS_LEN - 20, tail, 20) == 0);
  CHECK_EQ(mr_parse_status(b, sizeof b, &u), MR_OK);
  CHECK_EQ(u.tune_id, 7); CHECK_EQ(u.channel, 136); CHECK_EQ(u.rx, 0x11223344u);
  CHECK_EQ(u.fwd, 2); CHECK_EQ(u.foreign, 3); CHECK_EQ(u.bad_fcs, 4);
  CHECK_EQ(u.your_drops, 5); CHECK_EQ(u.uptime_s, 0xA0B0C0D0u);
  CHECK_EQ(u.tx, 0x01020304u); CHECK_EQ(u.tx_fail, 6); CHECK_EQ(u.tx_refused, 0xE0F00010u);
  CHECK_EQ(u.tx_scan_drop, 0x0A0B0C0Du);
  CHECK_EQ(mr_parse_status(b, MR_STATUS_LEN - 1, &u), MR_ESHORT);
}

static void golden_survey(void) {
  struct mr_survey s = {112, 2, 0x0102, 1000, 300, 100, 0, 0x0A0B0C0Du, 7}, u;
  uint8_t b[MR_SURVEY_LEN];
  CHECK_EQ(mr_pack_survey(b, &s), MR_SURVEY_LEN);
  static const uint8_t want[MR_SURVEY_LEN] = {
      0x4D, 0x52, 0x04, 0x06, 112, 2, 0x02, 0x01,
      0xE8, 0x03, 0x00, 0x00, 0x2C, 0x01, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x0D, 0x0C, 0x0B, 0x0A, 0x07, 0x00, 0x00, 0x00};
  CHECK(memcmp(b, want, sizeof want) == 0);
  CHECK_EQ(mr_parse_survey(b, sizeof b, &u), MR_OK);
  CHECK_EQ(u.channel, 112); CHECK_EQ(u.sec, 2); CHECK_EQ(u.gen, 0x0102);
  CHECK_EQ(u.active_ms, 1000); CHECK_EQ(u.busy_ms, 300); CHECK_EQ(u.rx_ms, 100);
  CHECK_EQ(u.tx_ms, 0); CHECK_EQ(u.ofdm_err, 0x0A0B0C0Du); CHECK_EQ(u.foreign, 7);
  CHECK_EQ(mr_parse_survey(b, MR_SURVEY_LEN - 1, &u), MR_ESHORT);
}

static void golden_scan(void) {
  struct mr_scan s = {0xBEEF, 2, 20, 3, {40, 64, 144}}, u;
  uint8_t b[MR_SCAN_HDR_LEN + MR_SCAN_MAX_CH];
  CHECK_EQ(mr_pack_scan(b, &s), MR_SCAN_HDR_LEN + 3);
  static const uint8_t want[] = {0x4D, 0x52, 0x04, 0x07, 0xEF, 0xBE, 2, 20, 3, 40, 64, 144};
  CHECK(memcmp(b, want, sizeof want) == 0);
  CHECK_EQ(mr_parse_scan(b, sizeof want, &u), MR_OK);
  CHECK_EQ(u.scan_id, 0xBEEF); CHECK_EQ(u.passes, 2); CHECK_EQ(u.observe_ms, 20);
  CHECK_EQ(u.n, 3); CHECK_EQ(u.ch[0], 40); CHECK_EQ(u.ch[2], 144);
  CHECK_EQ(mr_parse_scan(b, sizeof want - 1, &u), MR_ESHORT);      /* n says 3, only 2 bytes */
  b[6] = 0; CHECK_EQ(mr_parse_scan(b, sizeof want, &u), MR_EINVAL); b[6] = 5;
  CHECK_EQ(mr_parse_scan(b, sizeof want, &u), MR_EINVAL); b[6] = 2;   /* passes 1..4 */
  b[7] = 4; CHECK_EQ(mr_parse_scan(b, sizeof want, &u), MR_EINVAL); b[7] = 101;
  CHECK_EQ(mr_parse_scan(b, sizeof want, &u), MR_EINVAL); b[7] = 20; /* observe 5..100 */
  b[8] = 0; CHECK_EQ(mr_parse_scan(b, sizeof want, &u), MR_EINVAL); b[8] = 33;
  CHECK_EQ(mr_parse_scan(b, sizeof want, &u), MR_EINVAL); b[8] = 3;  /* n 1..32 */
}

static void golden_scan_result(void) {
  struct mr_scan_result_hdr h = {0xBEEF, MR_SCAN_OK, 112, 2, 1}, hu;
  struct mr_scan_entry e = {64, 1, 1, 20, 14, 1, 3, 0x0102};
  static struct mr_scan_entry eu[MR_SCAN_MAX_ENTRIES];
  uint8_t b[MR_SCAN_RESULT_HDR_LEN + MR_SCAN_ENTRY_LEN];
  CHECK_EQ(mr_pack_scan_result(b, &h, &e), sizeof b);
  static const uint8_t want[] = {0x4D, 0x52, 0x04, 0x08, 0xEF, 0xBE, 0, 112, 2, 1,
                                 64, 1, 1, 0x14, 0x00, 0x0E, 0x00, 0x01, 0x00,
                                 0x03, 0x00, 0x02, 0x01};
  CHECK(memcmp(b, want, sizeof want) == 0);
  CHECK_EQ(mr_parse_scan_result(b, sizeof b, &hu, eu), MR_OK);
  CHECK_EQ(hu.scan_id, 0xBEEF); CHECK_EQ(hu.status, 0); CHECK_EQ(hu.back_channel, 112);
  CHECK_EQ(hu.back_sec, 2); CHECK_EQ(hu.count, 1);
  CHECK_EQ(eu[0].ch, 64); CHECK_EQ(eu[0].pass, 1); CHECK_EQ(eu[0].valid, 1);
  CHECK_EQ(eu[0].active_ms, 20); CHECK_EQ(eu[0].busy_ms, 14); CHECK_EQ(eu[0].rx_ms, 1);
  CHECK_EQ(eu[0].foreign, 3); CHECK_EQ(eu[0].ofdm_err, 0x0102);
  CHECK_EQ(mr_parse_scan_result(b, sizeof b - 1, &hu, eu), MR_ESHORT);
  b[9] = MR_SCAN_MAX_ENTRIES + 1;
  CHECK_EQ(mr_parse_scan_result(b, sizeof b, &hu, eu), MR_EINVAL);
}

/* TX, mcs=0, flags=LDPC|STBC, 24-byte probe-req header (the mabur RCF shape). */
static void golden_tx(void) {
  uint8_t b[MR_TX_HDR_LEN + 24];
  CHECK_EQ(mr_pack_tx_hdr(b, 0, MR_TX_LDPC | MR_TX_STBC), MR_TX_HDR_LEN);
  static const uint8_t want[] = {0x4D, 0x52, 0x04, 0x05, 0x00, 0x03};
  CHECK(memcmp(b, want, sizeof want) == 0);
  for (int i = 0; i < 24; i++) b[MR_TX_HDR_LEN + i] = (uint8_t)(0x40 + i);
  struct mr_tx t;
  CHECK_EQ(mr_parse_tx(b, sizeof b, &t), MR_OK);
  CHECK_EQ(t.mcs, 0); CHECK_EQ(t.flags, 3); CHECK_EQ(t.dl, 24);
  CHECK(t.d == b + MR_TX_HDR_LEN);
}

static void tx_parse_rejects(void) {
  static uint8_t b[MR_TX_HDR_LEN + MR_TX_DOT11_MAX + 1];
  struct mr_tx t;
  mr_pack_tx_hdr(b, 0, 0);
  CHECK_EQ(mr_parse_tx(b, MR_TX_HDR_LEN - 1, &t), MR_ESHORT);
  CHECK_EQ(mr_parse_tx(b, MR_TX_HDR_LEN + 23, &t), MR_EINVAL);            /* dot11 < 24 */
  CHECK_EQ(mr_parse_tx(b, MR_TX_HDR_LEN + MR_TX_DOT11_MAX, &t), MR_OK);
  CHECK_EQ(mr_parse_tx(b, MR_TX_HDR_LEN + MR_TX_DOT11_MAX + 1, &t), MR_EINVAL);
  b[4] = 8; CHECK_EQ(mr_parse_tx(b, MR_TX_HDR_LEN + 24, &t), MR_EINVAL); b[4] = 7;
  CHECK_EQ(mr_parse_tx(b, MR_TX_HDR_LEN + 24, &t), MR_OK);
  b[5] = 0x10; CHECK_EQ(mr_parse_tx(b, MR_TX_HDR_LEN + 24, &t), MR_EINVAL); b[5] = 0;
  b[3] = MR_TUNE; CHECK_EQ(mr_parse_tx(b, MR_TX_HDR_LEN + 24, &t), MR_ETYPE);
}

static void header_errors(void) {
  uint8_t b[MR_HDR_LEN], type = 0;
  CHECK_EQ(mr_pack_hello(b), MR_HDR_LEN);
  CHECK_EQ(mr_parse_header(b, sizeof b, &type), MR_OK); CHECK_EQ(type, MR_HELLO);
  CHECK_EQ(mr_parse_header(b, 3, &type), MR_ESHORT);
  b[0] = 0x00; CHECK_EQ(mr_parse_header(b, 4, &type), MR_EMAGIC); b[0] = 0x4D;
  b[2] = 3; CHECK_EQ(mr_parse_header(b, 4, &type), MR_EVER); b[2] = 4;
  b[3] = 9; CHECK_EQ(mr_parse_header(b, 4, &type), MR_ETYPE);
  b[3] = 0; CHECK_EQ(mr_parse_header(b, 4, &type), MR_ETYPE);
  b[3] = MR_TX; CHECK_EQ(mr_parse_header(b, 4, &type), MR_OK); CHECK_EQ(type, MR_TX);
  b[3] = MR_SCAN_RESULT; CHECK_EQ(mr_parse_header(b, 4, &type), MR_OK); CHECK_EQ(type, MR_SCAN_RESULT);
  CHECK_EQ(MR_FLAGS_OFFSET, 10);
  struct mr_tune t = {1, 36, 0}; uint8_t tb[MR_TUNE_LEN]; mr_pack_tune(tb, &t);
  CHECK_EQ(mr_parse_tune(tb, MR_TUNE_LEN - 1, &t), MR_ESHORT);
  tb[3] = MR_HELLO; CHECK_EQ(mr_parse_tune(tb, MR_TUNE_LEN, &t), MR_ETYPE);
}

void t_wire(const char *fx) {
  (void)fx;
  golden_frame_hdr(); golden_tune(); golden_status(); golden_tx(); tx_parse_rejects(); header_errors();
  golden_survey(); golden_scan(); golden_scan_result();
}
