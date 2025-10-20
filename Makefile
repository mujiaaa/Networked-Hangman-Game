CC=gcc
CFLAGS=-Wall 

all: hangman_server hangman_client

hangman_server: hangman_server.c
	$(CC) $(CFLAGS) -o hangman_server hangman_server.c

hangman_client: hangman_client.c
	$(CC) $(CFLAGS) -o hangman_client hangman_client.c

clean:
	rm -f *.o hangman_server *~
	rm -f *.o hangman_client *~