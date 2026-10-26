# epoll Chat Server

Linux epoll 기반 다중 클라이언트 채팅 서버.
여러 클라이언트가 서버에 접속하면 한 클라이언트의 메시지가 다른 모든 클라이언트에게 전달된다.

## 실행 환경
- Linux 전용
- macOS: OrbStack Linux 머신에서 실행 가능
- Windows: WSL2에서 실행 가능

## 실행 화면
(서버, 클라이언트 2개, 로그 이미지)
<img width="1597" height="972" alt="image" src="https://github.com/user-attachments/assets/ec0d715d-b457-494f-816b-9ddfcbeb9e26" />


## 기술 스택
C, TCP 소켓, epoll, Linux

## 빌드 및 실행 (Linux)

빌드
```bash
git clone https://github.com/pig987/chat.git
cd chat
gcc epoll.c -o epoll
gcc client.c -o client
```

서버 실행 (터미널 1)
```bash
./epoll
```

클라이언트 실행 (터미널 2, 3 ...)
```bash
cd chat
./client localhost
```

- 포트: 9034
- 로그 파일: 서버를 실행한 폴더에 `server.log`로 생성
