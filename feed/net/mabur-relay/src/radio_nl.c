/* CPE radio (see radio.h): nl80211 through libnl-tiny for set-channel and
 * GET_SURVEY, ath9k debugfs phy_err for OFDM/HT PHY errors. Single-threaded,
 * blocking: set_channel returns once the kernel acked (~18 ms on the CPE510,
 * bench 2026-10-05: iw's 25 ms minus its 7 ms fork). */
#include "radio.h"
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <linux/nl80211.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <netlink/msg.h>
#include <netlink/attr.h>
#include "phyerr.h"

static struct { struct nl_sock *sk; int fam; int ifindex; char phyerr_path[160]; } N;

static uint64_t n_now(void *c) {
  (void)c;
  struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}

static int n_set(void *c, uint8_t ch, uint8_t sec) {
  (void)c;
  struct nl_msg *m = nlmsg_alloc();
  if (!m) return -1;
  genlmsg_put(m, 0, 0, N.fam, 0, 0, NL80211_CMD_SET_WIPHY, 0);
  nla_put_u32(m, NL80211_ATTR_IFINDEX, (uint32_t)N.ifindex);
  nla_put_u32(m, NL80211_ATTR_WIPHY_FREQ, 5000u + 5u * ch);
  nla_put_u32(m, NL80211_ATTR_WIPHY_CHANNEL_TYPE,
              sec == 0 ? NL80211_CHAN_HT20 : sec == 1 ? NL80211_CHAN_HT40PLUS : NL80211_CHAN_HT40MINUS);
  int rc = nl_send_auto_complete(N.sk, m);
  nlmsg_free(m);
  if (rc < 0) return -1;
  return nl_wait_for_ack(N.sk) < 0 ? -1 : 0;
}

struct sv_req { uint32_t freq; int found, done; struct survey_sample *out; };

static int sv_valid(struct nl_msg *msg, void *arg) {
  struct sv_req *r = arg;
  struct nlattr *tb[NL80211_ATTR_MAX + 1], *si[NL80211_SURVEY_INFO_MAX + 1];
  struct genlmsghdr *g = nlmsg_data(nlmsg_hdr(msg));
  nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(g, 0), genlmsg_attrlen(g, 0), NULL);
  if (!tb[NL80211_ATTR_SURVEY_INFO]) return NL_SKIP;
  if (nla_parse_nested(si, NL80211_SURVEY_INFO_MAX, tb[NL80211_ATTR_SURVEY_INFO], NULL)) return NL_SKIP;
  if (!si[NL80211_SURVEY_INFO_FREQUENCY] || nla_get_u32(si[NL80211_SURVEY_INFO_FREQUENCY]) != r->freq)
    return NL_SKIP;
  r->found = 1;
  r->out->active_ms = si[NL80211_SURVEY_INFO_TIME] ? (uint32_t)nla_get_u64(si[NL80211_SURVEY_INFO_TIME]) : 0;
  r->out->busy_ms = si[NL80211_SURVEY_INFO_TIME_BUSY] ? (uint32_t)nla_get_u64(si[NL80211_SURVEY_INFO_TIME_BUSY]) : 0;
  r->out->rx_ms = si[NL80211_SURVEY_INFO_TIME_RX] ? (uint32_t)nla_get_u64(si[NL80211_SURVEY_INFO_TIME_RX]) : 0;
  r->out->tx_ms = si[NL80211_SURVEY_INFO_TIME_TX] ? (uint32_t)nla_get_u64(si[NL80211_SURVEY_INFO_TIME_TX]) : 0;
  return NL_SKIP;
}
static int sv_finish(struct nl_msg *msg, void *arg) { (void)msg; ((struct sv_req *)arg)->done = 1; return NL_SKIP; }
static int sv_err(struct sockaddr_nl *nla, struct nlmsgerr *e, void *arg) {
  (void)nla; (void)e; ((struct sv_req *)arg)->done = -1; return NL_STOP;
}

static int n_survey(void *c, uint8_t ch, struct survey_sample *out) {
  (void)c;
  struct sv_req r = {5000u + 5u * ch, 0, 0, out};
  struct nl_msg *m = nlmsg_alloc();
  struct nl_cb *cb = nl_cb_alloc(NL_CB_DEFAULT);
  if (!m || !cb) { if (m) nlmsg_free(m); if (cb) nl_cb_put(cb); return -1; }
  genlmsg_put(m, 0, 0, N.fam, 0, NLM_F_DUMP, NL80211_CMD_GET_SURVEY, 0);
  nla_put_u32(m, NL80211_ATTR_IFINDEX, (uint32_t)N.ifindex);
  nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, sv_valid, &r);
  nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, sv_finish, &r);
  nl_cb_err(cb, NL_CB_CUSTOM, sv_err, &r);
  int rc = nl_send_auto_complete(N.sk, m);
  nlmsg_free(m);
  while (rc >= 0 && r.done == 0) rc = nl_recvmsgs(N.sk, cb);
  nl_cb_put(cb);
  return (rc >= 0 && r.done > 0 && r.found) ? 0 : -1;
}

static int n_err(void *c, uint32_t *e) {
  (void)c;
  char buf[4096];
  int fd = open(N.phyerr_path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return -1;
  ssize_t n = read(fd, buf, sizeof buf - 1);
  close(fd);
  if (n <= 0) return -1;
  buf[n] = 0;
  return phyerr_sum(buf, e);
}

static struct radio_ops OPS = {n_set, n_survey, n_err, n_now, NULL, NULL};

const struct radio_ops *radio_open(const char *mon) {
  N.ifindex = (int)if_nametoindex(mon);
  if (!N.ifindex) return NULL;
  char p[96], phy[32] = "phy0";
  snprintf(p, sizeof p, "/sys/class/net/%s/phy80211/name", mon);
  FILE *f = fopen(p, "r");
  if (f) { if (fscanf(f, "%31s", phy) != 1) snprintf(phy, sizeof phy, "phy0"); fclose(f); }
  snprintf(N.phyerr_path, sizeof N.phyerr_path, "/sys/kernel/debug/ieee80211/%s/ath9k/phy_err", phy);
  N.sk = nl_socket_alloc();
  if (!N.sk) return NULL;
  if (genl_connect(N.sk) < 0 || (N.fam = genl_ctrl_resolve(N.sk, "nl80211")) < 0) {
    nl_socket_free(N.sk);   /* no leaked socket on a failed open */
    N.sk = NULL;
    return NULL;
  }
  return &OPS;
}
