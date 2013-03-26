
LDFLAGS := -lm
CFLAGS := -Wall -std=c99 -g

GAMMU_LDFLAGS := $(shell pkg-config --libs gammu)
GAMMU_CFLAGS := $(shell pkg-config --cflags gammu)

all: gammu-json

gammu-json:
	gcc -o gammu-json gammu-json.c $(CFLAGS) $(LDFLAGS) $(GAMMU_CFLAGS) $(GAMMU_LDFLAGS)

clean:
	rm -f gammu-json

