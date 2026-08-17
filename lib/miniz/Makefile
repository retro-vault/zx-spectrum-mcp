#
# Builds libminiz.a into $(OBJROOT).
#
# This is third-party C, not project code, so it is compiled with the C
# compiler and without the project's warning flags: upstream is not
# written to this project's standard and warnings from it would drown
# out warnings from code we can actually fix.
#
# miniz_zip.c is deliberately not listed. The ZIP APIs are disabled by
# MINIZ_NO_ARCHIVE_APIS, which leaves that file empty; see UPSTREAM.md.
#
# GPL 3.0 License (see: LICENSE)
# Copyright (C) 2026 tomaz stih
#

NAME    := miniz
SRCDIR  := $(ROOT)/lib/$(NAME)
OBJDIR  := $(OBJROOT)/$(NAME)

SOURCES := $(SRCDIR)/miniz.c \
           $(SRCDIR)/miniz_tdef.c \
           $(SRCDIR)/miniz_tinfl.c

OBJECTS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SOURCES))
DEPS    := $(OBJECTS:.o=.d)

LIBRARY := $(OBJROOT)/lib$(NAME).a

.PHONY: all clean

all: $(LIBRARY)

$(LIBRARY): $(OBJECTS)
	@mkdir -p $(dir $@)
	@$(AR) rcs $@ $^
	@echo "  AR      $(notdir $@)"

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) $(MINIZ_INC) $(MINIZ_DEFS) -MMD -MP -c $< -o $@
	@echo "  CC      $(NAME)/$(notdir $<)"

clean:
	@rm -rf $(OBJDIR) $(LIBRARY)

-include $(DEPS)
