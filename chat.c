#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>

#define PORT "9034"   // Port we're listening on
#define MAXDATASIZE 100

int get_listener_socket(void){
    int rv;
    struct addrinfo hints;
    struct addrinfo *servInfo, *p;
    int listener;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servInfo)) != 0){
        fprintf(stderr, "%s", gai_strerror(rv));
        exit(1);
    }

    for(p = servInfo; p != NULL; p = p->ai_next){
        // 소켓 만들기
        if ((listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
            perror("invalid socktype");
            continue;
        }

        // bind
        if (bind(listener, p->ai_addr, p->ai_addrlen) == -1){
            perror("bind");
            close(listener);
            continue;
        }

        break;
    }

    freeaddrinfo(servInfo);

    if (p == NULL){
        fprintf(stderr, "bind 실패\n");
        return -1;
    }

    if (listen(listener, 10) == -1){
        perror("listen");
        return -1;
    }

    return listener;
}

void add_to_pfds(struct pollfd **pfds, int *pfds_count, int *pfds_size, int new_fd){
    if (*pfds_count == *pfds_size){
        *pfds_size *= 2;
        *pfds = realloc(*pfds, sizeof(**pfds) * (*pfds_size));
    }

    (*pfds)[*pfds_count].fd = new_fd;
    (*pfds)[*pfds_count].events = POLLIN;
    (*pfds)[*pfds_count].revents = 0;
    (*pfds_count)++;
}

void remove_from_pfds(struct pollfd *pfds, int *pfds_count, int i){
    pfds[i] = pfds[*pfds_count - 1];
    (*pfds_count)--;
}

int main(void){
    int listener = get_listener_socket();
    if (listener == -1){
        fprintf(stderr, "listener 생성 실패\n");
        exit(1);
    }

    int pfds_size = 5;
    int pfds_count = 0;
    struct pollfd *pfds = malloc(sizeof *pfds * pfds_size);
    pfds[0].fd = listener;
    pfds[0].events = POLLIN;
    pfds_count++;
    int occured;
    
    struct sockaddr_storage their_addr;
    socklen_t addr_size;
    int new_fd;

    int numbytes;
    char buf[MAXDATASIZE];

    while(1){
        occured = poll(pfds, pfds_count, -1); // 무한 기다림
        if (occured == -1){
            perror("poll");
            continue;
        }
        if (pfds[0].revents & POLLIN){ // 듣기 소켓이 이벤트 받은거면
            addr_size = sizeof their_addr;
            if ((new_fd = accept(listener, (struct sockaddr *)&their_addr, &addr_size)) == -1){
                perror("accept");
                continue;
            }
            add_to_pfds(&pfds, &pfds_count, &pfds_size, new_fd);

            char msg[100];
            int len = snprintf(msg, sizeof msg, "User %d entered the chat\n", new_fd - 4);
            for(int x = 1; x < pfds_count; x++){
                if (send(pfds[x].fd, msg, len, 0) == -1){
                    perror("send");
                }
            }
        }   
        for(int i = 1; i < pfds_count; i++){
            if (pfds[i].revents & (POLLHUP | POLLIN)){
                numbytes = recv(pfds[i].fd, buf, MAXDATASIZE - 1, 0);
                if(numbytes == -1){
                    perror("recv");
                    close(pfds[i].fd);
                    remove_from_pfds(pfds, &pfds_count, i);
                    i--;
                }
                else if(numbytes == 0){
                    char msg[100];
                    int len = snprintf(msg, sizeof msg, "User %d AFK\n", pfds[i].fd - 4);
                    for(int x = 1; x < pfds_count; x++){
                        if (pfds[x].fd != pfds[i].fd){
                            if (send(pfds[x].fd, msg, len, 0) == -1){
                                perror("send");
                            }
                        }
                    }
                    close(pfds[i].fd);
                    remove_from_pfds(pfds, &pfds_count, i);
                    i--;
                }
                else { 
                    // send
                    for(int j = 0; j < pfds_count; j++){
                        if (pfds[j].fd != listener && pfds[j].fd != pfds[i].fd){
                            if (send(pfds[j].fd, buf, numbytes, 0) == -1){
                                perror("send");
                            }
                        }
                    }
                }
            }
        }
    }
}

