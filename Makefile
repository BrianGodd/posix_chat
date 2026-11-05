CC = cc
CFLAGS = -O2 -std=c11 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L

all: chat_client chat_server

chat_client: chat_client.c
	$(CC) $(CFLAGS) -o $@ $<

chat_server: chat_server.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f chat_client chat_server

.PHONY: all clean
