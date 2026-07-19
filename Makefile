# SPDX-License-Identifier: 0BSD
#
# p96cts -- Picasso96 driver conformance test suite (AmigaOS m68k target).
#
# Needs an m68k-amigaos-gcc and the Picasso96 developer includes. The default
# P96INC is where the amiga-gcc toolchain ships them, so a containerised build
# needs no arguments at all:
#
#   docker run --rm --user $(id -u):$(id -u) -v .:/src -w /src stefanreinauer/amiga-gcc:gcc-v16.1 make
#
# With a toolchain that does not bundle them (e.g. bebbo's), point at an
# unpacked P96Develop.lha instead:
#
#   make CC=/path/to/bin/m68k-amigaos-gcc \
#        P96INC=/path/to/Picasso96Develop/Include

# Plain assignment, not ?=: make predefines CC as "cc". A command-line
# CC=... still overrides this.
CC       = m68k-amigaos-gcc
P96INC  ?= /opt/amiga/m68k-amigaos/include

TARGET   = p96cts
OBJS     = p96cts.o drawline.o png.o

# zlib and libpng, built for this target and committed. See
# third_party/README.md for provenance and how to rebuild them.
PNGINC   = -Ithird_party/libpng/include -Ithird_party/zlib/include
PNGLIB   = third_party/libpng/lib/libpng16.a third_party/zlib/lib/libz.a

# Strict enough to catch the usual C mistakes without fighting the Amiga
# headers. -Wstrict-prototypes and -Wold-style-definition matter here because
# these sources are otherwise easy to write in K&R style by accident.
WARNINGS = -Wall -Wextra -Wshadow -Wpointer-arith -Wundef -Wwrite-strings \
           -Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition

CFLAGS  ?= -O2 $(WARNINGS)

# Kept apart from CFLAGS: a command-line CFLAGS= replaces the variable
# entirely, and dropping -noixemul or the include path breaks the link.
ALL_CFLAGS = $(CFLAGS) -noixemul -I$(P96INC) $(PNGINC)

all: $(TARGET)

# libpng needs libm for its gamma arithmetic; both are software floating
# point, so no FPU is required at runtime.
$(TARGET): $(OBJS) $(PNGLIB)
	$(CC) $(ALL_CFLAGS) -o $@ $(OBJS) $(PNGLIB) -lm

%.o: %.c p96cts.h
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
