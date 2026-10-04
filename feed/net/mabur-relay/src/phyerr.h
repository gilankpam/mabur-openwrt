#ifndef PHYERR_H
#define PHYERR_H
#include <stdint.h>
/* Sums the OFDM-* and HT-* rows of ath9k's debugfs phy_err file
 * ("      OFDM-RESTART ERR :        149"): the PHY errors a non-802.11
 * carrier or a corrupted OFDM preamble produce -- the relay's stand-in for
 * the rtl false-alarm counter. 0 when at least one such row was read. */
int phyerr_sum(const char *text, uint32_t *out);
#endif
