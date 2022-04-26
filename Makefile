CC=gcc
CFLAGS=-std=gnu99 -Wall -Wextra -Werror -pedantic
LDFLAGS=

OBJS=
TARGET=proj2

.PHONY: pack clean debug

proj2: main.c
	$(CC) $(CFLAGS) -o proj2 main.c -lrt -pthread

pack: proj2.zip
	zip proj2.zip *.c Makefile

# Redirect output from file to stdout. Easier to debug.
debug: main.c
	$(CC) $(CFLAGS) -o proj2 main.c -lrt -pthread -DPRINT_CONSOLE

clean:
	rm -f $(OBJS) $(TARGET)