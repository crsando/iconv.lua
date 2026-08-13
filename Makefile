PROJECT := liconv
TARGET := iconv.so
PREFIX ?= /usr/local
LUA_BIN ?= luajit
CC ?= cc

UNAME_S := $(shell uname -s)

ifeq ($(filter Linux Darwin,$(UNAME_S)),)
$(error unsupported platform '$(UNAME_S)'; first version supports Linux and macOS only)
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
