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
BUILD ?= build
AMIGA_VERSION ?= $(shell git describe --tags --dirty | sed -r 's/^(release_|v)//')
AMIGA_DATE := $(shell date '+%-d.%-m.%Y')

# Strict enough to catch the usual C mistakes without fighting the Amiga headers.
# -Wvla and -Walloca are policy: AmigaOS tasks run on a small fixed stack.
WARNINGS = -Wall -Wextra -Wshadow -Wpointer-arith -Wundef -Wwrite-strings \
	-Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition \
	-Wfloat-conversion -Wdouble-promotion -Wduplicated-cond \
	-Wduplicated-branches -Wlogical-op -Wformat=2 -Wcast-align \
	-Wnull-dereference -Wvla -Walloca

ALL_CFLAGS = $(CFLAGS) -g -noixemul -Isrc $(P96_CFLAGS) $(PNG_CFLAGS) \
	-DAMIGA_VERSION=\"$(AMIGA_VERSION)\" \
	-DAMIGA_DATE=\"$(AMIGA_DATE)\"

SRCS = \
	src/backdrop.c \
	src/gfx.c \
	src/glyph.c \
	src/layer.c \
	src/main.c \
	src/modes.c \
	src/palette.c \
	src/pngio.c \
	src/report.c \
	src/rtg.c \
	src/runtest.c \
	src/timer.c \
	src/wall.c \
	tests/bitmapscale.c \
	tests/bltbitmap.c \
	tests/bltpattern.c \
	tests/blttemplate.c \
	tests/clipblit.c \
	tests/drawline.c \
	tests/pixelarray.c \
	tests/rectfill.c \
	tests/scrollraster.c \
	tests/unusedbits.c

OBJS = $(SRCS:%.c=$(BUILD)/%.o)

all: $(TARGET)

# p96cts_unstripped is what to point m68k-amigaos-gdb at while bgdbserver runs
# the stripped p96cts on the Amiga: 17k of that is the symbol table, which is
# enough for breakpoints by name and named frames in a backtrace.
#
# Source-level debugging does not work yet. The compiler emits DWARF per
# object, but linking to hunk drops every .debug_* section, so gdb has no line
# table. -g is kept anyway -- it costs 16 bytes here, and the day the linker
# carries DWARF this starts working on its own.
UNSTRIPPED = $(TARGET)_unstripped
STRIP = $(CC:gcc=strip)

$(UNSTRIPPED): $(OBJS)
	$(CC) $(ALL_CFLAGS) -o $@ $(OBJS) $(PNG_LIBS)

# chmod because strip -o writes a fresh file with default permissions, and a
# HOSTFS mount maps the unix x bit to the Amiga e bit: without it the guest
# answers "Unknown command" even given the full path.
$(TARGET): $(UNSTRIPPED)
	$(STRIP) -o $@ $<
	chmod +x $@

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

$(BUILD)/%.o: %.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD) $(TARGET) $(UNSTRIPPED)
	rm -rf build-aminet $(TARGET)-*.lha $(TARGET)-*.readme

# Keep the sources compilable as C++ too: no implicit conversions from
# void * or into enums. Syntax-only, with the C-only warnings dropped, and
# without g++'s complaint about designated initializers that leave trailing
# fields to zero -- deliberate throughout the test tables.
CXX = $(CC:gcc=g++)
CXX_WARNINGS = $(filter-out -Wstrict-prototypes -Wmissing-prototypes \
	-Wold-style-definition,$(WARNINGS)) -Wno-missing-field-initializers

check-cxx:
	$(CXX) -x c++ -fsyntax-only $(filter-out $(WARNINGS),$(ALL_CFLAGS)) \
		$(CXX_WARNINGS) $(SRCS)

# The git tag is "v1.0"; the archive is "p96cts-1.0.lha" with its
# "p96cts-1.0.readme" beside it, which is what an Aminet upload is, and the
# program's own banner says "p96cts 1.0".
RELNAME = $(TARGET)-$(TAG:v%=%)

# The drawer inside the archive keeps a fixed name, so an Amiga that already
# has one unpacks the new release over it instead of beside it.
AMINET_DIR = build-aminet/P96CTS

# The BitMapScale scenes read their source picture at run time, by a path
# relative to the current directory, so the archive is incomplete without it:
# without the file every scale scene renders the cleared background and fails.
# The other logos are documentation and stay out.
LOGO = logo/p96cts-logo-320x200x8.png

# Pack the binary, its documentation and the golden set built from the same
# commit, so the archive drops onto an Amiga as a matched pair. The readme is
# generated from aminet/p96cts.readme.in and goes both inside the drawer and
# next to the archive, where Aminet reads it. Needs LHa for UNIX:
# distributions now ship lhasa, which only extracts, so on most hosts this
# wants the docker-release target below.
release: $(TARGET)
	@test -n "$(TAG)" || { echo "usage: make release TAG=v1.0" >&2; exit 2; }
	rm -rf build-aminet $(RELNAME).lha $(RELNAME).readme
	mkdir -p $(AMINET_DIR)
	cp $(TARGET) README.md LICENSE $(AMINET_DIR)/
	cp -r golden $(AMINET_DIR)/
	@mkdir -p $(AMINET_DIR)/logo
	cp $(LOGO) $(AMINET_DIR)/logo/
	sed 's/@VERSION@/$(TAG:v%=%)/g' aminet/$(TARGET).readme.in > $(RELNAME).readme
	cp $(RELNAME).readme $(AMINET_DIR)/$(TARGET).readme
	cd build-aminet && lha aq ../$(RELNAME).lha P96CTS
	rm -rf build-aminet
	@echo "wrote $(RELNAME).lha and $(RELNAME).readme"

docker-build:
	$(DOCKER_RUN) make all

docker-clean:
	$(DOCKER_RUN) make clean

docker-release:
	$(DOCKER_RUN) make release TAG=$(TAG)

.PHONY: all clean check-cxx release docker-build docker-clean docker-release
