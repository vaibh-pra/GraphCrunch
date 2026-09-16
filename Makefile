# GraphCrunch
#
# nauty is NOT bundled. Fetch and build it first with
#     ./scripts/get_nauty.sh
# or point NAUTYDIR at an existing nauty tree
#     make NAUTYDIR=/path/to/nauty2_8_8
#
# Note on linking. nauty's build produces an archive named nauty.a, not
# libnauty.a, so it is linked by path. A plain -lnauty will not find it.

NAUTYDIR ?= ./nauty
CC       ?= gcc
CFLAGS   ?= -O3 -Wall
CPPFLAGS += -I$(NAUTYDIR)
NAUTYLIB  = $(NAUTYDIR)/nauty.a
LDLIBS    = -lm

BIN = automorph_serial_col

all: $(BIN)

$(BIN): src/automorph_serial_col.c $(NAUTYLIB)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $< $(NAUTYLIB) $(LDLIBS)

$(NAUTYLIB):
	@echo "nauty not found at $(NAUTYLIB)."
	@echo "Run  ./scripts/get_nauty.sh   or   make NAUTYDIR=/path/to/nauty"
	@false

nauty:
	./scripts/get_nauty.sh

test: $(BIN)
	python3 tests/test_orbits.py

example: $(BIN)
	python3 src/orbits_serial.py --edges examples/mygraph.txt --out -

clean:
	rm -f $(BIN)

distclean: clean
	rm -rf nauty nauty2_8_8 nauty2_8_8.tar.gz

.PHONY: all nauty test example clean distclean
