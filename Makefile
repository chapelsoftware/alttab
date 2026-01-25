# alttab - Alt+Tab window switcher
# Makefile

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g
LDFLAGS =

# pkg-config dependencies
PKG_DEPS = x11 xcomposite xrender cairo xinerama

CFLAGS += $(shell pkg-config --cflags $(PKG_DEPS))
LDFLAGS += $(shell pkg-config --libs $(PKG_DEPS))

# Directories
SRCDIR = src
BUILDDIR = build

# Source files
SRCS = $(wildcard $(SRCDIR)/*.c)
OBJS = $(SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

# Target
TARGET = alttab

# Installation prefix
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

.PHONY: all clean install uninstall

all: $(BUILDDIR) $(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILDDIR) $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
