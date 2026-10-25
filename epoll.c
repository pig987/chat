#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <errno.h>
#include <signal.h>
#include "protocol.h"

#define PORT "9034"   // Port we're listening on
#define MAX_EVENTS 64
#define MAX_CLIENTS 5

FILE *logfp;
volatile int running = 1;

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
        fprintf(stderr, "%s\n", gai_strerror(rv));
        fprintf(logfp, "%s\n", gai_strerror(rv));
        fflush(logfp);
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
        fprintf(logfp, "bind 실패\n");
        fflush(logfp);
        return -1;
    }

    if (listen(listener, 10) == -1){
        perror("listen");
        return -1;
    }

    return listener;
}

int sendall(int fd, char *buf, int len){
    int total = 0;
    int bytesleft = len;
    int n;

    while(total < len){ // 다 보낼때까지
        n = send(fd, buf + total, bytesleft, MSG_NOSIGNAL);
        if (n == -1) break;
        total += n;
        bytesleft -= n;
    }

    return n == -1? -1 : 0;
}

void on_sig_int(int sig){
    running = 0;
}

int main(void){
    logfp = fopen("server.log", "a");
    if (logfp == NULL){
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    fprintf(logfp, "서버 시작\n");
    fflush(logfp);
    signal(SIGINT, on_sig_int);
    signal(SIGHUP, on_sig_int);

    int listner = get_listener_socket();
    if (listner == -1){
        fprintf(stderr, "listener 생성 실패\n");
        fprintf(logfp, "listener 생성 실패\n");
        fflush(logfp);
        exit(1);
    }

    int epollfd, n, new_fd;
    struct epoll_event ev, events[MAX_EVENTS];

    struct sockaddr_storage their_addr;
    socklen_t addr_size;

    int numbytes;

    struct client{
        int fd;
        uint32_t body_len;
        int header_received;
        int remaining;
        int received;
        char msg[MAX_BODY_LEN + 1];
    };

    //int client_fds[MAX_CLIENTS] = {0};
    struct client clients[MAX_CLIENTS];
    int client_count = 0;
    
    epollfd = epoll_create1(0);
    if (epollfd == -1){
        perror("epoll_created1");
        exit(1);
    }

    ev.events = EPOLLIN;
    ev.data.fd = listner;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, listner, &ev);

    while(running){
        n = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (n == -1){
            if (errno == EINTR) continue;
            perror("epoll_wait");
            exit(1);
        }
        for(int i = 0; i < n; i++){
            int current = events[i].data.fd;
            if (current == listner){
                addr_size = sizeof their_addr;
                new_fd = accept(listner, (struct sockaddr *)&their_addr, &addr_size);
                if (new_fd == -1){
                    perror("accept");
                    continue;
                }
                // 접속자 수 제한. 대기 큐에서 빼야하니 accept는 먼저 해야 함.
                if (client_count >= MAX_CLIENTS){
                    close(new_fd);
                    continue;
                }
                // 입장 broadcast
                char msg[100];
                int len = snprintf(msg, sizeof msg, "User %d entered the chat\n", new_fd);
                for(int j = 0; j < client_count; j++){
                    if (sendall(clients[j].fd, msg, len) == -1){
                        perror("sendall");
                    }
                }
                printf("User %d Entered the chat\n", new_fd);
                fprintf(logfp, "User %d Entered the chat\n", new_fd);
                fflush(logfp);
                // set추가
                ev.events = EPOLLIN;
                ev.data.fd = new_fd;
                epoll_ctl(epollfd, EPOLL_CTL_ADD, new_fd, &ev);
                // 클라 배열에 추가
                clients[client_count] = (struct client){ .fd = new_fd };
                client_count++;
            }
            else{
                int idx;
                for (int j = 0; j < client_count; j++){
                    if (clients[j].fd == current) { idx = j; break; }
                }
                struct client *c = &clients[idx];

                if (c->header_received != 4){ // 길이 받기
                    // 주소에 1바이트 단위로 더해야해서 (char *)로 포인터 타입 변환
                    numbytes = recv(current, (char *)&(c->body_len) + c->header_received, 4 - (c->header_received), 0);
                    if (numbytes == -1){
                        if (errno == EINTR) continue; // 시그널 끼어들어서 -1인 경우
                        perror("recv");
                        // 비정상 종료. set에서 지우기
                        epoll_ctl(epollfd, EPOLL_CTL_DEL, current, NULL);
                        close(current);
                        // 클라 배열에서 지우기
                        clients[idx] = clients[client_count - 1];
                        client_count--;
                    }
                    else if (numbytes == 0){
                        // 감시대상에서 먼저 빼고 fd닫기
                        epoll_ctl(epollfd, EPOLL_CTL_DEL, current, NULL);
                        close(current); 
                        // 클라 배열에서 지우기
                        clients[idx] = clients[client_count - 1];
                        client_count--;
                        // 나갔다고 broadcast
                        char msg[100];
                        int len = snprintf(msg, sizeof msg, "User %d AFK\n", current);
                        for (int j = 0; j < client_count; j++){
                            if (sendall(clients[j].fd, msg, len) == -1){
                                perror("sendall");
                            }
                        }
                        printf("User %d AFK\n", current);
                        fprintf(logfp, "User %d AFK\n", current);
                        fflush(logfp);
                    }
                    else{
                        c->header_received += numbytes;
                        if (c->header_received < 4) continue;

                        // 서버에 있는 배열 크기를 넘으면 연결끊기
                        if (c->body_len == 0 || c->body_len > MAX_BODY_LEN){
                            printf("'Buffer Overflow' occurred by User %d\n", current);
                            fprintf(logfp, "'Buffer Overflow' occurred by User %d\n", current);
                            fflush(logfp);
                            epoll_ctl(epollfd, EPOLL_CTL_DEL, current, NULL);
                            close(current);
                            // 클라 배열에서 지우기
                            clients[idx] = clients[client_count - 1];
                            client_count--;
                            continue;
                        }

                        c->remaining = c->body_len;
                        c->received = 0;
                    }
                }
                else{
                    numbytes = recv(current, c->msg + c->received, c->remaining, 0);
                    if (numbytes == -1){
                        if (errno == EINTR) continue;
                        perror("recv");
                        epoll_ctl(epollfd, EPOLL_CTL_DEL, current, NULL);
                        close(current);
                        clients[idx] = clients[client_count - 1];
                        client_count--;
                    }
                    else if (numbytes == 0){
                        epoll_ctl(epollfd, EPOLL_CTL_DEL, current, NULL);
                        close(current); 
                        clients[idx] = clients[client_count - 1];
                        client_count--;
                        // 나갔다고 broadcast
                        char msg[100];
                        int len = snprintf(msg, sizeof msg, "User %d AFK\n", current);
                        for (int j = 0; j < client_count; j++){
                            if (sendall(clients[j].fd, msg, len) == -1){
                                perror("sendall");
                            }
                        }
                        printf("User %d AFK\n", current);
                        fprintf(logfp, "User %d AFK\n", current);
                        fflush(logfp);
                    }
                    else{
                        c->remaining -= numbytes;
                        c->received += numbytes;
                        if (c->remaining == 0){ // 다 받은 상태
                            c->header_received = 0;
                            c->msg[c->body_len] = '\0';
                            char temp[MAX_BODY_LEN + PREFIX_MAX];
                            int len = snprintf(temp, sizeof temp, "User %d: %s", current, c->msg);
                            for (int j = 0; j < client_count; j++){
                                if(clients[j].fd == current) continue;
                                if (sendall(clients[j].fd, temp, len) == -1){
                                    perror("sendall");
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    fprintf(logfp, "서버 정상 종료\n\n");
    fflush(logfp);
}