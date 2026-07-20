# SPDX-License-Identifier: 0BSD
#
# p96cts -- P96 driver conformance test suite (AmigaOS m68k target).
#
# Needs an m68k-amigaos-gcc and the Picasso96 developer includes. The default
# P96INC is where the amiga-gcc toolchain ships them, so a containerised build
# needs no arguments at all:
#
#   make docker-build
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

DOCKER_RUN = docker run --rm --user $$(id -u):$$(id -g) -v .:/src -w /src stefanreinauer/amiga-gcc:gcc-v16.1

TARGET   = p96cts
# Testcase scenes live in tests/, one translation unit per group; the harness
# and the PNG codec stay at the top level.
OBJS     = p96cts.o png.o backdrop.o \
           tests/drawline.o tests/fillrect.o tests/copyrect.o

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
ALL_CFLAGS = $(CFLAGS) -noixemul -I. -I$(P96INC) $(PNGINC)

all: $(TARGET)

# libpng needs libm for its gamma arithmetic; both are software floating
# point, so no FPU is required at runtime.
$(TARGET): $(OBJS) $(PNGLIB)
	$(CC) $(ALL_CFLAGS) -o $@ $(OBJS) $(PNGLIB) -lm

%.o: %.c p96cts.h
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

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

docker-thirdparty:
	$(DOCKER_RUN) bash third_party/build.sh

docker-release:
	$(DOCKER_RUN) make release TAG=$(TAG)

.PHONY: all clean release docker-build docker-clean docker-thirdparty docker-release
