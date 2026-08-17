#
# Builds the server executable.
#
# The tools are archived into libtools.a rather than being linked
# straight into the executable, because the test suite needs them too
# and cannot pull in a second main().
#
# Driven by the top level makefile, which exports the toolchain
# settings and has already built the libraries this links against.
#
# GPL 3.0 License (see: LICENSE)
# Copyright (C) 2026 tomaz stih
#

SRCDIR   := $(ROOT)/src
OBJDIR   := $(OBJROOT)/src

TOOL_SRC := $(wildcard $(SRCDIR)/tools/*.cpp)
TOOL_OBJ := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(TOOL_SRC))

MAIN_OBJ := $(OBJDIR)/main.o

DEPS     := $(TOOL_OBJ:.o=.d) $(MAIN_OBJ:.o=.d)

.PHONY: all toolslib clean

all: $(TARGET)

toolslib: $(TOOLS_LIB)

$(TOOLS_LIB): $(TOOL_OBJ)
	@mkdir -p $(dir $@)
	@$(AR) rcs $@ $^
	@echo "  AR      $(notdir $@)"

$(TARGET): $(MAIN_OBJ) $(TOOLS_LIB) $(LINK_LIBS)
	@mkdir -p $(dir $@)
	@$(CXX) $(MAIN_OBJ) $(TOOLS_LIB) $(LINK_LIBS) $(LDFLAGS) -o $@
	@echo "  LINK    $(notdir $@)"

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
	@echo "  CXX     src/$(patsubst $(SRCDIR)/%,%,$<)"

clean:
	@rm -rf $(OBJDIR) $(TOOLS_LIB) $(TARGET)

-include $(DEPS)
