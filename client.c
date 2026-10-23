#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <sys/epoll.h>
#include <errno.h>
#include <unistd.h>

#define PORT "9034"
#define MAX_EVENTS 10
#define MAXDATASIZE 256

int main(int argc, char *argv[]){
    if (argc != 2){
        fprintf(stderr, "usage: client hostname\n");
        exit(EXIT_FAILURE);
    }

    struct addrinfo hints, *servinfo, *p;
    int rv, sockfd;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(argv[1], PORT, &hints, &servinfo)) != 0){
        fprintf(stderr, "%s", gai_strerror(rv));
        exit(EXIT_FAILURE);
    }

    for (p = servinfo; p != NULL; p = p->ai_next){
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1){
            perror("socket");
            continue;
        }
        if ((connect(sockfd, p->ai_addr, p->ai_addrlen)) == -1){
            perror("connect");
            continue;
        }
        break;
    }

    if (p == NULL){
        fprintf(stderr, "connect 실패\n");
        exit(EXIT_FAILURE);
    }

    int epollfd, n;
    struct epoll_event ev, events[MAX_EVENTS];
    char sock_buf[MAXDATASIZE];
    char stdin_buf[MAXDATASIZE];
    int numbytes;

    epollfd = epoll_create1(0);
    if(epollfd == -1){
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    ev.events = EPOLLIN;
    ev.data.fd = 0; // 키보드 (fd 0)
    epoll_ctl(epollfd, EPOLL_CTL_ADD, 0, &ev);

    ev.events = EPOLLIN;
    ev.data.fd = sockfd;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &ev);

    while(1){
        n = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if (n == -1){
            if (errno == EINTR) continue;
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }

        for(int i = 0; i < n; i++){
            if(events[i].data.fd == 0){ // 키보드 엔터
                int readlen = read(0, stdin_buf, sizeof(stdin_buf)); // 터미널 버퍼에서 가져오기
                if (readlen == 1 && stdin_buf[0] == '\n') continue;

                int bytes = send(sockfd, stdin_buf, readlen, 0);
                if (bytes == -1){
                    perror("send");
                }
                printf("-- %d bytes were sented --\n", bytes);
            }
            else {
                numbytes = recv(sockfd, sock_buf, MAXDATASIZE - 1, 0);
                if (numbytes == -1){
                    if (errno == EINTR) continue;
                    perror("recv");
                    exit(EXIT_FAILURE);
                }
                if (numbytes == 0){
                    printf("서버 연결 끊김\n");
                    exit(EXIT_FAILURE);
                }
                sock_buf[numbytes] = '\0';
                printf("%s\n", sock_buf);
            }
        }
    }
}