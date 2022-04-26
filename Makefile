CC=gcc
CFLAGS=-std=gnu99 -Wall -Wextra -Werror -pedantic
LDFLAGS=

OBJS=
TARGET=proj2

proj2: main.c
	$(CC) $(CFLAGS) -o proj2 main.c -lrt -pthread

debug: main.c
	$(CC) $(CFLAGS) -o proj2 main.c -lrt -pthread -DPRINT_CONSOLE

clean:
	rm -f $(OBJS) $(TARGET)