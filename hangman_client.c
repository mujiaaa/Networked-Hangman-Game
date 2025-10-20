#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>

#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    if(argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    // ignore SIGPIPE to prevent termination on socket write errors.
    signal(SIGPIPE, SIG_IGN);
    
    int port = atoi(argv[2]);
    if(port <= 0) {
        fprintf(stderr, "Invalid port number.\n");
        exit(EXIT_FAILURE);
    }
    
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    
    if(inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    
    if(connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    
    // check for server-overloaded message
    fd_set read_fds;
    struct timeval tv;
    FD_ZERO(&read_fds);
    FD_SET(sockfd, &read_fds);
    tv.tv_sec = 0;
    tv.tv_usec = 500000; 
    
    int select_result = select(sockfd + 1, &read_fds, NULL, NULL, &tv);
    
    if(select_result > 0) { // data available to read
        unsigned char flag;
        int ret = recv(sockfd, &flag, 1, MSG_PEEK);
        if(ret > 0 && flag != 0) { // if the flag is non-zero, it's a message packet
            unsigned char msg_len;
            recv(sockfd, &msg_len, 1, 0);  // read the flag (message length)
            
            char msg[BUFFER_SIZE];
            int received = 0;
            while(received < msg_len) {
                int r = recv(sockfd, msg + received, msg_len - received, 0);
                if(r <= 0) break;
                received += r;
            }
            msg[received] = '\0';
            
            printf(">>>%s\n", msg);
            close(sockfd);
            exit(EXIT_SUCCESS);
        }
    }
    
    // game start
    char input[10];
    while(1) {
        printf(">>>Ready to start game? (y/n): ");
        if(fgets(input, sizeof(input), stdin) == NULL) {
            // on Ctrl-D, print a newline and exit.
            printf("\n");
            close(sockfd);
            exit(EXIT_SUCCESS);
        }
        input[strcspn(input, "\n")] = '\0';
        if(strcmp(input, "y") == 0) {
            break;
        } else if(strcmp(input, "n") == 0) {
            close(sockfd);
            exit(EXIT_SUCCESS);
        } else {
            printf(">>>Invalid input. Please enter 'y' or 'n'.\n");
        }
    }
    
    //empty message
    unsigned char start_signal = 0;
    if(send(sockfd, &start_signal, 1, 0) != 1) {
        perror("send");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    
    // game loop
    while(1) {
        unsigned char flag;
        int n = recv(sockfd, &flag, 1, 0);
        if(n <= 0) break; // connection closed or error
        
        if(flag != 0) {
            // message packet.
            int msg_len = flag;  // binary value equals message length
            char msg[msg_len + 1];
            int received = 0;
            while(received < msg_len) {
                int r = recv(sockfd, msg + received, msg_len - received, 0);
                if(r <= 0)
                    break;
                received += r;
            }
            msg[msg_len] = '\0';
            char *line = strtok(msg, "\n");
            while(line != NULL) {
                printf(">>>%s\n", line);
                line = strtok(NULL, "\n");
            }
            printf(">>>Game Over!\n");
            break;
        } else {
            // game control packet.
            unsigned char word_len, num_incorrect;
            if(recv(sockfd, &word_len, 1, 0) != 1)
                break;
            if(recv(sockfd, &num_incorrect, 1, 0) != 1)
                break;
            
            char game_state[word_len + 1];
            int received = 0;
            while(received < word_len) {
                int r = recv(sockfd, game_state + received, word_len - received, 0);
                if(r <= 0)
                    break;
                received += r;
            }
            game_state[word_len] = '\0';
            
            char incorrect[num_incorrect + 1];
            received = 0;
            while(received < num_incorrect) {
                int r = recv(sockfd, incorrect + received, num_incorrect - received, 0);
                if(r <= 0)
                    break;
                received += r;
            }
            incorrect[num_incorrect] = '\0';
            
            printf(">>>");
            for (int i = 0; i < word_len; i++) {
                printf("%c", game_state[i]);
                if(i < word_len - 1)
                    printf(" ");
            }
            printf("\n");
            printf(">>>Incorrect Guesses: ");
            if(num_incorrect > 0) {
                for (int i = 0; i < num_incorrect; i++){
                    printf("%c", incorrect[i]);
                    if(i < num_incorrect - 1)
                        printf(" ");
                }
            }
            printf("\n");
            printf(">>>\n");
            
            // prompt for letter.
            char guess_input[10];
            while(1) {
                printf(">>>Letter to guess: ");
                if(fgets(guess_input, sizeof(guess_input), stdin) == NULL) {
                    printf("\n");
                    close(sockfd);
                    exit(EXIT_SUCCESS);
                }
                guess_input[strcspn(guess_input, "\n")] = '\0';
                if(strlen(guess_input) != 1 || !isalpha(guess_input[0])) {
                    printf(">>>Error! Please guess one letter.\n");
                } else {
                    break;
                }
            }
            guess_input[0] = tolower(guess_input[0]);
            unsigned char guess_packet[2];
            guess_packet[0] = 1;
            guess_packet[1] = guess_input[0];
            if(send(sockfd, guess_packet, 2, 0) != 2) {
                perror("send");
                break;
            }
        }
    }
    
    close(sockfd);
    return 0;
}