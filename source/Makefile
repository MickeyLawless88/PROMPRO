# PROMPRO - macOS / Linux / BSD
#   make            build ./prompro
#   make install    copy to /usr/local/bin
CC      ?= cc
CFLAGS  ?= -O2

prompro: prompro.c
	$(CC) $(CFLAGS) -o $@ prompro.c

install: prompro
	install -m 755 prompro /usr/local/bin/prompro

clean:
	rm -f prompro

.PHONY: install clean
