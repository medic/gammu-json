
LDFLAGS := -lm
CFLAGS := -Wall -std=c99 -g

PREFIX ?= /usr
PKG_CONFIG = PKG_CONFIG_PATH="$$PKG_CONFIG_PATH:$(PREFIX)/lib/pkgconfig" pkg-config

GAMMU_LDFLAGS := $(shell $(PKG_CONFIG) --libs gammu)
GAMMU_CFLAGS := $(shell $(PKG_CONFIG) --cflags gammu)

all: gammu-json

gammu-json:
	gcc -o gammu-json gammu-json.c $(CFLAGS) $(LDFLAGS) $(GAMMU_CFLAGS) $(GAMMU_LDFLAGS)

clean:
	rm -f gammu-json

