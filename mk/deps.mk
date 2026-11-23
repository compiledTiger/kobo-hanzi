# MuPDF (PDF pages) and ncnn (the OCR models), static, for both targets.
#
#   make deps        build them for the desktop  -> $(DEPS_DIR)/host
#   make deps-arm    build them for the Kobo     -> $(DEPS_DIR)/arm
#   make deps-test   link tests/deps_link_test.cpp both ways, run the host one
#
# They are too big for the repository and the root disk, so they are built
# on the build drive by tools/fetch-deps.sh (sources pinned by sha256 there)
# and installed under DEPS_DIR. Override it to build somewhere else:
#   make DEPS_DIR=/elsewhere/deps          (or HANZI_BUILD=/elsewhere)
# out/hanzi and out/hanzi-kobo depend on the stamp, so a missing build is
# made first; the AppImage container builds its own (tools/appimage).
#
# HANZI_BUILD's default is this machine's build drive. Anywhere else --
# a fresh clone, or the drive not mounted -- set it to a writable folder
# with about 2 GB free; the first build downloads MuPDF, ncnn and (if there
# is no cmake) CMake, so it needs the network, curl and unzip.

HANZI_BUILD ?= $(CURDIR)/.deps
DEPS_DIR    ?= $(HANZI_BUILD)/deps

# The versions and every build option are in tools/fetch-deps.sh, which names
# the stamp after a hash of them all (and of the compiler): a new version or
# a changed flag builds again, from a fresh object tree.
DEPS_STAMP   = $(shell HANZI_BUILD='$(HANZI_BUILD)' DEPS_DIR='$(DEPS_DIR)' CROSS='$(CROSS)' \
                       sh tools/fetch-deps.sh stamp $(1))
DEPS_HOST_STAMP := $(call DEPS_STAMP,host)
DEPS_ARM_STAMP  := $(call DEPS_STAMP,arm)
ifeq ($(DEPS_HOST_STAMP),)
$(error tools/fetch-deps.sh stamp host printed nothing)
endif

# -isystem: their headers are theirs, and check.sh builds with -Werror.
# ncnn's platform.h differs per architecture, so each target has its own.
MUPDF_CFLAGS_HOST := -isystem $(DEPS_DIR)/host/include
MUPDF_CFLAGS_ARM  := -isystem $(DEPS_DIR)/arm/include
NCNN_CFLAGS_HOST  := -isystem $(DEPS_DIR)/host/include
NCNN_CFLAGS_ARM   := -isystem $(DEPS_DIR)/arm/include
MUPDF_CFLAGS      := $(MUPDF_CFLAGS_HOST)
NCNN_CFLAGS       := $(NCNN_CFLAGS_HOST)

MUPDF_LIBS_HOST := $(DEPS_DIR)/host/lib/libmupdf.a $(DEPS_DIR)/host/lib/libmupdf-third.a -lm
MUPDF_LIBS_ARM  := $(DEPS_DIR)/arm/lib/libmupdf.a $(DEPS_DIR)/arm/lib/libmupdf-third.a -lm
NCNN_LIBS_HOST  := $(DEPS_DIR)/host/lib/libncnn.a -lpthread
NCNN_LIBS_ARM   := $(DEPS_DIR)/arm/lib/libncnn.a -lpthread

# Both, as the two app rules use them.
DEPS_CFLAGS_HOST := $(MUPDF_CFLAGS_HOST)
DEPS_CFLAGS_ARM  := $(MUPDF_CFLAGS_ARM)
DEPS_LIBS_HOST   := $(MUPDF_LIBS_HOST) $(NCNN_LIBS_HOST)
DEPS_LIBS_ARM    := $(MUPDF_LIBS_ARM) $(NCNN_LIBS_ARM)

# The script clears MAKEFLAGS and the *FLAGS before recursing, so a
# `make CXXFLAGS=-Werror ...` (check.sh) does not reach MuPDF or cmake; it
# is cleared here as well, for anything run before it gets there.
DEPS_ENV := env -u MAKEFLAGS -u MAKEOVERRIDES -u MFLAGS -u MAKELEVEL \
            -u CFLAGS -u CXXFLAGS -u CPPFLAGS -u LDFLAGS \
            HANZI_BUILD=$(HANZI_BUILD) DEPS_DIR=$(DEPS_DIR) CROSS=$(CROSS)

$(DEPS_HOST_STAMP):
	$(DEPS_ENV) sh tools/fetch-deps.sh deps host
$(DEPS_ARM_STAMP):
	$(DEPS_ENV) sh tools/fetch-deps.sh deps arm

deps: $(DEPS_HOST_STAMP)
deps-arm: $(DEPS_ARM_STAMP)

# Proof that both link statically and work: a PDF opened and drawn from
# memory, and an ncnn net run. The ARM one is only built (no emulator here).
out/deps_link_test: tests/deps_link_test.cpp $(DEPS_HOST_STAMP)
	@mkdir -p out
	$(CXX) -O1 -std=gnu++17 $(WARN) $(DEPS_CFLAGS_HOST) -o $@ $< $(DEPS_LIBS_HOST)

out/deps_link_test-arm: tests/deps_link_test.cpp $(DEPS_ARM_STAMP)
	@mkdir -p out
	$(CROSS)g++ -O1 -std=gnu++17 $(WARN) -static $(DEPS_CFLAGS_ARM) -o $@ $< $(DEPS_LIBS_ARM)

deps-test: out/deps_link_test out/deps_link_test-arm
	./out/deps_link_test
	@file out/deps_link_test-arm

.PHONY: deps deps-arm deps-test
