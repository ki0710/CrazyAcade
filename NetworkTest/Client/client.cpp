
#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>

#pragma comment(lib, "ws2_32.lib")

#define SERVERIP "127.0.0.1"
#define SERVERPORT 9000
#define BUFSIZE 4096

int main()
{
    int retval;

    // 1. 윈속 초기화
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return 1;

    // 2. TCP 소켓 생성
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock == INVALID_SOCKET)
    {
        WSACleanup();
        return 1;
    }

    // 3. 서버 주소 설정
    struct sockaddr_in serveraddr;
    memset(&serveraddr, 0, sizeof(serveraddr));

    serveraddr.sin_family = AF_INET;
    inet_pton(AF_INET, SERVERIP, &serveraddr.sin_addr);
    serveraddr.sin_port = htons(SERVERPORT);

    // 4. 서버 접속 요청
    printf("[CLIENT] 서버에 접속 중...\n");

    retval = connect(
        sock,
        (struct sockaddr*)&serveraddr,
        sizeof(serveraddr)
    );

    if (retval == SOCKET_ERROR)
    {
        printf("[CLIENT] 서버 접속 실패: %d\n",
            WSAGetLastError());

        closesocket(sock);
        WSACleanup();
        return 1;
    }

    // 5. 서버 접속 성공
    printf("[CLIENT] 서버 접속 성공!\n");
    printf("[CLIENT] 서버 IP: %s\n", SERVERIP);
    printf("[CLIENT] 서버 PORT: %d\n", SERVERPORT);

    // 6. 연결 유지
    printf("\n연결을 종료하려면 Enter 키를 누르세요.\n");

    getchar();

    // 7. 소켓 종료
    closesocket(sock);

    // 8. 윈속 종료
    WSACleanup();

    return 0;
}
