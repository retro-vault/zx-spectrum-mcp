#
# Builds and runs the test suite.
#
# Every tests/test_*.cpp becomes its own executable with its own
# main(). There is no framework to register with, so adding a test means
# adding a file and nothing else.
#
# Driven by the top level makefile, which builds the libraries first and
# always selects the sanitizer build for this target.
#
# GPL 3.0 License (see: LICENSE)
# Copyright (C) 2026 tomaz stih
#

SRCDIR  := $(ROOT)/tests
OBJDIR  := $(OBJROOT)/tests

SOURCES := $(wildcard $(SRCDIR)/test_*.cpp)
BINARIES := $(patsubst $(SRCDIR)/test_%.cpp,$(OBJDIR)/test_%,$(SOURCES))
DEPS    := $(BINARIES:=.d)

.PHONY: all run clean

all: $(BINARIES)

$(OBJDIR)/test_%: $(SRCDIR)/test_%.cpp $(TOOLS_LIB) $(LINK_LIBS)
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -I$(SRCDIR) -MMD -MP $< \
	    $(TOOLS_LIB) $(LINK_LIBS) $(LDFLAGS) -o $@
	@echo "  TEST    $(notdir $@)"

# Runs every suite and fails the build if any of them does.
run: $(BINARIES)
	@echo ""
	@failed=0; \
	for t in $(BINARIES); do \
	    $$t || failed=1; \
	done; \
	echo ""; \
	if [ $$failed -eq 0 ]; then \
	    echo "=== all test suites passed ==="; \
	else \
	    echo "=== TEST FAILURES ==="; \
	fi; \
	exit $$failed

clean:
	@rm -rf $(OBJDIR)

-include $(DEPS)
