# SPDX-License-Identifier: 0BSD
#
# p96cts -- P96 driver conformance test suite (AmigaOS m68k target).
#

# To build using the default containerized toolchain:
#   make docker-build
DOCKER_RUN = docker run --rm --user $$(id -u):$$(id -g) -v .:/src -w /src berniecodewiz/m68k-amigaos-gcc:gcc-v16.2

# Or build with a specified compiler:
#
#   make CC=m68k-amigaos-gcc
CC = m68k-amigaos-gcc

# With a toolchain that does not bundle the P96 headers (e.g. bebbo's),
# unpack P96Develop.lha and point P96_CFLAGS at the headers:
#
#   make P96_CFLAGS=-I/path/to/Picasso96Develop/Include
#
P96_CFLAGS ?=

# Allow overriding code generation flags
CFLAGS ?= -O3 -fomit-frame-pointer -m68020-60 $(WARNINGS)

# Normally bundled with AmigaPorts/m68k-amigaos-gcc
PNG_CFLAGS ?=
PNG_LIBS   ?= -lpng -lz -lm

TARGET = p96cts
AMIGA_VERSION ?= $(shell git describe --tags --dirty | sed -r 's/^(release_|v)//')
AMIGA_DATE := $(shell date '+%-d.%-m.%Y')

# Strict enough to catch the usual C mistakes without fighting the Amiga headers.
# -Wvla and -Walloca are policy: AmigaOS tasks run on a small fixed stack.
WARNINGS = -Wall -Wextra -Wshadow -Wpointer-arith -Wundef -Wwrite-strings \
	-Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition \
	-Wfloat-conversion -Wdouble-promotion -Wduplicated-cond \
	-Wduplicated-branches -Wlogical-op -Wformat=2 -Wcast-align \
	-Wnull-dereference -Wvla -Walloca

ALL_CFLAGS = $(CFLAGS) -noixemul -Isrc $(P96_CFLAGS) $(PNG_CFLAGS) \
	-DAMIGA_VERSION=\"$(AMIGA_VERSION)\" \
	-DAMIGA_DATE=\"$(AMIGA_DATE)\"

OBJS = \
	src/backdrop.o \
	src/gfx.o \
	src/glyph.o \
	src/layer.o \
	src/main.o \
	src/modes.o \
	src/palette.o \
	src/pngio.o \
	src/report.o \
	src/rtg.o \
	src/runtest.o \
	src/timer.o \
	src/wall.o \
	tests/bitmapscale.o \
	tests/bltbitmap.o \
	tests/bltpattern.o \
	tests/blttemplate.o \
	tests/clipblit.o \
	tests/drawline.o \
	tests/pixelarray.o \
	tests/rectfill.o \
	tests/scrollraster.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(ALL_CFLAGS) -o $@ $(OBJS) $(PNG_LIBS)

HEADERS  = \
	src/backdrop.h \
	src/gfx.h \
	src/glyph.h \
	src/layer.h \
	src/modes.h \
	src/p96cts.h \
	src/palette.h \
	src/pngio.h \
	src/report.h \
	src/rtg.h \
	src/runtest.h \
	src/timer.h \
	src/wall.h

%.o: %.c $(HEADERS)
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

# Keep the sources compilable as C++ too: no implicit conversions from
# void * or into enums. Syntax-only, with the C-only warnings dropped, and
# without g++'s complaint about designated initializers that leave trailing
# fields to zero -- deliberate throughout the test tables.
CXX = $(CC:gcc=g++)
CXX_WARNINGS = $(filter-out -Wstrict-prototypes -Wmissing-prototypes \
	-Wold-style-definition,$(WARNINGS)) -Wno-missing-field-initializers

check-cxx:
	$(CXX) -x c++ -fsyntax-only $(filter-out $(WARNINGS),$(ALL_CFLAGS)) \
		$(CXX_WARNINGS) $(OBJS:.o=.c)

# The git tag is "v0.1"; the archive is "p96cts-0.1.lha", matching both the
# Aminet convention and the program's own "p96cts 0.1" banner.
RELDIR = $(TARGET)-$(TAG:v%=%)

# Pack the binary, its documentation and the golden set built from the same
# commit, so the archive drops onto an Amiga as a matched pair. Needs LHa for
# UNIX: distributions now ship lhasa, which only extracts, so on most hosts
# this wants the docker-release target below.
release: $(TARGET)
	@test -n "$(TAG)" || { echo "usage: make release TAG=v0.1" >&2; exit 2; }
	rm -rf $(RELDIR) $(RELDIR).lha
	mkdir -p $(RELDIR)
	cp $(TARGET) README.md LICENSE $(RELDIR)/
	cp -r golden $(RELDIR)/
	lha a $(RELDIR).lha $(RELDIR)/
	rm -rf $(RELDIR)
	@echo "wrote $(RELDIR).lha"

docker-build:
	$(DOCKER_RUN) make all

docker-clean:
	$(DOCKER_RUN) make clean

docker-release:
	$(DOCKER_RUN) make release TAG=$(TAG)

.PHONY: all clean check-cxx release docker-build docker-clean docker-release
