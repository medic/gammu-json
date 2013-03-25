
LDFLAGS := -lm
CFLAGS := -Wall -std=c99

GAMMU_LDFLAGS := $(shell pkg-config --libs gammu)
GAMMU_CFLAGS := $(shell pkg-config --cflags gammu)

gammu-json:
	gcc -o gammu-json $(CFLAGS) $(LDFLAGS) \
	  $(GAMMU_CFLAGS) $(GAMMU_LDFLAGS) gammu-json.c

clean:
	rm -f gammu-json

