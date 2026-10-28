PROJECT := liconv
TARGET := iconv.so
LUA_BIN ?= luajit
CC ?= cc
PKG_CONFIG ?= pkg-config

# Install next to the luajit that will load the module: /opt/homebrew with
# Homebrew on Apple Silicon, /usr/local for a source build on Linux.
LUA_BIN_PATH := $(shell command -v $(LUA_BIN) 2>/dev/null)
LUA_PREFIX := $(patsubst %/bin/,%,$(dir $(LUA_BIN_PATH)))
ifeq ($(strip $(LUA_PREFIX)),)
LUA_PREFIX := /usr/local
endif
PREFIX ?= $(LUA_PREFIX)

UNAME_S := $(shell uname -s)

ifeq ($(filter Linux Darwin,$(UNAME_S)),)
$(error unsupported platform '$(UNAME_S)'; first version supports Linux and macOS only)
endif

# Prefer the headers of the luajit we install for (pkg-config, then its
# prefix), then fall back to a search of common locations.
ifeq ($(strip $(LUA_INC)),)
LUA_INC := $(patsubst -I%,%,$(filter -I%,$(shell $(PKG_CONFIG) --cflags luajit 2>/dev/null)))
endif
ifeq ($(strip $(LUA_INC)),)
LUA_INC := $(shell test -f "$(LUA_PREFIX)/include/luajit-2.1/lua.h" && printf '%s' "$(LUA_PREFIX)/include/luajit-2.1")
endif
ifeq ($(strip $(LUA_INC)),)
LUA_INC := $(shell for d in /usr/include/lua5.1 /usr/local/include/luajit-2.1 /usr/local/include/lua5.1 /opt/homebrew/include/luajit-2.1 /opt/homebrew/include/lua5.1 /opt/local/include/luajit-2.1 /opt/local/include/lua5.1; do if test -f "$$d/lua.h"; then printf '%s' "$$d"; break; fi; done)
endif

ifeq ($(strip $(LUA_INC)),)
$(error LUA_INC is not set and no Lua 5.1/LuaJIT headers were found)
endif

CFLAGS ?= -O2 -fPIC
CPPFLAGS ?=
WARNINGS ?= -Wall -Wextra
LUA_LIBS ?=
ICONV_INC ?=
ICONV_LIBS ?=

CPPFLAGS += -I$(LUA_INC)
ifneq ($(strip $(ICONV_INC)),)
CPPFLAGS += -I$(ICONV_INC)
endif
CFLAGS += $(WARNINGS)

ifeq ($(UNAME_S),Darwin)
MODULE_LDFLAGS := -bundle -undefined dynamic_lookup
# macOS keeps iconv(3) in libiconv.dylib, not in libSystem, so with nothing
# linked the module loads but fails at dlopen time with
# "symbol not found in flat namespace '_iconv'". -undefined dynamic_lookup
# only defers Lua's own symbols; iconv has to be linked explicitly.
ICONV_LIBS += -liconv
else
MODULE_LDFLAGS := -shared
endif

LIBDIR := $(PREFIX)/lib/lua/5.1
SRC := src/iconv.c
OBJ := build/iconv.o

.PHONY: all test install clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(MODULE_LDFLAGS) $(LDFLAGS) -o $@ $^ $(LUA_LIBS) $(ICONV_LIBS)

build:
	mkdir -p $@

build/iconv.o: $(SRC) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	LUA_CPATH="$(CURDIR)/?.so;;" $(LUA_BIN) tests/basic.lua

install: $(TARGET)
	mkdir -p "$(DESTDIR)$(LIBDIR)"
	install -m 755 $(TARGET) "$(DESTDIR)$(LIBDIR)/$(TARGET)"

clean:
	rm -rf build $(TARGET)
