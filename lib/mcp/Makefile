#
# Builds libmcp.a into $(OBJROOT). Driven by the top level makefile,
# which exports the toolchain settings; running this directly will not
# work.
#
# GPL 3.0 License (see: LICENSE)
# Copyright (C) 2026 tomaz stih
#

NAME    := mcp
SRCDIR  := $(ROOT)/lib/$(NAME)
OBJDIR  := $(OBJROOT)/$(NAME)

SOURCES := $(wildcard $(SRCDIR)/*.cpp)
OBJECTS := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SOURCES))
DEPS    := $(OBJECTS:.o=.d)

LIBRARY := $(OBJROOT)/lib$(NAME).a

.PHONY: all clean

all: $(LIBRARY)

$(LIBRARY): $(OBJECTS)
	@mkdir -p $(dir $@)
	@$(AR) rcs $@ $^
	@echo "  AR      $(notdir $@)"

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@
	@echo "  CXX     $(NAME)/$(notdir $<)"

clean:
	@rm -rf $(OBJDIR) $(LIBRARY)

-include $(DEPS)
