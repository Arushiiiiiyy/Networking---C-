CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lssl -lcrypto

# Executables
TARGETS = client server

all: $(TARGETS)

client: client.c sham.h
	$(CC) $(CFLAGS) -o $@ client.c $(LDFLAGS)

server: server.c sham.h
	$(CC) $(CFLAGS) -o $@ server.c $(LDFLAGS)

clean:
	rm -f $(TARGETS) *.o *.txt *_log.txt
