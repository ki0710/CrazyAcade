
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>

#pragma comment(lib, "ws2_32.lib")

#define SERVERPORT 9000
#define BUFSIZE 4096
#define MAX_PLAYERS 2

// 콘솔 출력 동기화를 위한 임계 영역
CRITICAL_SECTION cs;

// 클라이언트 정보를 저장하는 구조체
struct ClientInfo
{
    SOCKET sock;
    int number;
};

// 콘솔 메시지 출력 함수
void PrintMessage(int number, const char* message)
{
    EnterCriticalSection(&cs);

    printf("[Player %d] %s\n", number, message);
    fflush(stdout);

    LeaveCriticalSection(&cs);
}

// 클라이언트별 데이터 수신 스레드
DWORD WINAPI ProcessClient(LPVOID arg)
{
    ClientInfo* info = (ClientInfo*)arg;

    SOCKET client_sock = info->sock;
    int number = info->number;

    // 전달받은 정보를 복사했으므로 메모리 해제
    delete info;

    // 클라이언트 IP 주소 확인
    struct sockaddr_in clientaddr;
    int addrlen = sizeof(clientaddr);
    char ip[INET_ADDRSTRLEN] = "";

    if (getpeername(
        client_sock,
        (struct sockaddr*)&clientaddr,
        &addrlen
    ) == 0)
    {
        inet_ntop(
            AF_INET,
            &clientaddr.sin_addr,
            ip,
            sizeof(ip)
        );
    }

    EnterCriticalSection(&cs);
    printf("[Player %d] 접속 성공! IP: %s\n",
        number, ip);
    LeaveCriticalSection(&cs);

    // 클라이언트와 연결 유지
    char buf[BUFSIZE];

    while (1)
    {
        int retval = recv(
            client_sock,
            buf,
            BUFSIZE,
            0
        );

        if (retval == 0)
        {
            // 클라이언트가 정상적으로 연결 종료
            PrintMessage(number, "접속 종료");
            break;
        }

        if (retval == SOCKET_ERROR)
        {
            // 데이터 수신 오류
            PrintMessage(number, "수신 오류 발생");
            break;
        }

        // 수신한 데이터 확인
        EnterCriticalSection(&cs);
        printf("[Player %d] %d바이트 수신\n",
            number, retval);
        LeaveCriticalSection(&cs);

        // 실제 게임 패킷 처리는 추후 구현
    }

    // 클라이언트 소켓 종료
    closesocket(client_sock);

    return 0;
}

int main()
{
    int retval;

    // 1. 윈속 초기화
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return 1;

    // 2. TCP 서버 소켓 생성
    SOCKET listen_sock = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if (listen_sock == INVALID_SOCKET)
    {
        WSACleanup();
        return 1;
    }

    // 3. 서버 주소 설정
    struct sockaddr_in serveraddr;
    memset(&serveraddr, 0, sizeof(serveraddr));

    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(SERVERPORT);

    // 4. IP 주소와 포트 번호 연결
    retval = bind(
        listen_sock,
        (struct sockaddr*)&serveraddr,
        sizeof(serveraddr)
    );

    if (retval == SOCKET_ERROR)
    {
        printf("bind() 실패: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return 1;
    }

    // 5. 클라이언트 접속 대기
    retval = listen(listen_sock, SOMAXCONN);

    if (retval == SOCKET_ERROR)
    {
        printf("listen() 실패: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return 1;
    }

    // 6. 임계 영역 초기화
    InitializeCriticalSection(&cs);

    printf("================================\n");
    printf("       Network Game Server      \n");
    printf("================================\n");
    printf("서버 포트: %d\n", SERVERPORT);
    printf("최대 플레이어: %d명\n", MAX_PLAYERS);
    printf("클라이언트 접속 대기 중...\n\n");

    // 클라이언트별 스레드 핸들
    HANDLE hThread[MAX_PLAYERS] = { NULL };

    int count = 0;

    // 7. 클라이언트 2명 접속 처리
    while (count < MAX_PLAYERS)
    {
        struct sockaddr_in clientaddr;
        int addrlen = sizeof(clientaddr);

        SOCKET client_sock = accept(
            listen_sock,
            (struct sockaddr*)&clientaddr,
            &addrlen
        );

        if (client_sock == INVALID_SOCKET)
        {
            printf("accept() 실패\n");
            break;
        }

        // 플레이어 정보 생성
        ClientInfo* info = new ClientInfo;

        info->sock = client_sock;
        info->number = count + 1;

        // 8. 클라이언트별 스레드 생성
        HANDLE thread = CreateThread(
            NULL,
            0,
            ProcessClient,
            info,
            0,
            NULL
        );

        if (thread == NULL)
        {
            printf("스레드 생성 실패\n");
            closesocket(client_sock);
            delete info;
            break;
        }

        hThread[count] = thread;
        count++;
    }

    if (count == MAX_PLAYERS)
    {
        printf("\n플레이어 2명 접속 완료!\n");
    }

    // 9. 모든 클라이언트 스레드 종료 대기
    if (count > 0)
    {
        WaitForMultipleObjects(
            count,
            hThread,
            TRUE,
            INFINITE
        );
    }

    // 10. 스레드 핸들 정리
    for (int i = 0; i < count; i++)
    {
        CloseHandle(hThread[i]);
    }

    // 11. 서버 소켓 종료
    closesocket(listen_sock);

    // 12. 임계 영역 제거
    DeleteCriticalSection(&cs);

    // 13. 윈속 종료
    WSACleanup();

    printf("\n모든 클라이언트 연결 종료\n");
    system("pause");

    return 0;
}
