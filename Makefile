# xrandr-profile
# See LICENSE file for copyright and license details.

VERSION  ?= 0.3.0
CC       ?= cc

CFLAGS ?= -Os
CFLAGS += -std=c99 -Wall -Wextra -Wpedantic -Wpointer-arith -Wshadow -Wstrict-prototypes \
	-Wmissing-prototypes -Wold-style-definition -Wformat=2 -Wconversion -Wsign-conversion

CPPFLAGS += -MMD -MP -DVERSION=\"${VERSION}\"
LDLIBS   ?= -lX11 -lXrandr -lXrender -lm

DEBUG_CFLAGS  := -g3 -O0 -fanalyzer -fsanitize=address,undefined -fno-omit-frame-pointer
DEBUG_LDFLAGS := -fsanitize=address,leak,undefined

PREFIX    ?= /usr/local
MANPREFIX ?= ${PREFIX}/share/man

SRCDIR    := src
BUILDDIR  := build
BINDIR    := $(BUILDDIR)/bin
OBJDIR    := $(BUILDDIR)/obj
DOCDIR    := docs

BIN      := xrandr-profile
SRCS     := xrandr-profile.c profile.c utils.c xrandr.c
OBJS     := $(SRCS:%.c=$(OBJDIR)/%.o)
DEPS     := $(OBJS:.o=.d)

TARGET   := $(BINDIR)/$(BIN)

TESTSDIR := $(SRCDIR)/tests
TESTDIR  := $(BUILDDIR)/tests
TEST     := $(TESTDIR)/test-profile
TESTSRC  := $(TESTSDIR)/test-profile.c
TESTOBJS := $(OBJDIR)/profile.o $(OBJDIR)/utils.o

COLOR  ?= 1
PRINTF ?= printf

ifeq ($(COLOR),0)
COLOR_RESET  :=
COLOR_GREEN  :=
COLOR_YELLOW :=
COLOR_BLUE   :=
COLOR_CYAN   :=
else
COLOR_RESET  := \033[0m
COLOR_GREEN  := \033[1;32m
COLOR_YELLOW := \033[1;33m
COLOR_BLUE   := \033[1;34m
COLOR_CYAN   := \033[1;36m
endif

all: $(TARGET)

debug: CFLAGS += $(DEBUG_CFLAGS)
debug: LDFLAGS += $(DEBUG_LDFLAGS)
debug: clean all

$(TARGET): $(OBJS) | $(BINDIR)
	@$(PRINTF) "$(COLOR_GREEN)Linking:$(COLOR_RESET) %s\n" "$@"
	@$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	@$(PRINTF) "$(COLOR_BLUE)Compiling:$(COLOR_RESET) %s\n" "$@"
	@$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BINDIR) $(OBJDIR):
	@mkdir -p $@

clean:
	@$(PRINTF) "$(COLOR_YELLOW)Cleaning:$(COLOR_RESET) %s\n" "$(BUILDDIR)"
	@rm -rf $(BUILDDIR)

install: $(TARGET)
	@$(PRINTF) "$(COLOR_CYAN)Installing $(BIN) at:$(COLOR_RESET) %s\n" "$(DESTDIR)$(PREFIX)/bin/$(BIN)"
	@install -d $(DESTDIR)$(PREFIX)/bin
	@install -d $(DESTDIR)$(MANPREFIX)/man1
	@install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(BIN)
	@sed "s/VERSION/$(VERSION)/g" < $(DOCDIR)/$(BIN).1 > $(DESTDIR)$(MANPREFIX)/man1/$(BIN).1
	@chmod 644 $(DESTDIR)$(MANPREFIX)/man1/$(BIN).1

uninstall:
	@$(PRINTF) "$(COLOR_CYAN)Uninstalling $(BIN) from:$(COLOR_RESET) %s\n" "$(DESTDIR)$(PREFIX)/bin/$(BIN)"
	@rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN) $(DESTDIR)$(MANPREFIX)/man1/$(BIN).1

test: $(TESTOBJS) | $(OBJDIR)
	@mkdir -p $(TESTDIR)
	@$(PRINTF) "$(COLOR_BLUE)Testing:$(COLOR_RESET) %s\n" "$(TEST)"
	@$(CC) $(CFLAGS) -I$(SRCDIR) -o $(TEST) $(TESTSRC) $(TESTOBJS)
	@$(TEST)

-include $(DEPS)

.PHONY: all debug clean install uninstall test
