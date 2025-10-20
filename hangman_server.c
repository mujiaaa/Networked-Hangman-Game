#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>

#define MAX_CONN 3
#define BUFFER_SIZE 1024
#define WORD_SIZE 10   // max word length

volatile sig_atomic_t active_clients = 0;

void sigchld_handler(int signo) {
    // reap all finished child processes.
    while(waitpid(-1, NULL, WNOHANG) > 0) {
        active_clients--;
    }
}

// handle individual client connection
void handle_client(int client_sock) {
    // ignore SIGPIPE to avoid termination on broken socket writes
    signal(SIGPIPE, SIG_IGN);
    
    // read candidate words from "hangman_words.txt"
    FILE *fp = fopen("hangman_words.txt", "r");
    if(fp == NULL) {
        perror("fopen");
        close(client_sock);
        exit(EXIT_FAILURE);
    }
    
    char words[15][WORD_SIZE];
    int word_count = 0;
    while(fgets(words[word_count], WORD_SIZE, fp) != NULL && word_count < 15) {
        words[word_count][strcspn(words[word_count], "\n")] = '\0';
        int len = strlen(words[word_count]);
        if(len < 3 || len > 8) continue;
        word_count++;
    }
    fclose(fp);
    
    if(word_count == 0) {
        char msg[] = "No valid words.";
        unsigned char header = (unsigned char)strlen(msg);
        char packet[BUFFER_SIZE];
        packet[0] = header;
        memcpy(packet+1, msg, header);
        send(client_sock, packet, 1 + header, 0);
        close(client_sock);
        exit(EXIT_FAILURE);
    }
    
    // choose a word at random.
    srand(time(NULL) ^ getpid());
    int index = rand() % word_count;
    char selected[WORD_SIZE];
    strncpy(selected, words[index], WORD_SIZE);
    int word_len = strlen(selected);
    
    // set up: initially all underscores
    char game_state[word_len];
    for (int i = 0; i < word_len; i++) {
        game_state[i] = '_';
    }
    
    // buffers for tracking guesses
    char incorrect[10] = {0}; // store incorrect guesses (max 6)
    int num_incorrect = 0;
    char guessed[27] = {0};   // store all guessed letters
    int num_guessed = 0;
    
    // wait for the client’s start signal.
    unsigned char start_signal;
    int r = recv(client_sock, &start_signal, 1, 0);
    if(r <= 0 || start_signal != 0) {
        close(client_sock);
        exit(EXIT_FAILURE);
    }
    
    // build and send the initial game control packet.
    char packet[BUFFER_SIZE];
    memset(packet, 0, sizeof(packet));
    packet[0] = 0;  // Byte 0: game control packet flag
    packet[1] = (unsigned char) word_len; // Byte 1: word length 
    packet[2] = (unsigned char) num_incorrect; // Byte 2: number of incorrect guesses
    memcpy(packet + 3, game_state, word_len); // Bytes 3...: game state then incorrect guesses
    int packet_len = 3 + word_len;  // no incorrect guesses yet
    send(client_sock, packet, packet_len, 0);
    
    // game loop
    while(1) {
        // expect a guess packet from the client (2 bytes)
        unsigned char guess_packet[2];
        int n = recv(client_sock, guess_packet, 2, 0);
        if(n <= 0) break;
        if(n < 2) continue;
        
        char guess_letter = tolower(guess_packet[1]);
        int already_guessed = 0;
        for(int i = 0; i < num_guessed; i++) {
            if(guessed[i] == guess_letter) {
                already_guessed = 1;
                break;
            }
        }
        if(!already_guessed) {
            // add the guessed letter to the guessed array
            guessed[num_guessed++] = guess_letter;
            int found = 0;
            for(int i = 0; i < word_len; i++) {
                if(selected[i] == guess_letter) {
                    game_state[i] = guess_letter;
                    found = 1;
                }
            }
            if(!found) {
                if(num_incorrect < 6) {
                    incorrect[num_incorrect++] = guess_letter;
                }
            }
        }
        
        // check if all letters revealed
        int win = 1;
        for (int i = 0; i < word_len; i++) {
            if(game_state[i] != selected[i]) {
                win = 0;
                break;
            }
        }
        if(win) {
            char win_msg[BUFFER_SIZE];
            snprintf(win_msg, sizeof(win_msg), "The word was %s\nYou Win!", selected);
            unsigned char header = (unsigned char) strlen(win_msg);
            char msg_packet[BUFFER_SIZE];
            msg_packet[0] = header;
            memcpy(msg_packet+1, win_msg, header);
            send(client_sock, msg_packet, 1 + header, 0);
            break;
        }
        if(num_incorrect >= 6) { // lose
            char lose_msg[BUFFER_SIZE];
            snprintf(lose_msg, sizeof(lose_msg), "The word was %s\nYou Lose!", selected);
            unsigned char header = (unsigned char) strlen(lose_msg);
            char msg_packet[BUFFER_SIZE];
            msg_packet[0] = header;
            memcpy(msg_packet+1, lose_msg, header);
            send(client_sock, msg_packet, 1 + header, 0);
            break;
        }
        
        // build the updated game control packet
        memset(packet, 0, sizeof(packet));
        packet[0] = 0;
        packet[1] = (unsigned char) word_len;
        packet[2] = (unsigned char) num_incorrect;
        memcpy(packet + 3, game_state, word_len);
        memcpy(packet + 3 + word_len, incorrect, num_incorrect);
        packet_len = 3 + word_len + num_incorrect;
        send(client_sock, packet, packet_len, 0);
    }
    
    close(client_sock);
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    if(argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    // Ignore SIGPIPE in the main server process.
    signal(SIGPIPE, SIG_IGN);
    
    int port = atoi(argv[1]);
    if(port <= 0) {
        fprintf(stderr, "Invalid port number.\n");
        exit(EXIT_FAILURE);
    }
    
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(server_sock < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    int opt = 1;
    if(setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_sock);
        exit(EXIT_FAILURE);
    }
    
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);
    
    if(bind(server_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        close(server_sock);
        exit(EXIT_FAILURE);
    }
    
    if(listen(server_sock, 5) < 0) {
        perror("listen");
        close(server_sock);
        exit(EXIT_FAILURE);
    }
    
    // set up SIGCHLD handler to reap finished children
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if(sigaction(SIGCHLD, &sa, NULL) < 0) {
        perror("sigaction");
        close(server_sock);
        exit(EXIT_FAILURE);
    }
    
    printf("Server listening on port %d...\n", port);
    
    while(1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);
        if(client_sock < 0) {
            perror("accept");
            continue;
        }
        
        // enforce maximum number of concurrent clients
        if(active_clients >= MAX_CONN) {
            char overload_str[] = "server-overloaded";
            unsigned char header = (unsigned char) strlen(overload_str);
            char overload_packet[BUFFER_SIZE];
            overload_packet[0] = header;
            memcpy(overload_packet + 1, overload_str, header);

            // send the entire message, handling partial writes
            int bytes_to_send = 1 + header;
            int total_sent = 0;
            while (total_sent < bytes_to_send) {
                int sent = send(client_sock, overload_packet + total_sent, bytes_to_send - total_sent, 0);
                if (sent < 0) {
                    perror("send");
                    break;
                }
                total_sent += sent;
            }
            close(client_sock);
            continue;
        }
        
        active_clients++;
        pid_t pid = fork();
        if(pid < 0) {
            perror("fork");
            close(client_sock);
            continue;
        } else if(pid == 0) {
            // child process: handle this client
            close(server_sock);
            handle_client(client_sock);
            exit(EXIT_SUCCESS);
        } else {
            // parent process: close the connected socket and continue
            close(client_sock);
        }
    }
    
    close(server_sock);
    return 0;
}
