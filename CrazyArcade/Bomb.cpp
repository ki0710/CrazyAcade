#include"Bomb.h"
#include "BoardRender.h"
#include "ResourceManager.h"
bool Bomb::make_Bomb(const Player& player, Board board[MAP_TILES][MAP_TILES])
{
    const int col = player.GetX();
    const int row = player.GetY();
    if (!CanMoveTo(col, row, board)) return false;

    this->pos = GetBoardScreenPosition(col, row);
    this->logicalPos.x = row;
    this->logicalPos.y = col;
	this->reach = player.GetBomb_distance();
    this->reachLeft = this->reachRight = this->reachUp = this->reachDown = this->reach;
	this->state = 1;
	this->time = 20;
    this->isCollisionLeft = false;
    this->isCollisionRight = false;
    this->isCollisionUp = false;
    this->isCollisionDown = false;
    this->is_collision_player = false;
    this->is_collision_owner = false;
    board[row][col].obstacle = 9;
    return true;
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
    // Bomb stores row in logicalPos.x and column in logicalPos.y.
    int bombRow = bomb.GetLogicalX();
    int bombCol = bomb.GetLogicalY();
    // The sprite is centered on px/py; x/y stay on the old tile until the move finishes.
    const int playerRow = static_cast<int>(player.GetPy() + 0.5f);
    const int playerCol = static_cast<int>(player.GetPx() + 0.5f);

    // The center tile is always part of the explosion.
    if (bombRow == playerRow && bombCol == playerCol) {
        return 1;
    }

    // Left/right extend along columns within the same row.
    if (playerRow == bombRow &&
        playerCol >= bombCol - bomb.GetReachLeft() &&
        playerCol <= bombCol + bomb.GetReachRight()) {
        return 1;
    }

    // Up/down extend along rows within the same column.
    if (playerCol == bombCol &&
        playerRow >= bombRow - bomb.GetReachUp() &&
        playerRow <= bombRow + bomb.GetReachDown()) {
        return 1;
    }

    return 0;
}


// Called once when a bomb enters the explosion state, before player hit tests.
void update_BombExplosion(Bomb& bomb, Board board[MAP_TILES][MAP_TILES], Player* player, bool isForest)
{
    if (bomb.GetState() != 2) return;
    int logicX = bomb.GetLogicalY();
    int logicY = bomb.GetLogicalX();
    if (board[logicY][logicX].obstacle == 9)
        board[logicY][logicX].obstacle = 0;
    for (int j = 0; j < bomb.GetReachLeft(); ++j) {
        int nx = logicX - j - 1;
        if (nx < 0) break;
        if (board[logicY][nx].obstacle == 1) {
            if (!bomb.GetIsCollisionLeft()) {
                BreakBoardObstacle(board[logicY][nx]);
                bomb.SetReachLeft(j + 1);
            }
            bomb.SetIsCollisionLeft(true);
            break;
        }
        else if (board[logicY][nx].obstacle == 2) {
            if (!bomb.GetIsCollisionLeft()) {
                bomb.SetIsCollisionLeft(true);
                bomb.SetReachLeft(j + 1);
                if (isForest) player->SetCurrentApple(player->GetCurrentApple() + 1);
            }
        }
    }
    for (int j = 0; j < bomb.GetReachRight(); ++j) {
        int nx = logicX + j + 1;
        if (nx >= MAP_TILES) break;
        if (board[logicY][nx].obstacle == 1) {
            if (!bomb.GetIsCollisionRight()) {
                BreakBoardObstacle(board[logicY][nx]);
                bomb.SetReachRight(j + 1);
            }
            bomb.SetIsCollisionRight(true);
            break;
        }
        else if (board[logicY][nx].obstacle == 2) {
            if (!bomb.GetIsCollisionRight()) {
                bomb.SetIsCollisionRight(true);
                bomb.SetReachRight(j + 1);
                if (isForest) player->SetCurrentApple(player->GetCurrentApple() + 1);
            }
        }
    }
    for (int j = 0; j < bomb.GetReachUp(); ++j) {
        int ny = logicY - j - 1;
        if (ny < 0) break;
        if (board[ny][logicX].obstacle == 1) {
            if (!bomb.GetIsCollisionUp()) {
                BreakBoardObstacle(board[ny][logicX]);
                bomb.SetReachUp(j + 1);
            }
            bomb.SetIsCollisionUp(true);
            break;
        }
        else if (board[ny][logicX].obstacle == 2) {
            if (!bomb.GetIsCollisionUp()) {
                bomb.SetIsCollisionUp(true);
                bomb.SetReachUp(j + 1);
                if (isForest) player->SetCurrentApple(player->GetCurrentApple() + 1);
            }
        }
    }
    for (int j = 0; j < bomb.GetReachDown(); ++j) {
        int ny = logicY + j + 1;
        if (ny >= MAP_TILES) break;
        if (board[ny][logicX].obstacle == 1) {
            if (!bomb.GetIsCollisionDown()) {
                BreakBoardObstacle(board[ny][logicX]);
                bomb.SetReachDown(j + 1);
            }
            bomb.SetIsCollisionDown(true);
            break;
        }
        else if (board[ny][logicX].obstacle == 2) {
            if (!bomb.GetIsCollisionDown()) {
                bomb.SetIsCollisionDown(true);
                bomb.SetReachDown(j + 1);
                if (isForest) player->SetCurrentApple(player->GetCurrentApple() + 1);
            }
        }
    }
}

void draw_Bomb(HDC hDC,HINSTANCE g_hInst,const Bomb bombs[], int n, const Board board[MAP_TILES][MAP_TILES])
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
                SelectObject(memDC, oldBitmap);
                
            }
            //우
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
                SelectObject(memDC, oldBitmap);
            }
            //상
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
                SelectObject(memDC, oldBitmap);
            }
            //하
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
                    SelectObject(memDC, oldBitmap);
                  
            }
            //출력끝
          
            SelectObject(memDC, oldBitmap);
           
          
            
           
        }
    }
    DeleteDC(memDC);
    
}
