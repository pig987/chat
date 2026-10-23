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

#define PORT "9034"   // Port we're listening on
#define MAXDATASIZE 100
#define MAX_EVENTS 64
#define MAX_CLIENTS 5

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

int main(void){
    int listner = get_listener_socket();
    if (listner == -1){
        fprintf(stderr, "listener 생성 실패\n");
        exit(1);
    }

    int epollfd, n, new_fd;
    struct epoll_event ev, events[MAX_EVENTS];

    struct sockaddr_storage their_addr;
    socklen_t addr_size;

    int numbytes;
    char buf[MAXDATASIZE];
    
    int client_fds[MAX_CLIENTS] = {0};
    int client_count = 0;
    
    epollfd = epoll_create1(0);
    if (epollfd == -1){
        perror("epoll_created1");
        exit(1);
    }

    ev.events = EPOLLIN;
    ev.data.fd = listner;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, listner, &ev);

    for(;;){
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
                    if (send(client_fds[j], msg, len, MSG_NOSIGNAL) == -1){
                        perror("send");
                    }
                }
                printf("User %d Entered the chat\n", new_fd);
                // set추가
                ev.events = EPOLLIN;
                ev.data.fd = new_fd;
                epoll_ctl(epollfd, EPOLL_CTL_ADD, new_fd, &ev);
                // 클라 배열에 추가
                client_fds[client_count] = new_fd;
                client_count++;
            }
            else{ // 리스너 아닌 나머지
                numbytes = recv(current, buf, MAXDATASIZE - 1, 0);
                if (numbytes == -1){
                    if (errno == EINTR) continue; // 시그널 끼어들어서 -1인 경우
                    perror("recv");
                    // 비정상 종료. set에서 지우기
                    epoll_ctl(epollfd, EPOLL_CTL_DEL, current, NULL);
                    close(current);
                    // 클라 배열에서 지우기
                    for (int j = 0; j < client_count; j++){
                        if (client_fds[j] == current){
                            client_fds[j] = client_fds[client_count - 1];
                            client_count--;
                            break;
                        }
                    }
                }
                else if (numbytes == 0){
                    // 감시대상에서 먼저 빼고 fd닫기
                    epoll_ctl(epollfd, EPOLL_CTL_DEL, current, NULL);
                    close(current); 
                    // 클라 배열에서 지우기
                    for (int j = 0; j < client_count; j++){
                        if (client_fds[j] == current){
                            client_fds[j] = client_fds[client_count - 1];
                            client_count--;
                            break;
                        }
                    }
                    // 나갔다고 broadcast
                    char msg[100];
                    int len = snprintf(msg, sizeof msg, "User %d AFK\n", current);
                    for (int j = 0; j < client_count; j++){
                        if (send(client_fds[j], msg, len, MSG_NOSIGNAL) == -1){
                            perror("send");
                        }
                    }
                    printf("User %d AFK\n", current);
                }
                else{ // numbytes > 0 일 때
                    buf[numbytes] = '\0';
                    char msg[150];
                    int len = snprintf(msg, sizeof msg, "User %d: %s", current, buf);
                    for (int j = 0; j < client_count; j++){
                        if(client_fds[j] == current) continue;
                        if (send(client_fds[j], msg, len, MSG_NOSIGNAL) == -1){
                            perror("send");
                        }
                    }
                }
            }
        }
    }
}