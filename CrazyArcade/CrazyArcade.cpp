#include"pch.h"
#include"Train.h"
#include"Bomb.h"
#include"ResourceManager.h"
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#include <shlwapi.h>  // PathFileExists를 사용하기 위해 추가

#pragma comment(lib, "shlwapi.lib")  // shlwapi 라이브러리 링크
HINSTANCE g_hInst;
LPCTSTR lpszClass = L"Window Class Name";
LPCTSTR lpszWindowName = L"CrazyArcade";
void DrawOutlinedText(HDC hdc, int x, int y, const wchar_t* text) {
    // 폰트 생성 (크고 두꺼운 글씨)
    HFONT hFont = CreateFont(
        48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"맑은 고딕"
    );

    HFONT oldFont = (HFONT)SelectObject(hdc, hFont);

    // 배경 투명하게 설정
    SetBkMode(hdc, TRANSPARENT);

    // 외곽선(Stroke)을 위한 경로 시작
    BeginPath(hdc);
    TextOut(hdc, x, y, text, wcslen(text));
    EndPath(hdc);

    // 외곽선 색
    HPEN hPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
    HPEN oldPen = (HPEN)SelectObject(hdc, hPen);

    // 안쪽 채우기 색
    HBRUSH hBrush = CreateSolidBrush(RGB(255, 165, 0));
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, hBrush);

    StrokeAndFillPath(hdc);

    // 정리
    SelectObject(hdc, oldBrush);
    DeleteObject(hBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(hPen);
    SelectObject(hdc, oldFont);
    DeleteObject(hFont);
}

static int frameCount = 0;
static DWORD lastTime = GetTickCount();  // 시작 시간(ms)
static int currentFPS = 0;
LRESULT CALLBACK WndProc(HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lParam);
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpszCmdParam, _In_ int nCmdShow) {
    HWND hWnd;
    MSG Message;
    WNDCLASSEX WndClass;
  

   

    g_hInst = hInstance;
    ResourceManager::LoadAll(g_hInst);
  
    WndClass.cbSize = sizeof(WndClass);
    WndClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    WndClass.lpfnWndProc = (WNDPROC)WndProc;
    WndClass.cbClsExtra = 0;
    WndClass.cbWndExtra = 0;
    WndClass.hInstance = hInstance;
    WndClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    WndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    WndClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    WndClass.lpszMenuName = NULL;
    WndClass.lpszClassName = lpszClass;
    WndClass.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    //위의 12개의 항목을 다 만들어야 윈도우가 생길거임.
    RegisterClassEx(&WndClass);//만든 윈도우를 레지스터에 등록?하는거라함

    hWnd = CreateWindow(lpszClass, lpszWindowName, WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL | CS_DBLCLKS,
        0, 0, 1500, 900, NULL, (HMENU)NULL, hInstance, NULL);
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    
   
    while (GetMessage(&Message, 0, 0, 0)) {
        TranslateMessage(&Message);
        DispatchMessage(&Message);
    }
    ResourceManager::UnloadAll();
    return Message.wParam;
  

}





LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    
    
    PAINTSTRUCT ps;
    HDC hdc;
    static Player p1, p2;
    static Board board[MAP_TILES][MAP_TILES];
    static int selected_stage = 0;
    static std::vector<Bomb> p1bombs;
    static std::vector<Bomb> p2bombs;
	static bool game_started = false;
    static bool game_end = false;
    static int game_winner = 0; 
    static int p1_check_color_pos;
	static int p2_check_color_pos;
	static int p1_hit_cnt = 0;
	static int p2_hit_cnt = 0;
    //스테이지 1
    static int p1Applebox = 0;
    static int p2Applebox = 0;

    //스테이지 2
	static Train train;
    static int p1Time = 0;
	static int p2Time = 0;
	static int p1currentTime = 100;
	static int p2currentTime = 100;
    //스테이지 3
	static int p1CurrentHeart = 0;
	static int p2CurrentHeart = 0;

    //현재 아이템 개수 파악
	static int p1_current_item[3] = { 0,0,0 }; 
	static int p2_current_item[3] = { 0,0,0 }; 

    switch (msg) {
    case WM_CREATE:
        p1_check_color_pos = 270;
        p2_check_color_pos = 270;
        p1.SetColor(0);
		p2.SetColor(0);
        //로그인 사운드 재생
        PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Login.wav"), NULL, SND_LOOP | SND_ASYNC);
        
        break;
    case WM_KEYDOWN:
        if (wParam < 256) keyStates[wParam] = true;
        if (game_started && selected_stage) {
            if (wParam == VK_SPACE) {
                if (selected_stage == 1) {
                    if (p1.GetMaxBomb() - p1.GetCurrentApple() > p1bombs.size()) {
                        Bomb newBomb;
                        newBomb.make_Bomb(p1, board);

                        p1bombs.push_back(newBomb);
                        PlaySound(NULL, NULL, NULL);
                        PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Ballon.wav"), NULL, SND_ASYNC);
                    }
                }
                else if (selected_stage == 2 || selected_stage == 3) {
                    if (p1.GetMaxBomb() > p1bombs.size()) {
                        Bomb newBomb;
                        newBomb.make_Bomb(p1, board);

                        p1bombs.push_back(newBomb);
                        PlaySound(NULL, NULL, NULL);
                        PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Ballon.wav"), NULL, SND_ASYNC);
                    }
                }
            }
            if (wParam == VK_RETURN) {

                if (selected_stage == 1) {
                    if (p2.GetMaxBomb() - p2.GetCurrentApple() > p2bombs.size()) {
                        Bomb newBomb;
                        newBomb.make_Bomb(p2, board);

                        p2bombs.push_back(newBomb);
                        PlaySound(NULL, NULL, NULL);
                        PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Ballon.wav"), NULL, SND_ASYNC);
                    }
                }
				else if (selected_stage == 2 || selected_stage == 3) {
					if (p2.GetMaxBomb() > p2bombs.size()) {
						Bomb newBomb;
						newBomb.make_Bomb(p2, board);
						p2bombs.push_back(newBomb);
                        PlaySound(NULL, NULL, NULL);
                        PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Ballon.wav"), NULL, SND_ASYNC);
					}
				}
            }
        }
        else if (game_started) {
            if (wParam == VK_LEFT) {
                if (p2_check_color_pos > 270) {
					p2_check_color_pos -= 155;
                    p2.SetColor(p2.GetColor() - 1);
                }
            }
            else if (wParam == VK_RIGHT) {
                if (p2_check_color_pos < 1200) {
                    p2_check_color_pos += 155;
                    p2.SetColor(p2.GetColor() + 1);
                }
            }
        }
        if (selected_stage == 1) {
            p1.on_applebox(&p1, &p1Applebox, 8, 19);
            p2.on_applebox(&p2, &p2Applebox, 11, 19);
			if (p1Applebox >= 10) {
				//p1승리 이제 win메시지 띄우고 전부 초기화
                game_end = true;
                game_winner = 1;
			}
            else if (p2Applebox >= 10) {
                //p2승리 이제 win메시지 띄우고 전부 초기화
                game_end = true;
				game_winner = 2;
            }
        }
	
        InvalidateRect(hWnd, NULL, FALSE);
        break;
    case WM_KEYUP:
        if (wParam < 256) keyStates[wParam] = false;
        break;
    case WM_TIMER: {
        switch (wParam) {
        case 1: {
            
            if (selected_stage) {
                HandlePlayerMovement(p1, keyStates, 'W', 'S', 'A', 'D', board);
                UpdatePlayerPosition(p1, p1.Getspeed()/8.0f);

                HandlePlayerMovement(p2, keyStates, VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, board);
                UpdatePlayerPosition(p2, p2.Getspeed()/8.0f);

                int temp = -1;
                temp=p1.collision_item(&p1, board, p1currentTime)-1;
				if (temp >= 0 && temp < 3) { // 아이템이 유효한 범위 내에 있는지 확인
					p1_current_item[temp]++; // 아이템 개수 증가
                    PlaySound(NULL, NULL, NULL);
                    PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Acting.wav"), NULL, SND_ASYNC);
				}
				
               temp= p2.collision_item(&p2, board, p2currentTime)-1;
			
               if (temp >= 0 && temp < 3) { // 아이템이 유효한 범위 내에 있는지 확인
				   p2_current_item[temp]++; // 아이템 개수 증가
                   PlaySound(NULL, NULL, NULL);
                   PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Acting.wav"), NULL, SND_ASYNC);
               }
            }
            frameCount++;

            DWORD currentTime = GetTickCount();
            if (currentTime - lastTime >= 1000) { // 1초 지남
                currentFPS = frameCount;
                frameCount = 0;
                lastTime = currentTime;
            }
            if (selected_stage == 2) {
                if (move_train(train)) {
                    make_item(train, board);

                }
                move_item(train, board);
				if (train.is_spawned) {
                    if (!train.collisionP1) {
                        if (p1.collision_train(&p1, train, board, p1currentTime)) {
                            train.collisionP1 = true; // 플레이어 1과 충돌한 경우
                            PlaySound(NULL, NULL, NULL);
                            PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Die.wav"), NULL, SND_ASYNC);
                        }
                    }
				}
				if (train.is_spawned) {
                    if (!train.collisionP2) {
                        if (p2.collision_train(&p2, train, board, p2currentTime)) {
                            train.collisionP2 = true; // 플레이어 2와 충돌한 경우
                            PlaySound(NULL, NULL, NULL);
                            PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Die.wav"), NULL, SND_ASYNC);
                        }
                    }
				}
			
			
				
                p1Time++;
                p2Time++;
                if (p1Time >= 50) {
                    p1Time = 0;
                    p1currentTime--;
                }
                if (p2Time >= 50) {
                    p2Time = 0;
                    p2currentTime--;
                }

				if (p1currentTime <= 0) {
					game_end = true;
					game_winner = 2; // 플레이어 2 승리
					KillTimer(hWnd, 1); // 타이머 중지
					KillTimer(hWnd, 2); // 타이머 중지
                    PlaySound(NULL, NULL, NULL);
                    PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Win.wav"), NULL, SND_ASYNC);
				}
				else if (p2currentTime <= 0) {
					game_end = true;
					game_winner = 1; // 플레이어 1 승리
                    KillTimer(hWnd, 1); // 타이머 중지
                    KillTimer(hWnd, 2); // 타이머 중지
                    PlaySound(NULL, NULL, NULL);
                    PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Win.wav"), NULL, SND_ASYNC);
				}
            }
           
            
        }
            break;
        case 2:
           
        {//충돌 시 깜빡이는거 구현하려고 필요한거
            if (p1.GetIsHit()) {
                p1_hit_cnt++;
                if (p1_hit_cnt >= 10) {
                    p1.SetIsHit(false);
                    p1_hit_cnt = 0;
                }
            }
            if (p2.GetIsHit()) {
                p2_hit_cnt++;
                if (p2_hit_cnt >= 10) {
                    p2.SetIsHit(false);
                    p2_hit_cnt = 0;
                }

            }
        }
            //플레이어 폭탄 충돌
            for (int i = 0;i < p1bombs.size();++i) {
				if (!p1bombs.data()[i].GetIsCollisionPlayer()&&p1bombs.data()[i].GetState() == 2) {
                    if (p1bombs.data()[i].collision_bomb_player(p1bombs.data()[i], p2)) {
                        if (p2.GetIsHit() == false) {
                            PlaySound(NULL, NULL, NULL);
                            PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Die.wav"), NULL, SND_ASYNC);
                        }
						p2.SetIsHit(true); // 플레이어 2가 폭탄에 맞음
                       
                        if (selected_stage == 1) {
							//스테이지 1 충돌 처리
                            if (p2.GetCurrentApple() > 0) {
                                p1.SetCurrentApple(p1.GetCurrentApple() + p2.GetCurrentApple());
                                p2.SetCurrentApple(0);
                            }
                            p1bombs.data()[i].SetIsCollisionPlayer(true); // 플레이어 2와 충돌한 경우
                        }
                        else if (selected_stage == 2) {
                            //스테이지 2 충돌 처리
							p2currentTime-=2;

							if (p2currentTime <= 0) {
								game_end = true;
								game_winner = 1; // 플레이어 1 승리
                                PlaySound(NULL, NULL, NULL);
                                PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Win.wav"), NULL, SND_ASYNC);
							}
                            p1bombs.data()[i].SetIsCollisionPlayer(true);
                        }
                        else if (selected_stage == 3) {
                            //스테이지 3 충돌 처리
                            p2CurrentHeart--;
                            p1bombs.data()[i].SetIsCollisionPlayer(true); // 플레이어 2와 충돌한 경우
							if (p2CurrentHeart <= 0) {
								game_end = true;
								game_winner = 1; // 플레이어 1 승리
                                PlaySound(NULL, NULL, NULL);
                                PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Win.wav"), NULL, SND_ASYNC);
							}
                        }
                    }
				}
            }
          
            
                for (int i = 0;i < p2bombs.size();++i) {
                    if (!p2bombs.data()[i].GetIsCollisionPlayer() && p2bombs.data()[i].GetState() == 2) {
						
                        if (p2bombs.data()[i].collision_bomb_player(p2bombs.data()[i], p1)) {
                            if (p1.GetIsHit() == false) {
                                PlaySound(NULL, NULL, NULL);
                                PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Die.wav"), NULL, SND_ASYNC);
                            }
                            p1.SetIsHit(true); // 플레이어 1이 폭탄에 맞음
                            
                            //스테이지 별 충돌 처리
                            if (selected_stage == 1) {
                                //스테이지 1 충돌 처리
                                if (p1.GetCurrentApple() > 0) {
                                    p2.SetCurrentApple(p2.GetCurrentApple() + p1.GetCurrentApple());
                                    p1.SetCurrentApple(0);
                                }
                                p2bombs.data()[i].SetIsCollisionPlayer(true); // 플레이어 1과 충돌한 경우
                            }
							else if (selected_stage == 2) {
								//스테이지 2 충돌 처리
								p1currentTime-=2;
								if (p1currentTime <= 0) {
									game_end = true;
									game_winner = 2; // 플레이어 2 승리
                                    PlaySound(NULL, NULL, NULL);
                                    PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Win.wav"), NULL, SND_ASYNC);
								}
                                p2bombs.data()[i].SetIsCollisionPlayer(true);
							}
							else if (selected_stage == 3) {
								//스테이지 3 충돌 처리
                                p1CurrentHeart--;
                                p2bombs.data()[i].SetIsCollisionPlayer(true); // 플레이어 1과 충돌한 경우
								if (p1CurrentHeart <= 0) {
									game_end = true;
									game_winner = 2; // 플레이어 2 승리
                                    PlaySound(NULL, NULL, NULL);
                                    PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Win.wav"), NULL, SND_ASYNC);
								}
							}
                        }
                    }
                }
            
            
            for (int i = 0;i < p1bombs.size();++i) {
                if (p1bombs.data()[i].GetState() == 2) {
                    board[p1bombs.data()[i].GetLogicalX()][p1bombs.data()[i].GetLogicalY()].obstacle = 0; // 폭탄이 터지면 해당 위치의 장애물 제거

                }
                if (p1bombs.data()[i].tick_Bomb()) {
                     
                     p1bombs.erase(p1bombs.begin() + i);
                 }
            }
            for (int i = 0;i < p2bombs.size();++i) {
                if (p2bombs.data()[i].GetState() == 2) {
                    board[p2bombs.data()[i].GetLogicalX()][p2bombs.data()[i].GetLogicalY()].obstacle = 0; // 폭탄이 터지면 해당 위치의 장애물 제거

                     }
				if (p2bombs.data()[i].tick_Bomb()) {
                   
                    p2bombs.erase(p2bombs.begin() + i);
                }
            }
          
            break;
            }
   
        InvalidateRect(hWnd, NULL, FALSE);
        break;
    }
    case WM_LBUTTONDOWN: {
		int pos_x = LOWORD(lParam);
        if (!game_started && pos_x < 800) {
            PlaySound(NULL, NULL, NULL);
			PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Menu.wav"), NULL, SND_ASYNC | SND_LOOP);
            game_started = true;
        }
        else if (!game_started &&pos_x > 800) {
            PostQuitMessage(0);
        }
        InvalidateRect(hWnd, NULL, FALSE);
        break;
    }
    case WM_CHAR:
       
        if (selected_stage == 0) {
            if (wParam == '1') {
                selected_stage = 1;
                PlaySound(NULL, NULL, NULL);
                PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\GameStart.wav"), NULL, SND_ASYNC );
                
                POINT p1_start_point = { 8,18 };
                POINT p2_start_point = { 11,18 };
                int hueFilterMin = 0;   // 원하는 색 필터 범위
                int hueFilterMax = 360;
             
                HBITMAP original = ResourceManager::GetBitmap(BitmapID::Player1Moving);
                
                HBITMAP recolored = AdjustBitmapHueWithFilter(original, p1.GetColor(), hueFilterMin, 20);
                ResourceManager::SetBitmap(BitmapID::Player1Moving, recolored);

				original = ResourceManager::GetBitmap(BitmapID::Player2Moving);
				recolored = AdjustBitmapHueWithFilter(original, p2.GetColor(), hueFilterMin, 20);
				ResourceManager::SetBitmap(BitmapID::Player2Moving, recolored);

                p1.Init_Player(p1_start_point);
                p2.Init_Player(p2_start_point);
                p2.SetPlayerNum(2);
                init_board_Forest(board);
               
                SetTimer(hWnd, TIMER_ID, TIMER_INTERVAL, NULL);
                SetTimer(hWnd, 2, 50, NULL);
              
              
            }
            else  if (wParam == '2') {
                PlaySound(NULL, NULL, NULL);
                PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\GameStart.wav"), NULL, SND_ASYNC);
                selected_stage = 2;
                POINT p1_start_point = { 8,5 };
                POINT p2_start_point = { 11,14 };
				init_board_City(board);
                p1.Init_Player(p1_start_point);
                p2.Init_Player(p2_start_point);
				init_train(train);
                p1currentTime = 30;
				p2currentTime = 30;
                int hueFilterMin = 0;   // 원하는 색 필터 범위
                int hueFilterMax = 360;
                HBITMAP original = ResourceManager::GetBitmap(BitmapID::Player1Moving);

                HBITMAP recolored = AdjustBitmapHueWithFilter(original, p1.GetColor(), hueFilterMin, 20);
                ResourceManager::SetBitmap(BitmapID::Player1Moving, recolored);

                original = ResourceManager::GetBitmap(BitmapID::Player2Moving);
                recolored = AdjustBitmapHueWithFilter(original, p2.GetColor(), hueFilterMin, 20);
                ResourceManager::SetBitmap(BitmapID::Player2Moving, recolored);
                p2.SetPlayerNum(2);
                SetTimer(hWnd, TIMER_ID, TIMER_INTERVAL, NULL);
                SetTimer(hWnd, 2, 50, NULL);
            }
            else  if (wParam == '3') {
                PlaySound(NULL, NULL, NULL);
                PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\GameStart.wav"), NULL, SND_ASYNC);
                selected_stage = 3;
                POINT p1_start_point = { 6,5 };
                POINT p2_start_point = { 13,5 };
                int hueFilterMin = 0;   // 원하는 색 필터 범위
                int hueFilterMax = 360;

                HBITMAP original = ResourceManager::GetBitmap(BitmapID::Player1Moving);

                HBITMAP recolored = AdjustBitmapHueWithFilter(original, p1.GetColor(), hueFilterMin, 20);
                ResourceManager::SetBitmap(BitmapID::Player1Moving, recolored);

                original = ResourceManager::GetBitmap(BitmapID::Player2Moving);
                recolored = AdjustBitmapHueWithFilter(original, p2.GetColor(), hueFilterMin, 20);
                ResourceManager::SetBitmap(BitmapID::Player2Moving, recolored);

                p1.Init_Player(p1_start_point);
                p2.Init_Player(p2_start_point);
                p1CurrentHeart = 3;
				p2CurrentHeart = 3;
                p2.SetPlayerNum(2);
                init_board_Ice(board);
                SetTimer(hWnd, TIMER_ID, TIMER_INTERVAL, NULL);
                SetTimer(hWnd, 2, 50, NULL);
            }
            else if (wParam == 'a') {
             if (game_started) {
                 
                    if (p1_check_color_pos > 270) {
                        p1_check_color_pos -= 155;
                        p1.SetColor(p1.GetColor() - 1);
                    }
               
                
                 }
            }
            else if (wParam == 'd') {
                if (game_started) {
                if (p1_check_color_pos < 1200) {
                    p1_check_color_pos += 155;
                    p1.SetColor(p1.GetColor() + 1);
                }
            }
                
            }
        }
        if (selected_stage) {
			
            if (wParam == 'r' && game_end) {
            //게임 리셋
				PlaySound(NULL, NULL, NULL);
                PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Login.wav"), NULL, SND_LOOP | SND_ASYNC);
            if (selected_stage == 1) {
                p1.Init_Player({ 8, 18 });
                p1.Init_Player({ 11, 18 });
                p1Applebox = 0;
                p2Applebox = 0;
            }
            selected_stage = 0;
            p1bombs;
            p2bombs;
            game_started = false;
            game_end = false;
            game_winner = 0;
            p1_check_color_pos = 270;
            p2_check_color_pos = 270;
            p1Applebox = 0;
            p2Applebox = 0;
            for (int i = 0;i < 3;++i) {
				p1_current_item[i] = 0;
				p2_current_item[i] = 0;
            }
			p1bombs.clear();
			p2bombs.clear();
            //캐릭터 색상 원래대로 복구
			HBITMAP original = ResourceManager::GetBitmap(BitmapID::Player1Moving);
			DeleteObject(original);
			original = ResourceManager::GetBitmap(BitmapID::Player2Moving);
			DeleteObject(original);
			ResourceManager::SetBitmap(BitmapID::Player1Moving, LoadBitmap(g_hInst, MAKEINTRESOURCE(IDB_BITMAP_PLAYER)));
			ResourceManager::SetBitmap(BitmapID::Player2Moving, LoadBitmap(g_hInst, MAKEINTRESOURCE(IDB_BITMAP_PLAYER)));

			p1.SetColor(0);
			p2.SetColor(0);
			p1.SetCurrentApple(0);
			p2.SetCurrentApple(0);
			p1currentTime = 100;
			p2currentTime = 100;
		


			p1.SetPlayerNum(1);
			p2.SetPlayerNum(2);

            }
			
        }
        break;
    case WM_PAINT: {
      
            hdc = BeginPaint(hWnd, &ps);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, 1500, 900);
            HGDIOBJ oldBmp = SelectObject(mem, bmp);
            RECT rc = { 0,0,WINDOW_WIDTH,WINDOW_HEIGHT };
            FillRect(mem, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
            if (!game_started) {
                
				// 게임이 시작되지 않은 상태에서의 처리
				HBITMAP loginBitmap = LoadBitmap(g_hInst, MAKEINTRESOURCE(IDB_BITMAP_GAME_LOGIN));
				HDC loginDC = CreateCompatibleDC(mem);
				HBITMAP oldLoginBitmap = (HBITMAP)SelectObject(loginDC, loginBitmap);
				StretchBlt(mem, 0, 0, 1500, 900, loginDC, 0, 0, 800, 600, SRCCOPY);
				SelectObject(loginDC, oldLoginBitmap);
				DeleteObject(loginBitmap);
				DeleteDC(loginDC);
			
            }
            else if (game_started) {
				// 게임이 시작된 상태에서의 처리
                if (selected_stage == 0) {
					HBITMAP selectStageBitmap = LoadBitmap(g_hInst, MAKEINTRESOURCE(IDB_BITMAP_SELECT_STAGE));
					HDC selectStageDC = CreateCompatibleDC(mem);
					HBITMAP oldSelectStageBitmap = (HBITMAP)SelectObject(selectStageDC, selectStageBitmap);
					StretchBlt(mem, 0, 0, 1500, 900, selectStageDC, 0, 0, 1500, 900, SRCCOPY);
					SelectObject(selectStageDC, oldSelectStageBitmap);
					DeleteObject(selectStageBitmap);

                    //체크표시 그리기
					HBITMAP p1CheckBitmap = LoadBitmap(g_hInst, MAKEINTRESOURCE(IDB_BITMAP_P1_CHECK));
					HDC p1CheckDC = CreateCompatibleDC(mem);
					HBITMAP oldP1CheckBitmap = (HBITMAP)SelectObject(p1CheckDC, p1CheckBitmap);
                    TransparentBlt(mem, p1_check_color_pos, 670, 100, 50, p1CheckDC, 0, 0, 224, 150, RGB(255,0,255));
					HBITMAP p2CheckBitmap = LoadBitmap(g_hInst, MAKEINTRESOURCE(IDB_BITMAP_P2_CHECK));
					HDC p2CheckDC = CreateCompatibleDC(mem);
					HBITMAP oldP2CheckBitmap = (HBITMAP)SelectObject(p2CheckDC, p2CheckBitmap);
                    TransparentBlt(mem, p2_check_color_pos, 640, 100, 50, p2CheckDC, 0, 0, 224, 150, RGB(255, 0, 255));

					DeleteDC(selectStageDC);
                    DeleteObject(p1CheckBitmap);
					DeleteObject(p2CheckBitmap);

					DeleteDC(p1CheckDC);
					DeleteDC(p2CheckDC);
                }
				else if (selected_stage >= 1) {
                    //공통UI 출력

					//게임 UI 출력
                    HBITMAP gameui = LoadBitmap(g_hInst, MAKEINTRESOURCE(IDB_BITMAP_GAMEUI));
                    HDC uiDC = CreateCompatibleDC(mem);
                    HBITMAP oldUIBitmap = (HBITMAP)SelectObject(uiDC, gameui);
                    StretchBlt(mem, 0, 0, 1450, 900, uiDC, 0, 0, 1040, 780, SRCCOPY);
                    SelectObject(uiDC, oldUIBitmap);
                    DeleteObject(gameui);
                    DeleteDC(uiDC);
					HBITMAP p1bitmap = ResourceManager::GetBitmap(BitmapID::Player1Moving);
					HBITMAP p2bitmap = ResourceManager::GetBitmap(BitmapID::Player2Moving);
					HBITMAP pmask = ResourceManager::GetBitmap(BitmapID::PlayerMovingMask);
					HDC p1DC = CreateCompatibleDC(mem);
					HDC p2DC = CreateCompatibleDC(mem);
					HBITMAP oldMaskBitmap = (HBITMAP)SelectObject(p1DC, pmask);
					HBITMAP oldP1Bitmap = (HBITMAP)SelectObject(p1DC, p1bitmap);
					HBITMAP oldP2Bitmap = (HBITMAP)SelectObject(p2DC, p2bitmap);
					TransparentBlt(mem, 175, 150, 80, 50, p1DC, 0, 0, 50, 40, RGB(255, 0, 255));
					TransparentBlt(mem, 1195, 150, 80, 50, p2DC, 0, 0, 50, 40, RGB(255, 0, 255));
					SelectObject(p1DC, oldMaskBitmap);
					SelectObject(p1DC, oldP1Bitmap);
					SelectObject(p2DC, oldP2Bitmap);
					DeleteDC(p1DC);
					DeleteDC(p2DC);
					DeleteObject(pmask);
                    //플레이어 아이템 출력
					HBITMAP ItemPowerBitmap = ResourceManager::GetBitmap(BitmapID::ItemPower);
					HBITMAP ItemBombCountBitmap = ResourceManager::GetBitmap(BitmapID::ItemBombCount);
					HBITMAP ItemSpeedBitmap = ResourceManager::GetBitmap(BitmapID::ItemSpeed);
					HDC ItemPowerDC = CreateCompatibleDC(mem);
					HDC ItemBombCountDC = CreateCompatibleDC(mem);
					HDC ItemSpeedDC = CreateCompatibleDC(mem);
					HBITMAP oldItemPowerBitmap = (HBITMAP)SelectObject(ItemPowerDC, ItemPowerBitmap);
					HBITMAP oldItemBombCountBitmap = (HBITMAP)SelectObject(ItemBombCountDC, ItemBombCountBitmap);
                    HBITMAP oldItemSpeedBitmap = (HBITMAP)SelectObject(ItemSpeedDC, ItemSpeedBitmap);
					TransparentBlt(mem, 50, 700, 40, 40, ItemPowerDC, 0, 0, 38, 38, RGB(255, 0, 255));
					TransparentBlt(mem, 1200, 700, 40, 40, ItemPowerDC, 0, 0, 38, 38, RGB(255, 0, 255));
					TransparentBlt(mem, 50, 740, 40, 40, ItemBombCountDC, 0, 0, 38, 38, RGB(255, 0, 255));
					TransparentBlt(mem, 1200, 740, 40, 40, ItemBombCountDC, 0, 0, 38, 38, RGB(255, 0, 255));
					TransparentBlt(mem, 50, 780, 40, 40, ItemSpeedDC, 0, 0, 38, 38, RGB(255, 0, 255));
					TransparentBlt(mem, 1200, 780, 40, 40, ItemSpeedDC, 0, 0, 38, 38, RGB(255, 0, 255));
					//플레이어 아이템 개수 출력
					DrawOutlinedText(mem, 100, 700, std::to_wstring(p1_current_item[0]).c_str());
					DrawOutlinedText(mem, 1250, 700, std::to_wstring(p2_current_item[0]).c_str());
					DrawOutlinedText(mem, 100, 740, std::to_wstring(p1_current_item[1]).c_str());
					DrawOutlinedText(mem, 1250, 740, std::to_wstring(p2_current_item[1]).c_str());
					DrawOutlinedText(mem, 100, 780, std::to_wstring(p1_current_item[2]).c_str());
					DrawOutlinedText(mem, 1250, 780, std::to_wstring(p2_current_item[2]).c_str());

					SelectObject(ItemPowerDC, oldItemPowerBitmap);
					SelectObject(ItemBombCountDC, oldItemBombCountBitmap);
					SelectObject(ItemSpeedDC, oldItemSpeedBitmap);
					DeleteDC(ItemPowerDC);
					DeleteDC(ItemBombCountDC);
					DeleteDC(ItemSpeedDC);
                    if (selected_stage == 1) {
                        draw_tile_forest(mem);
                        draw_Bomb(mem, g_hInst, p1bombs.data(), static_cast<int>(p1bombs.size()), board, &p1);
                        draw_Bomb(mem, g_hInst, p2bombs.data(), static_cast<int>(p2bombs.size()), board, &p2);
                        DrawGrid_Forest(mem, g_hInst, board, &p1, &p2);
                        //p1.Draw_Player(mem, &p1, RGB(255, 0, 255));
                        //p2.Draw_Player(mem, &p2, RGB(0, 255, 255));
                       


                        HBITMAP appleboxBitmap = ResourceManager::GetBitmap(BitmapID::ForestAppleBox);
                        HDC appleboxDC = CreateCompatibleDC(hdc);
                        HBITMAP oldAppleboxBitmap = (HBITMAP)SelectObject(appleboxDC, appleboxBitmap);
                        TransparentBlt(mem, 50, 260, 64, 64, appleboxDC, 0, 0, 64, 64, RGB(255, 0, 255));
                        TransparentBlt(mem, 1200, 260, 64, 64, appleboxDC, 0, 0, 64, 64, RGB(255, 0, 255));

                        HBITMAP applexBitmap = ResourceManager::GetBitmap(BitmapID::ForestApple);
                        HDC applexDC = CreateCompatibleDC(hdc);
                        HBITMAP oldApplexBitmap = (HBITMAP)SelectObject(applexDC, applexBitmap);
                        TransparentBlt(mem, 50, 220, 64, 64, applexDC, 0, 0, 40, 40, RGB(255, 0, 255));
                        TransparentBlt(mem, 1200, 220, 64, 64, applexDC, 0, 0, 40, 40, RGB(255, 0, 255));
                        //임시로 플레이어 사과 출력
                        int tempapple = p1.GetCurrentApple();
                        int tempapple2 = p2.GetCurrentApple();
                        DrawOutlinedText(mem, 125, 220, L" : ");
                        DrawOutlinedText(mem, 1280, 220, L" : ");
                        DrawOutlinedText(mem, 215, 220, std::to_wstring(tempapple).c_str());
                        DrawOutlinedText(mem, 1370, 220, std::to_wstring(tempapple2).c_str());
                        DrawOutlinedText(mem, 125, 270, L" : ");
                        DrawOutlinedText(mem, 1280, 270, L" : ");
                        DrawOutlinedText(mem, 215, 270, std::to_wstring(p1Applebox).c_str());
                        DrawOutlinedText(mem, 1370, 270, std::to_wstring(p2Applebox).c_str());


                        SelectObject(appleboxDC, oldAppleboxBitmap);
                        SelectObject(applexDC, oldApplexBitmap);
                        
                      
                        
                        DeleteDC(appleboxDC);
                        DeleteDC(applexDC);
                    }
					else if (selected_stage == 2) {
						// 다른 스테이지 처리
                        draw_tile_city(mem);
                        draw_Bomb(mem, g_hInst, p1bombs.data(), static_cast<int>(p1bombs.size()), board, &p1);
                        draw_Bomb(mem, g_hInst, p2bombs.data(), static_cast<int>(p2bombs.size()), board, &p2);
						DrawGrid_City(mem, g_hInst, board, &p1, &p2,train);

                        //플레이어 시간 출력
						HBITMAP timeBitmap = ResourceManager::GetBitmap(BitmapID::CityClock);
						HDC timeDC = CreateCompatibleDC(mem);
						HBITMAP oldTimeBitmap = (HBITMAP)SelectObject(timeDC, timeBitmap);
						TransparentBlt(mem, 100, 250, 40, 46, timeDC, 0, 0, 40, 46, RGB(255, 0, 255));
						TransparentBlt(mem, 1255, 250, 40, 46, timeDC, 0, 0, 40, 46, RGB(255, 0, 255));
						wchar_t p1Time[3];
                        swprintf(p1Time, 3, L"%d", p1currentTime);
						DrawOutlinedText(mem, 155, 250, p1Time);
						wchar_t p2Time[3];
						swprintf(p2Time, 3, L"%d", p2currentTime);
						DrawOutlinedText(mem, 1310, 250, p2Time);
						SelectObject(timeDC, oldTimeBitmap);
						DeleteDC(timeDC);

					}
					else if (selected_stage == 3) {
						// 다른 스테이지 처리
                        draw_tile_ice(mem);
                        draw_Bomb(mem, g_hInst, p1bombs.data(), static_cast<int>(p1bombs.size()), board, &p1);
                        draw_Bomb(mem, g_hInst, p2bombs.data(), static_cast<int>(p2bombs.size()), board, &p2);
						DrawGrid_Ice(mem, g_hInst, board, &p1, &p2);
                        wchar_t positionText[50];
                        swprintf(positionText, 50, L"Y: %d, X: %d", p1.GetY(), p1.GetX());

                        //플레이어 하트 출력
						HBITMAP heartBitmap = ResourceManager::GetBitmap(BitmapID::Heart);
						HDC heartDC = CreateCompatibleDC(mem);
						HBITMAP oldHeartBitmap = (HBITMAP)SelectObject(heartDC, heartBitmap);
						for (int i = 0; i < p1CurrentHeart; ++i) {
							TransparentBlt(mem, 10 + i * 90, 250, 80, 64, heartDC, 0, 0, 80, 64, RGB(255, 0, 255));
						}
						for (int i = 0; i < p2CurrentHeart; ++i) {
							TransparentBlt(mem, 1170 + i * 90, 250, 80, 64, heartDC, 0, 0, 80, 64, RGB(255, 0, 255));
						}
						SelectObject(heartDC, oldHeartBitmap);
						
						DeleteDC(heartDC);
					}
                    //화면 프레임 출력
                   // std::wstring fpsText = L"FPS: " + std::to_wstring(currentFPS);
                   // DrawOutlinedText(mem, 10, 10, fpsText.c_str());  

					
					
				}
            
            
            }
           
            if (game_end) {
				
                DrawDarkOverlay(mem, 1500, 900);
                HBITMAP gameEndBitmap = LoadBitmap(g_hInst, MAKEINTRESOURCE(IDB_BITMAP_GAME_END));
                HDC gameEndDC = CreateCompatibleDC(mem);
                HBITMAP oldGameEndBitmap = (HBITMAP)SelectObject(gameEndDC, gameEndBitmap);
                if (game_winner == 1) {
					TransparentBlt(mem, 1100, 400, 348, 65, gameEndDC, 0, 0, 348, 65, RGB(255, 0, 255));
                    TransparentBlt(mem, 50, 400, 348, 65, gameEndDC, 0, 65, 348, 65, RGB(255, 0, 255));
				}
                else if (game_winner == 2) {
                    // 플레이어 2 승리 처리
                    TransparentBlt(mem, 50, 400, 348, 65, gameEndDC, 0, 0, 348, 65, RGB(255, 0, 255));
                    TransparentBlt(mem, 1100, 400, 348, 65, gameEndDC, 0, 65, 348, 65, RGB(255, 0, 255));
                }
              
                DrawOutlinedText(mem, 500, 600,L"다시 시작하려면 R을 누르시오 .");  // 
                SelectObject(gameEndDC, oldGameEndBitmap);
                DeleteObject(gameEndBitmap);
                DeleteDC(gameEndDC);
            }
            BitBlt(hdc, 0, 0, 1500, 900, mem, 0, 0, SRCCOPY);
            SelectObject(mem, oldBmp);
            DeleteObject(bmp);
            
            DeleteDC(mem);
            EndPaint(hWnd, &ps);
        
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hWnd, TIMER_ID);
       
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

