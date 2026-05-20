CC=gcc
CFLAGS=-g -Wall -Werror -Wextra

SERVER=dbs-server
CLIENT=dbs-client

all: $(SERVER) $(CLIENT)

$(SERVER): dbs-server.c
	$(CC) $(CFLAGS) -o $(SERVER) dbs-server.c

$(CLIENT): dbs-client.c
	$(CC) $(CFLAGS) -o $(CLIENT) dbs-client.c

clean:
	rm -f $(SERVER) $(CLIENT)

.PHONY: all clean
