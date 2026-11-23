# hanzi -- desktop (SDL2) and Kobo (framebuffer) builds.
#
#   make            desktop binary  -> out/hanzi
#   make kobo       Kobo binary     -> out/hanzi-kobo   (needs arm cross-gcc)
#   make run        build and run on the desktop

SRC      := src/core/gfx.cpp src/core/db.cpp src/core/srs.cpp src/core/match.cpp src/core/pic.cpp src/core/ocr.cpp src/core/pdf.cpp \
            src/ui/ui.cpp src/ui/app.cpp src/main.cpp
SQLITE   := src/vendor/sqlite3.c
# -Wno-format-truncation: labels and toasts are deliberately clipped to fit a
# fixed-width screen; snprintf doing exactly that is the intent, not a defect.
WARN     := -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation
# gnu++17, not plain c++17: strict ISO hides the POSIX declarations this needs.
CXXFLAGS := -O2 -std=gnu++17 $(WARN)
SQLFLAGS := -O1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=0 \
            -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_OMIT_DEPRECATED

CROSS    ?= arm-linux-gnueabihf-
CXX      := g++
CROSS_TC ?= arm-linux-gnueabihf
FBINK_DIR := src/vendor/FBInk
FBINK_LIB := $(FBINK_DIR)/Release/libfbink.a
# Headers too: a change to one alone (a table size in db.h, a struct in
# mtstate.h) rebuilt nothing, and deploy copied the stale binary.
HDRS     := $(wildcard src/*/*.h) src/main.cpp

all: out/hanzi

# MuPDF and ncnn, static (deps, deps-arm, deps-test): after `all`, which stays
# the default.
include mk/deps.mk

out/sqlite3.o: $(SQLITE)
	@mkdir -p out
	$(CC) $(SQLFLAGS) -c -o $@ $<

out/hanzi: $(SRC) $(HDRS) out/sqlite3.o src/backend/surf_sdl.cpp $(DEPS_HOST_STAMP)
	@mkdir -p out
	$(CXX) $(CXXFLAGS) $(DEPS_CFLAGS_HOST) -DHANZI_DESKTOP -o $@ $(SRC) src/backend/surf_sdl.cpp out/sqlite3.o \
	    $(shell pkg-config --cflags --libs sdl2) $(DEPS_LIBS_HOST) -lm

out/sqlite3-arm.o: $(SQLITE)
	@mkdir -p out
	$(CROSS)gcc $(SQLFLAGS) -c -o $@ $<

# FBInk names its version with git describe, which in this vendored copy (no
# .git of its own) would describe hanzi-trainer's commit instead. So it is
# given the commit that is vendored (tools/fetch-deps.sh).
FBINK_VERSION := v1.25.0-543-g886f25f

$(FBINK_LIB):
	$(MAKE) -C $(FBINK_DIR) CROSS_TC=$(CROSS_TC) KOBO=1 MINIMAL=1 FBINK_VERSION=$(FBINK_VERSION) \
	    staticlib -j$(shell nproc)

kobo: out/hanzi-kobo
out/hanzi-kobo: $(SRC) $(HDRS) out/sqlite3-arm.o src/backend/surf_fb.cpp src/backend/mtstate.cpp $(FBINK_LIB) $(DEPS_ARM_STAMP)
	@mkdir -p out
	$(CROSS)g++ $(CXXFLAGS) $(DEPS_CFLAGS_ARM) -static -o $@ $(SRC) src/backend/surf_fb.cpp src/backend/mtstate.cpp \
	    out/sqlite3-arm.o $(FBINK_LIB) $(DEPS_LIBS_ARM) -lm
	$(CROSS)strip $@ || true
	@ls -la $@
	@file $@

run: out/hanzi
	cd out && ./hanzi --font ../data/fonts/LXGWWenKaiLite-Regular.ttf

clean:
	rm -f out/hanzi out/hanzi-kobo out/*.o

.PHONY: all kobo run clean
