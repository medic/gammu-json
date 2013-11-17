
CFLAGS += -DJSMN_STRICT -DJSMN_PARENT_LINKS

all: libjsmn.a test

libjsmn.a: jsmn.o
	$(AR) rc $@ $^

%.o: %.c jsmn.h
	$(CC) -c $(CFLAGS) $< -o $@

test: jsmn-test
	./jsmn-test

jsmn_test: jsmn-test.o
	$(CC) -L. -ljsmn $< -o $@

jsmn_test.o: jsmn_test.c libjsmn.a

clean:
	rm -f jsmn.o jsmn_test.o
	rm -f jsmn_test
	rm -f libjsmn.a

.PHONY: all clean test

