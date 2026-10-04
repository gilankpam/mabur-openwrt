#ifndef RADIO_H
#define RADIO_H
#include "sweep.h"
/* The radio behind the sweep and the SURVEY feed (protocol v4). One backend
 * per build: radio_nl.c (OpenWrt: nl80211 via libnl-tiny + ath9k debugfs)
 * or radio_fake.c (host tests: scripted through $IW_STUB_DIR). NULL = no
 * usable radio; the relay then answers SCAN with status 4 and sends no
 * SURVEY. ops->foreign is left NULL for the relay to fill. */
const struct radio_ops *radio_open(const char *mon);
#endif
