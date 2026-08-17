#
# Top level makefile for zx-spectrum-mcp.
#
# Defines the toolchain settings once, exports them, and delegates the
# actual compiling to a makefile in each library and in src. Every
# artifact lands under build/; nothing is ever written beside a source
# file.
#
# Two build modes:
#
#   release  the default. -O2, no sanitizers, and a fully static link,
#            so bin/bin/zx-spectrum-mcp has no shared library dependencies
#            and can be copied to another machine as it stands.
#   debug    -g with the address and undefined behaviour sanitizers, as
#            the coding standard requires for development. ASan cannot
#            live inside a fully static executable, so this mode links
#            the toolchain runtimes statically and libc dynamically.
#
# GPL 3.0 License (see: LICENSE)
# Copyright (C) 2026 tomaz stih
#

PROJECT   := zx-spectrum-mcp
ROOT      := $(CURDIR)
BUILD     := $(ROOT)/build
BIN       := $(ROOT)/bin

CXX       ?= g++
CC        ?= gcc
AR        ?= ar

MODE      ?= release

WARN      := -Wall -Wextra -pedantic

INCLUDES  := -I$(ROOT)/include \
             -I$(ROOT)/lib/json/include \
             -I$(ROOT)/lib/mcp/include \
             -I$(ROOT)/lib/png/include \
             -I$(ROOT)/lib/spectrum/include \
             -I$(ROOT)/lib/z80/include \
             -I$(ROOT)/lib/miniz/include

ifeq ($(MODE),debug)
  OPT      := -O1 -g -fno-omit-frame-pointer
  SAN      := -fsanitize=address,undefined
  LINKMODE := -static-libasan -static-libstdc++ -static-libgcc
  SUFFIX   := -debug
else
  OPT      := -O2 -DNDEBUG
  SAN      :=
  LINKMODE := -static
  SUFFIX   :=
endif

# miniz is third-party C. It is built without the ZIP and stdio APIs,
# which this project does not use; see lib/miniz/UPSTREAM.md.
MINIZ_DEFS := -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_STDIO \
              -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES

# Its own sources include their headers unqualified, so the header
# directory itself has to be on the path. Project code includes them as
# <miniz/miniz.h> and uses the path in INCLUDES instead.
MINIZ_INC  := -I$(ROOT)/lib/miniz/include/miniz

CXXFLAGS  := -std=c++20 $(WARN) $(OPT) $(SAN) $(INCLUDES)
CFLAGS    := -std=c11 -O2 $(INCLUDES)
LDFLAGS   := $(SAN) $(LINKMODE)

OBJROOT   := $(BUILD)/$(MODE)
TARGET    := $(BIN)/bin/$(PROJECT)$(SUFFIX)

# Unix installation paths. DESTDIR stages the tree without changing the
# paths recorded by PREFIX, as packaging tools conventionally expect.
PREFIX    ?= /usr/local
DESTDIR   ?=
DATADIR   := $(PREFIX)/share/$(PROJECT)
DOCDIR    := $(PREFIX)/share/doc/$(PROJECT)

# Dependency order matters to the static linker: a library must appear
# before the ones it needs. mcp needs json, png needs miniz.
LIB_NAMES := spectrum mcp png json miniz
LINK_LIBS := $(foreach n,$(LIB_NAMES),$(OBJROOT)/lib$(n).a)

# The tools live in src/ but are archived so the tests can link them
# without pulling in main().
TOOLS_LIB := $(OBJROOT)/libtools.a

export ROOT BUILD BIN OBJROOT CXX CC AR CXXFLAGS CFLAGS LDFLAGS
export MINIZ_DEFS MINIZ_INC SUFFIX TARGET LINK_LIBS TOOLS_LIB PROJECT MODE

.PHONY: all libs app test run-tests debug release clean distclean help \
        run list-tools install install-release package package-release \
        $(LIB_NAMES)

all: app

help:
	@echo "targets:"
	@echo "  all         build the server (release, statically linked)"
	@echo "  debug       build with sanitizers into bin/bin/$(PROJECT)-debug"
	@echo "  test        build and run the test suite with sanitizers"
	@echo "  run         build and start the server on stdio"
	@echo "  list-tools  build and print the tool schemas as JSON"
	@echo "  package     stage a copyable bin/ and share/ tree under bin/"
	@echo "  install     install under PREFIX (default /usr/local)"
	@echo "  clean       remove build/ and bin/ entirely"
	@echo "  distclean   compatibility alias for clean"
	@echo ""
	@echo "variables:"
	@echo "  MODE=debug|release   (currently $(MODE))"
	@echo "  PREFIX=/usr/local    installation prefix"
	@echo "  DESTDIR=path         optional installation staging root"

libs: $(LIB_NAMES)

$(LIB_NAMES):
	@$(MAKE) --no-print-directory -C lib/$@

# spectrum and mcp are independent of each other but both are needed
# before the application links.
app: libs
	@$(MAKE) --no-print-directory -C src

debug:
	@$(MAKE) --no-print-directory MODE=debug all

release:
	@$(MAKE) --no-print-directory MODE=release all

# The suite is always built with sanitizers: a test that passes while
# corrupting memory is not a passing test.
#
# This re-enters this makefile rather than passing MODE straight down to
# the sub-makefiles. They take OBJROOT from the environment and would
# otherwise keep the parent's value, putting debug objects in the
# release tree.
test:
	@$(MAKE) --no-print-directory MODE=debug run-tests

run-tests: libs
	@$(MAKE) --no-print-directory -C src toolslib
	@$(MAKE) --no-print-directory -C tests run

run: app
	@$(TARGET)

list-tools: app
	@$(TARGET) --list-tools

# Always install the static release binary, even when the caller currently
# has MODE=debug in its environment. DESTDIR supports staged packages and
# distro packaging without granting this make process root access.
install:
	@$(MAKE) --no-print-directory MODE=release install-release \
	    PREFIX="$(PREFIX)" DESTDIR="$(DESTDIR)"

install-release: app
	@install -d "$(DESTDIR)$(PREFIX)/bin" \
	    "$(DESTDIR)$(DATADIR)/roms" "$(DESTDIR)$(DOCDIR)"
	@install -m 0755 "$(TARGET)" \
	    "$(DESTDIR)$(PREFIX)/bin/$(PROJECT)"
	@install -m 0644 "$(ROOT)/data/roms/README.md" \
	    "$(DESTDIR)$(DATADIR)/roms/README.md"
	@for rom in "$(ROOT)"/data/roms/*.rom; do \
	    test -f "$$rom" || continue; \
	    install -m 0644 "$$rom" "$(DESTDIR)$(DATADIR)/roms/"; \
	done
	@install -m 0644 "$(ROOT)/README.md" "$(ROOT)/LICENSE" \
	    "$(ROOT)/docs/manuals/USER-GUIDE.md" "$(DESTDIR)$(DOCDIR)/"
	@echo "installed $(PROJECT) under $(DESTDIR)$(PREFIX)"

# Add read-only data and documentation beside the already-built bin/
# directory. The resulting bin/bin and bin/share directories can be copied
# directly under either /usr/local or an application directory in /opt.
package:
	@$(MAKE) --no-print-directory MODE=release package-release

package-release: app
	@rm -rf "$(BIN)/share" "$(BIN)/package"
	@rm -f "$(BIN)/$(PROJECT)" "$(BIN)/$(PROJECT)-debug"
	@rm -f "$(BIN)/bin/$(PROJECT)-debug"
	@chmod 0755 "$(TARGET)"
	@install -d "$(BIN)/share/$(PROJECT)/roms" \
	    "$(BIN)/share/doc/$(PROJECT)"
	@install -m 0644 "$(ROOT)/data/roms/README.md" \
	    "$(BIN)/share/$(PROJECT)/roms/README.md"
	@for rom in "$(ROOT)"/data/roms/*.rom; do \
	    test -f "$$rom" || continue; \
	    install -m 0644 "$$rom" "$(BIN)/share/$(PROJECT)/roms/"; \
	done
	@install -m 0644 "$(ROOT)/README.md" "$(ROOT)/LICENSE" \
	    "$(ROOT)/docs/manuals/USER-GUIDE.md" \
	    "$(BIN)/share/doc/$(PROJECT)/"
	@echo "package tree: $(BIN)/{bin,share}"

clean:
	@rm -rf $(BUILD) $(BIN)
	@echo "removed build/ and bin/"

distclean: clean
