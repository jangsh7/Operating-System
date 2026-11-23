CC := gcc
CFLAGS := -O2 -Wall
LDFLAGS_THREAD := -pthread

MAX_PROCESSES ?= 8
CFLAGS += -DMAX_PROCESSES=$(MAX_PROCESSES)

.PHONY: all clean run8 run64

all: numgen fork thread

numgen: numgen.c
	$(CC) $(CFLAGS) $< -o $@

fork: fork.c
	$(CC) $(CFLAGS) $< -o $@

thread: thread.c
	$(CC) $(CFLAGS) $(LDFLAGS_THREAD) $< -o $@

clean:
	rm -f numgen fork thread temp.txt *.o

# Helper targets
run8: clean
	$(MAKE) all MAX_PROCESSES=8
	./numgen
	./fork
	./thread

run64: clean
	$(MAKE) all MAX_PROCESSES=64
	./numgen
	./fork
	./thread

