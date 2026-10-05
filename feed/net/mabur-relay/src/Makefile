# Plain make. OpenWrt passes CC/CFLAGS on the command line, so extra flags go
# through `override` to survive that.
CC ?= cc
CFLAGS ?= -O2
override CFLAGS += -std=gnu99 -Wall -Wextra
TESTDIR ?= ../tests
RADIO ?= fake
LDLIBS ?=

LIB_OBJS = wire.o filter.o rtap.o ws.o tune.o txrt.o phyerr.o sweep.o
RELAY_OBJS = relay.o

all: mabur-relay

mabur-relay: main.o $(RELAY_OBJS) $(LIB_OBJS) radio_$(RADIO).o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

unit_tests: $(TESTDIR)/unit_main.c $(TESTDIR)/test_wire.c $(TESTDIR)/test_filter.c $(TESTDIR)/test_rtap.c $(TESTDIR)/test_ws.c $(TESTDIR)/test_tune.c $(TESTDIR)/test_txrt.c $(TESTDIR)/test_phyerr.c $(TESTDIR)/test_sweep.c $(LIB_OBJS)
	$(CC) $(CFLAGS) -I. -I$(TESTDIR) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o radio_*.o mabur-relay unit_tests

.PHONY: all clean
