#include"Bomb.h"
#include "ResourceManager.h"
void Bomb::make_Bomb(Player player, Board board[MAP_TILES][MAP_TILES])
{
	
	this->pos.x = board[(int)player.GetY()][(int)player.GetX()].pos.x;
	this->pos.y = board[(int)player.GetY()][(int)player.GetX()].pos.y;
    this->logicalPos.x = (int)player.GetY();
    this->logicalPos.y = (int)player.GetX();
	this->reach = player.GetBomb_distance();
    this->reachLeft = this->reachRight = this->reachUp = this->reachDown = this->reach;
	this->state = 1;
	this->time = 20;
    this->isCollisionLeft = false;
    this->isCollisionRight = false;
    this->isCollisionUp = false;
    this->isCollisionDown = false;
    this->is_collision_player = false;
    board[(int)player.GetY()][(int)player.GetX()].obstacle = 9;
}

bool Bomb::tick_Bomb()
{	if(this->time>0)
		this->time--;
	else if (this->time <= 0&&this->state==1) {
		this->state = 2;
		this->time = 10;
        PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Bubble.wav"), NULL, SND_FILENAME | SND_ASYNC);
	}
	else if (this->time <= 0 && this->state == 2) {
	this->state = 0;
	return true;
	}
	return false;
}

int Bomb::collision_bomb_player(Bomb bomb, Player player)
{
    int bx = bomb.GetLogicalX();
    int by = bomb.GetLogicalY();
    int px = player.GetY();
    int py = player.GetX();

    // 폭탄 중심에 있는 경우
    if (bx == px && by == py) {
        return 1;
    }

    // 수평 방향 사거리 판정 (같은 Y축, X범위 내)
    if (by == py && px >= bx - bomb.GetReachLeft() && px <= bx + bomb.GetReachRight()) {
        return 1;
    }

    // 수직 방향 사거리 판정 (같은 X축, Y범위 내)
    if (bx == px && py >= by - bomb.GetReachUp() && py <= by + bomb.GetReachDown()) {
        return 1;
    }

    return 0; // 충돌 없음
}


void draw_Bomb(HDC hDC,HINSTANCE g_hInst,Bomb bombs[], int n, Board board[MAP_TILES][MAP_TILES],Player *player)
{
  
    HBITMAP hbitmapState1 = ResourceManager::GetBitmap(BitmapID::BombSet);
    HBITMAP hbitmapcenter= ResourceManager::GetBitmap(BitmapID::BombCenter);;
    HBITMAP hbitmapleft= ResourceManager::GetBitmap(BitmapID::BombLeft2);
	HBITMAP hbitmapright= ResourceManager::GetBitmap(BitmapID::BombRight2);
	HBITMAP hbitmapup = ResourceManager::GetBitmap(BitmapID::BombUp2);;
	HBITMAP hbitmapdown = ResourceManager::GetBitmap(BitmapID::BombDown2);
    HDC memDC = CreateCompatibleDC(hDC);

    for (int i = 0; i < n; ++i) {
        int x = bombs[i].GetX();
        int y = bombs[i].GetY();
        int reach = bombs[i].GetReach();
        int logicX = bombs[i].GetLogicalY();
        int logicY = bombs[i].GetLogicalX();
        // 설치 상태
        if (bombs[i].GetState() == 1) {
            
                if (bombs[i].GetState() != 1) continue;  // 설치 상태만
               
                
               
               
                HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, hbitmapState1);

                int x = bombs[i].GetX();
                int y = bombs[i].GetY();
                int currentFrame = bombs[i].GetTime() % 4;
                int drawSize = MAP_TILES * 2;
                TransparentBlt(
                    hDC, x - MAP_TILES, y - MAP_TILES, drawSize, drawSize,
                    memDC, 56 * currentFrame, 0, 54, 54,
                    RGB(255, 0, 255)
                );
                SelectObject(memDC, oldBitmap);
                
               
         
         
        }
        // 터진 상태
        else if (bombs[i].GetState() == 2) {
            int cx = x / (MAP_TILES * 2);
            int cy = y / (MAP_TILES * 2);

            // 본체
          
            HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, hbitmapcenter);

           // int x = bombs[i].GetX();
           // int y = bombs[i].GetY();
            int currentFrame = bombs[i].GetTime() % 3;
            int drawSize = MAP_TILES * 2;
            TransparentBlt(
                hDC, x - MAP_TILES, y - MAP_TILES, drawSize, drawSize,
                memDC, 52 * currentFrame, 0, 52, 52,
                RGB(255, 0, 255)
            );
            SelectObject(memDC, oldBitmap);
           
            // 방향별로 퍼트림 (벽에 막히면 중단)
            //좌
            bool is_collision = false;
            for (int j = 0;j < bombs[i].GetReachLeft();++j) {
               
                

              
                oldBitmap = (HBITMAP)SelectObject(memDC, hbitmapleft);
                int nx = logicX - j - 1;
                if (nx < 0) break;  // 바깥으로 나가지 않도록!

                if (board[logicY][nx].obstacle == 0|| board[logicY][nx].obstacle==9) {
                    TransparentBlt(
                        hDC, x - 3 * MAP_TILES - MAP_TILES * (2 * j),y-MAP_TILES , drawSize, drawSize,
                        memDC, 52 * currentFrame, 0, 52, 52,
                        RGB(255, 0, 255)
                    );
                }
                else if (board[logicY][nx].obstacle == 1) {
                    if (!bombs[i].GetIsCollisionLeft()) {
                        board[logicY][nx].obstacle = 0;
                        bombs[i].SetReachLeft(j+1);
                    }
                    is_collision = true;
                   
                }
                else if (board[logicY][nx].obstacle == 2) {
					if (bombs[i].GetIsCollisionLeft() == false) {
                        bombs[i].SetIsCollisionLeft(true);
                        bombs[i].SetReachLeft(j + 1);
						player->SetCurrentApple(player->GetCurrentApple() + 1); // 플레이어의 사과 수 증가
					}
					
                 

                }
                SelectObject(memDC, oldBitmap);
               
                if (is_collision) {
                    bombs[i].SetIsCollisionLeft(true);
                    break;
                }
                
            }
            //우
            is_collision = false;
            for (int j = 0;j < bombs[i].GetReachRight();++j) {
              //  int logicX = bombs[i].GetLogicalY();
              //  int logicY = bombs[i].GetLogicalX();

              
                oldBitmap = (HBITMAP)SelectObject(memDC, hbitmapright);
                int nx = logicX + j + 1;
                if (nx >= MAP_TILES) break;  // 바깥으로 나가지 않도록!

                if (board[logicY][nx].obstacle == 0|| board[logicY][nx].obstacle==9) {
                    TransparentBlt(
                        hDC, x + MAP_TILES + MAP_TILES * (2 * j), y - MAP_TILES, drawSize, drawSize,
                        memDC, 52 * currentFrame, 0, 52, 52,
                        RGB(255, 0, 255)
                    );
                }
                else if (board[logicY][nx].obstacle == 1) {
                    if (!bombs[i].GetIsCollisionRight()) {
                        board[logicY][nx].obstacle = 0;
                        bombs[i].SetReachRight(j+1);
                    }
                    is_collision = true;

                }
                else if (board[logicY][nx].obstacle == 2) {
                    if (bombs[i].GetIsCollisionRight() == false) {
                        bombs[i].SetIsCollisionRight(true);
                        bombs[i].SetReachRight(j + 1);
                        player->SetCurrentApple(player->GetCurrentApple() + 1); // 플레이어의 사과 수 증가
                    }
                }
                SelectObject(memDC, oldBitmap);
                
                if (is_collision) {
                    bombs[i].SetIsCollisionRight(true);
                    break;
                }
            }
            //상
            is_collision = false;
            for (int j = 0;j < bombs[i].GetReachUp();++j) {
               // int logicX = bombs[i].GetLogicalY();
              //  int logicY = bombs[i].GetLogicalX();
              
         
                oldBitmap = (HBITMAP)SelectObject(memDC, hbitmapup);
                int ny = logicY - j - 1;
                if (ny <0) break;  // 바깥으로 나가지 않도록!

                if (board[ny][logicX].obstacle == 0|| board[ny][logicX].obstacle==9) {
                    TransparentBlt(
                        hDC, x - MAP_TILES, y - 3*MAP_TILES - MAP_TILES * (2 * j), drawSize, drawSize,
                        memDC, 52 * currentFrame, 0, 52, 52,
                        RGB(255, 0, 255)
                    );
                }
                else if (board[ny][logicX].obstacle == 1) {
                    if (!bombs[i].GetIsCollisionUp()) {
                        board[ny][logicX].obstacle = 0;
                        bombs[i].SetReachUp(j+1);
                    }
                    is_collision = true;
                    
                }
                else if (board[ny][logicX].obstacle == 2) {
                    if (bombs[i].GetIsCollisionUp() == false) {
                        bombs[i].SetIsCollisionUp(true);
                        bombs[i].SetReachUp(j + 1);
                        player->SetCurrentApple(player->GetCurrentApple() + 1); // 플레이어의 사과 수 증가
                    }
                }
                SelectObject(memDC, oldBitmap);
             
                if (is_collision) {
                    bombs[i].SetIsCollisionUp(true);
                    break;
                }
            }
            //하
            is_collision = false;
            for (int j = 0;j < bombs[i].GetReachDown();++j) {
             //   int logicX = bombs[i].GetLogicalY();
              //  int logicY = bombs[i].GetLogicalX();
    
               // hbitmap = LoadBitmap(g_hInst, MAKEINTRESOURCE(IDB_BITMAP_BOMB_DOWN2));
                oldBitmap = (HBITMAP)SelectObject(memDC, hbitmapdown);
                    int ny = logicY + j+1;
                    if (ny >= MAP_TILES) break;  // 바깥으로 나가지 않도록!

                    if (board[ny][logicX].obstacle == 0|| board[ny][logicX].obstacle==9) {
                        TransparentBlt(
                            hDC, x - MAP_TILES, y + MAP_TILES+ MAP_TILES * (2 * j), drawSize, drawSize,
                            memDC, 52 * currentFrame, 0, 52, 52,
                            RGB(255, 0, 255)
                        );
                    }
                    else if(board[ny][logicX].obstacle == 1){
                        if (!bombs[i].GetIsCollisionDown()) {
                            board[ny][logicX].obstacle = 0;
                            bombs[i].SetReachDown(j+1);
                        }
                        is_collision = true;
                       
                    }
                    else if (board[ny][logicX].obstacle == 2) {
                        if (bombs[i].GetIsCollisionDown() == false) {
                            bombs[i].SetIsCollisionDown(true);
                            bombs[i].SetReachDown(j + 1);
                            player->SetCurrentApple(player->GetCurrentApple() + 1); // 플레이어의 사과 수 증가
                        }
                    }
                    SelectObject(memDC, oldBitmap);
                   
                    if (is_collision) {
                        bombs[i].SetIsCollisionDown(true);
                        break;
                    }
                  
            }
            //출력끝
          
            SelectObject(memDC, oldBitmap);
           
          
            
           
        }
    }
    DeleteDC(memDC);
    
}
