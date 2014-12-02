
C99 = -std=c99

LDFLAGS += -lm
CFLAGS += -D_FORTIFY_SOURCE=2 -Wall -Os -g

PREFIX ?= /usr
PKG_CONFIG_PATH = ${PREFIX}/lib/pkgconfig:${PREFIX}/lib64/pkgconfig
PKG_CONFIG = PKG_CONFIG_PATH="$$PKG_CONFIG_PATH:${PKG_CONFIG_PATH}" pkg-config

GAMMU_LDFLAGS := $(shell $(PKG_CONFIG) --libs gammu 2>/dev/null)
GAMMU_CFLAGS := $(shell $(PKG_CONFIG) --cflags gammu 2>/dev/null)

SRC_FILES := allocate.c bitfield.c json.c encoding.c gammu-json.c

ifeq ($(shell uname -s),Darwin)
  LDFLAGS_EXTRA += -liconv
endif

ifeq ($(filter clean distclean, $(MAKECMDGOALS)),)
  ifeq ($(and $(GAMMU_LDFLAGS), $(GAMMU_CFLAGS)),)
    $(info Failure while running the pkg-config utility:)
    $(info   Unable to locate compilation/link flags for `gammu`)
    $(info )
    $(info Please check that the `pkg-config` program is in your path,)
    $(info and ensure that `gammu` is installed and available to `pkg-config`.)
    $(info )
    $(error Aborting compilation due to unsatisfied dependencies)
  endif
endif


all: build-gammu-json

build-dependencies:
	cd dependencies && $(MAKE)

clean-dependencies:
	cd dependencies && $(MAKE) clean

build-gammu-json: build-dependencies
	gcc -o gammu-json $(SRC_FILES) \
		-Idependencies/jsmn -Ldependencies/jsmn -ljsmn \
		$(C99) $(CFLAGS) $(CFLAGS_EXTRA) $(LDFLAGS) \
		$(LDFLAGS_EXTRA) $(GAMMU_CFLAGS) $(GAMMU_LDFLAGS)

clean: clean-dependencies
	rm -f gammu-json

install: install-gammu-json

install-gammu-json: gammu-json
	install -o root gammu-json "$(PREFIX)/bin/gammu-json"

uninstall: uninstall-gammu-json

uninstall-gammu-json:
	rm -f "$(PREFIX)/bin/gammu-json"

