CC      := gcc
CFLAGS  := -O2 -Wall -Wextra -std=c11
LDFLAGS := -lm

BIN     := cpu_scheduler
SRC     := cpu_scheduler.c

FILE ?= input.1
ALG  ?= FCFS
TQ   ?= 4

.PHONY: all build run clean

all: $(BIN)
build: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

run: $(BIN)
	@if [ "$(ALG)" = "RR" ]; then \
		./$(BIN) "$(FILE)" "$(ALG)" "$(TQ)"; \
	else \
		./$(BIN) "$(FILE)" "$(ALG)"; \
	fi

clean:
	@rm -f $(BIN)
