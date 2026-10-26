# Cartarchy for the Amiga (Workbench 1.3 and later, 68000 up), talking to
# the Cartarchy server through FujiNet NIO (fujinet-nio.device).
#
# Needs bebbo's m68k-amigaos-gcc and a fujinet-nio-workspace checkout for
# fujinet-nio-lib:
#
#   make                                  build/Cartarchy + Cartarchy.info
#   make NIO_WORKSPACE=/path/to/workspace
#   make test                             rules and JSON checks on this machine
#
# It links libnix's nix13 runtime, so one binary runs on Kickstart 1.3
# through 3.x. The map comes from the iOS app's own board (../ios).

NIO_WORKSPACE ?= $(abspath ../../fujinet-nio-workspace)
NIO_LIB       ?= $(NIO_WORKSPACE)/repos/fujinet-nio-lib
IOS           := ../ios/cartarchy

PROGRAM := Cartarchy
CC      := m68k-amigaos-gcc
# -mcrt=nix13 puts the 1.3 NDK first, so a call that needs Kickstart 2.0
# fails to compile instead of crashing on an A500.
CFLAGS  := -std=gnu99 -Wall -O2 -mcpu=68000 -msoft-float -mcrt=nix13 \
           -Isrc -I$(NIO_LIB)/include
LDFLAGS := -mcpu=68000 -msoft-float -mcrt=nix13

SRCS := $(wildcard src/*.c)
GEN  := build/gen/map_data.c build/gen/logo.c
OBJS := $(SRCS:src/%.c=build/obj/%.o) $(GEN:build/gen/%.c=build/obj/gen/%.o)
HEADERS := $(wildcard src/*.h)
NIO_LIB_FILE := $(NIO_LIB)/build/fujinet-nio-amiga.a

all: build/$(PROGRAM) build/$(PROGRAM).info

# No -lamiga here: the driver adds it after libnix, and ahead of libnix its
# RawDoFmt sprintf() would read %d as 16 bits
build/$(PROGRAM): $(OBJS) $(NIO_LIB_FILE)
	$(CC) -o $@ $(OBJS) $(NIO_LIB_FILE) $(LDFLAGS)

build/obj/%.o: src/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

build/obj/gen/%.o: build/gen/%.c src/mapdata.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

build/gen/map_data.c: support/mkmap.py $(IOS)/board_paths.json $(IOS)/Models.swift
	@mkdir -p $(dir $@)
	python3 support/mkmap.py $(IOS) $@

# art/*.png come from support/mklogo.py (Pillow) and are committed
build/gen/logo.c: art/logo.png support/png2planes.py
	@mkdir -p $(dir $@)
	python3 support/png2planes.py art/logo.png logo $@

build/$(PROGRAM).info: art/icon.png support/mkicon.py
	@mkdir -p build
	python3 support/mkicon.py art/icon.png $@

$(NIO_LIB_FILE):
	$(MAKE) -C $(NIO_LIB) amiga

# The portable sources, natively, with sanitizers
HOST_SRCS := tests/host_test.c src/json.c src/rules.c src/state.c build/gen/map_data.c
test: build/gen/map_data.c
	@mkdir -p build/host
	cc -std=gnu99 -Wall -Wextra -Wno-deprecated-declarations -g -fsanitize=address,undefined \
	   -Itests/stub -Isrc -o build/host/host_test $(HOST_SRCS)
	build/host/host_test

clean:
	rm -rf build

.PHONY: all test clean
