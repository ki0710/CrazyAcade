#include "Player.h"
#include "ResourceManager.h"
Player::Player()
{
}

Player::~Player()
{
}

void Player::Init_Player(POINT pos)
{
    int mapSize = min(WINDOW_WIDTH, WINDOW_HEIGHT);
    int tempx = 85;
    int tempy= mapSize / MAP_TILES / 2;
    this->x = pos.x;
    this->y = pos.y;

    // 초기 목적지 좌표는 현재 좌표와 동일하게
    this->destX = pos.x;
    this->destY = pos.y;


    this->speed = 1.0f;
    // 실수형 좌표(px, py)도 현재 좌표로 초기화
    this->px = static_cast<float>(pos.x);
    this->py = static_cast<float>(pos.y);

    // 방향 초기화
    this->dir = DIR_DOWN;

    //폭탄 사거리 초기화
    this->SetBomb_distance(1);
    this->maxBomb = 2;
    this->currrentApple = 0;

    //선택 캐릭터 초기화
    this->selected_character = 1;
   
	this->move_cnt = 0;
    this->player_num = 1;

    this->is_hit = false;
    // 스프라이트 핸들 초기화
    this->hSprite = NULL;
}


void Player::Draw_Player(HDC hdc, Player* p, int character)
{
    if (p->GetIsHit()&& GetTickCount()%2==1)return;
    int mapSize = min(WINDOW_WIDTH, WINDOW_HEIGHT);
    int tile = mapSize / MAP_TILES;
    int ox = (WINDOW_WIDTH - mapSize) / 2;
    int oy = (WINDOW_HEIGHT - mapSize) / 2;
    float drawX = 85 + ox + p->px * tile;
    float drawY = oy + p->py * tile + 20;
    
    HBITMAP hbitmapPlayer = ResourceManager::GetBitmap(BitmapID::Player1Moving);
  if(p->GetPlayerNum() == 2)
      hbitmapPlayer = ResourceManager::GetBitmap(BitmapID::Player2Moving);
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, hbitmapPlayer);
    // 현재 방향에 따라 스프라이트의 위치를 계산
    int spriteX = 0;
    int spriteY = 0;
    if (p->dir == DIR_UP) {
        spriteY = 60 * 1; // 위쪽 스프라이트
    }
    else if (p->dir == DIR_DOWN) {
        spriteY = 60 * 2; // 아래쪽 스프라이트
    }
    else if (p->dir == DIR_LEFT) {
        spriteY = 60 * 3; // 왼쪽 스프라이트
    }
    else if (p->dir == DIR_RIGHT) {
        spriteY = 60 * 4; // 오른쪽 스프라이트
    }

    spriteX = p->move_cnt / 4 * 50; // 스프라이트의 X 위치 (애니메이션 프레임)

    TransparentBlt(
        hdc, drawX, drawY - 2 * MAP_TILES + 10, MAP_TILES * 2, MAP_TILES * 3,
        memDC, spriteX, spriteY, 50, 60,
        RGB(255, 0, 255)
    );
    HBITMAP hbitmapPlayermask = ResourceManager::GetBitmap(BitmapID::PlayerMovingMask);
    HBITMAP oldBitmapmask = (HBITMAP)SelectObject(memDC, hbitmapPlayermask);
    TransparentBlt(
        hdc, drawX, drawY - 2 * MAP_TILES + 10, MAP_TILES * 2, MAP_TILES * 3,
        memDC, spriteX, spriteY, 50, 60,
        RGB(255, 0, 255)
    );
    SelectObject(memDC, oldBitmap);
    SelectObject(memDC, oldBitmapmask);
    DeleteDC(memDC);
}
void Player::on_applebox(Player* player, int* applebox, int boxX, int boxY)
{
    int px = player->GetX();
    int py = player->GetY();

    if ((px == boxX && py == boxY - 1) ||  // 위쪽
        (px == boxX - 1 && py == boxY) ||  // 왼쪽
        (px == boxX + 1 && py == boxY))    // 오른쪽
    {
        if (player->GetCurrentApple() > 0) {
        
            (*applebox) += player->GetCurrentApple();;                // 박스 수 증가
            player->SetCurrentApple(0);
        }
    }
}

int Player::collision_item(Player* player, Board board[MAP_TILES][MAP_TILES],int& Pcurrenttime)
{
	int px = player->GetX();
	int py = player->GetY();
	// 플레이어가 아이템을 먹었는지 확인
	if (board[py][px].item == 1) { // 물풍선 사거리 증가
		player->SetBomb_distance(player->GetBomb_distance() + 1);
		board[py][px].item = 0; // 아이템 사용 후 제거
        return 1;
	}
	else if (board[py][px].item == 2) { // 물풍선 개수 증가
		player->SetMaxBomb(player->GetMaxBomb() + 1);
		board[py][px].item = 0; // 아이템 사용 후 제거
        return 2;
	}
	else if (board[py][px].item == 3) { // 이동 속도 증가
		if (player->Getspeed() < 1.5f) // 최대 속도 제한
		player->SetSpeed(player->Getspeed() + 0.1f);
		board[py][px].item = 0; // 아이템 사용 후 제거
        return 3;
	}
	else if (board[py][px].item == 4) { // 시계 아이템 도시맵 전용
		if (Pcurrenttime > 0) {
			Pcurrenttime += 5; // 시간 증가
			board[py][px].item = 0; // 아이템 사용 후 제거
		}
		
	}
    return 0;
}

int Player::collision_train(Player* player, Train& train, Board board[MAP_TILES][MAP_TILES], int& Pcurrenttime)
{
	int px = board[player->GetY()][player->GetX()].pos.x; // 플레이어의 x 좌표
	int py = board[player->GetY()][player->GetX()].pos.y; // 플레이어의 y 좌표
	// 플레이어가 기차와 충돌했는지 확인
    if (train.is_spawned) {
        if (px >= train.Pos.left && px <= train.Pos.right) {
            if (py >= train.Pos.top && py <= train.Pos.bottom) {
                Pcurrenttime -= 5;
				player->SetIsHit(true);
                return 1;
            }


        }
    }
    return 0;
}


void HandlePlayerMovement(Player& player, const bool* keyStates, int upKey, int downKey, int leftKey, int rightKey, Board board[MAP_TILES][MAP_TILES]) {
    if (player.IsStopped()) {
        int x = player.GetX();
        int y = player.GetY();

        if (keyStates[upKey]) {
            if (y > 0 && board[y - 1][x].obstacle == 0) {
                player.SetDir(DIR_UP);
                player.SetDestY(y - 1);
            }
        }
        else if (keyStates[downKey]) {
            if (y < MAP_TILES - 1 && board[y + 1][x].obstacle == 0) {
                player.SetDir(DIR_DOWN);
                player.SetDestY(y + 1);
            }
        }
        else if (keyStates[leftKey]) {
            if (x > 0 && board[y][x - 1].obstacle == 0) {
                player.SetDir(DIR_LEFT);
                player.SetDestX(x - 1);
            }
        }
        else if (keyStates[rightKey]) {
            if (x < MAP_TILES - 1 && board[y][x + 1].obstacle == 0) {
                player.SetDir(DIR_RIGHT);
                player.SetDestX(x + 1);
            }
        }
    }
}

void UpdatePlayerPosition(Player& player, float speed) {
    if (player.IsMoving()) {
        float dx = player.GetDestX() - player.GetPx();
        float dy = player.GetDestY() - player.GetPy();

        if (fabs(dx) > speed) player.SetPx(player.GetPx() + (dx > 0 ? speed : -speed));
        else player.SetPx(player.GetDestX());

        if (fabs(dy) > speed) player.SetPy(player.GetPy() + (dy > 0 ? speed : -speed));
        else player.SetPy(player.GetDestY());

        if (!player.IsMoving()) player.SnapToDest();
        
        
       
        player.SetMoveCount((player.GetMoveCount() + 1) % 16);
        
    }
}
void DrawGrid_Forest(HDC hdc, HINSTANCE g_hInst, Board board[MAP_TILES][MAP_TILES], Player* p1, Player* p2) {
    int mapSize = min(WINDOW_WIDTH, WINDOW_HEIGHT);
    int tile = mapSize / MAP_TILES / 2;
    int ox = (WINDOW_WIDTH - mapSize) / 2;
    int oy = (WINDOW_HEIGHT - mapSize) / 2;

  
    HDC tileDC = CreateCompatibleDC(hdc);
    HBITMAP tilebit[6];
    tilebit[0] = ResourceManager::GetBitmap(BitmapID::ForestTile1);
    tilebit[1] = ResourceManager::GetBitmap(BitmapID::ForestTile2);
    tilebit[2] = ResourceManager::GetBitmap(BitmapID::ForestTile3);
    tilebit[3] = ResourceManager::GetBitmap(BitmapID::ForestTile4);
    tilebit[4] = ResourceManager::GetBitmap(BitmapID::ForestTile5);
    tilebit[5] = ResourceManager::GetBitmap(BitmapID::ForestTile6);
    HBITMAP  oldTile = (HBITMAP)SelectObject(tileDC, tilebit[0]);
    HBITMAP obstacleBitmap[5];
    obstacleBitmap[0] = ResourceManager::GetBitmap(BitmapID::ForestObstacle1);
    obstacleBitmap[1] = ResourceManager::GetBitmap(BitmapID::ForestObstacle2);
    obstacleBitmap[2] = ResourceManager::GetBitmap(BitmapID::ForestObstacle3);
    obstacleBitmap[3] = ResourceManager::GetBitmap(BitmapID::ForestObstacle4);
    obstacleBitmap[4] = ResourceManager::GetBitmap(BitmapID::ForestObstacle5);
    HDC obstacleDC = CreateCompatibleDC(hdc);
    HBITMAP oldObstacleBitmap = (HBITMAP)SelectObject(obstacleDC, obstacleBitmap[0]);

    HBITMAP itemBitmap[3];
    itemBitmap[0] = ResourceManager::GetBitmap(BitmapID::ItemPower);
	itemBitmap[1] = ResourceManager::GetBitmap(BitmapID::ItemBombCount);
	itemBitmap[2] = ResourceManager::GetBitmap(BitmapID::ItemSpeed);

	HDC itemDC = CreateCompatibleDC(hdc);
	HBITMAP oldItemBitmap = (HBITMAP)SelectObject(itemDC, itemBitmap[0]);

    for (int i = 0; i < MAP_TILES; ++i) {
        for (int j = 0; j < MAP_TILES; ++j) {
          
            if (j == 11 && i == 2) {
                HBITMAP treebit = ResourceManager::GetBitmap(BitmapID::ForestTree);
                HDC treeDC = CreateCompatibleDC(hdc);
                HBITMAP oldTreeBitmap = (HBITMAP)SelectObject(treeDC, treebit);
                TransparentBlt(hdc, board[1][8].pos.x - 10, board[1][9].pos.y - 3 * MAP_TILES, 7 * MAP_TILES, 6 * MAP_TILES, treeDC, 0, 0, 100, 80, RGB(255, 0, 255));
                SelectObject(treeDC, oldTreeBitmap);

                DeleteDC(treeDC);
            }
            if (board[i][j].item >= 1&&board[i][j].obstacle==0) {
                oldItemBitmap = (HBITMAP)SelectObject(itemDC, itemBitmap[board[i][j].item-1]);
				TransparentBlt(hdc, board[i][j].pos.x - MAP_TILES, board[i][j].pos.y - MAP_TILES - 10, 2 * MAP_TILES, 2 * MAP_TILES, itemDC, 0, 0, 38, 38, RGB(255, 0, 255));
				SelectObject(itemDC, oldItemBitmap);
            }
            if (j == p1->GetX() && i == p1->GetY()) {
               
            
                p1->Draw_Player(hdc, p1, p1->GetSelectedCharacter());
            }
            if (j == p2->GetX() && i == p2->GetY()) {
                p2->Draw_Player(hdc, p2, p2->GetSelectedCharacter());
            }



            if (board[i][j].obstacle == 1) {

                (HBITMAP)SelectObject(obstacleDC, obstacleBitmap[board[i][j].obstacle_tpye - 1]);
                TransparentBlt(hdc, board[i][j].pos.x - MAP_TILES - 5, board[i][j].pos.y - MAP_TILES - 5, 2 * MAP_TILES + 10, 2 * MAP_TILES + 10, obstacleDC, 0, 0, 68, 67, RGB(255, 0, 255));
                SelectObject(obstacleDC, oldObstacleBitmap);
            }
            switch (board[i][j].obstacle) {
            case 3:  // 갈색 (바구니)
            {
                HBITMAP appleboxBitmap = ResourceManager::GetBitmap(BitmapID::ForestAppleBox);
                HDC appleboxDC = CreateCompatibleDC(hdc);
                HBITMAP oldAppleboxBitmap = (HBITMAP)SelectObject(appleboxDC, appleboxBitmap);
                TransparentBlt(hdc, board[i][j].pos.x - MAP_TILES - 10, board[i][j].pos.y - MAP_TILES - 30, 2 * MAP_TILES + 20, 2 * MAP_TILES + 30, appleboxDC, 0, 0, 64, 64, RGB(255, 0, 255));
                SelectObject(appleboxDC, oldAppleboxBitmap);
                DeleteDC(appleboxDC);
            }
            break;
            default:
        
                break; // 0번 장애물은 그리지 않음
            }


        }

    }
 
 

	SelectObject(itemDC, oldItemBitmap);
    DeleteDC(itemDC);
	SelectObject(obstacleDC, oldObstacleBitmap);
    DeleteDC(obstacleDC);
    SelectObject(tileDC, oldTile);
    DeleteDC(tileDC);

}

void DrawGrid_City(HDC hdc, HINSTANCE g_hInst, Board board[MAP_TILES][MAP_TILES], Player* p1, Player* p2, Train train)
{
    int mapSize = min(WINDOW_WIDTH, WINDOW_HEIGHT);
    int tile = mapSize / MAP_TILES / 2;
    int ox = (WINDOW_WIDTH - mapSize) / 2;
    int oy = (WINDOW_HEIGHT - mapSize) / 2;

    RECT boardRect = { 320, 0, 1125, 1000 }; // 원하는 사각형 영역

    //건물 리소스
    HBITMAP building1Bitmap = ResourceManager::GetBitmap(BitmapID::CityBuilding1);
    HDC  building1DC = CreateCompatibleDC(hdc);
    HBITMAP  oldbuilding1Bitmap = (HBITMAP)SelectObject(building1DC, building1Bitmap);
    HBITMAP building2Bitmap = ResourceManager::GetBitmap(BitmapID::CityBuilding2);
    HDC  building2DC = CreateCompatibleDC(hdc);
    HBITMAP  oldbuilding2Bitmap = (HBITMAP)SelectObject(building2DC, building2Bitmap);
	HBITMAP bulding3Bitmap = ResourceManager::GetBitmap(BitmapID::CityBuilding3);
	HDC  building3DC = CreateCompatibleDC(hdc);
	HBITMAP  oldbuilding3Bitmap = (HBITMAP)SelectObject(building3DC, bulding3Bitmap);
    //장애물 리소스
	HBITMAP obstacleBitmap = ResourceManager::GetBitmap(BitmapID::CityObstacles);
	HDC obstacleDC = CreateCompatibleDC(hdc);
	HBITMAP oldObstacleBitmap = (HBITMAP)SelectObject(obstacleDC, obstacleBitmap);
	HBITMAP TrashboxBitmap = ResourceManager::GetBitmap(BitmapID::CityTrashbox);
	HDC TrashboxDC = CreateCompatibleDC(hdc);
	HBITMAP oldTrashboxBitmap = (HBITMAP)SelectObject(TrashboxDC, TrashboxBitmap);
    //신호등 리소스
	HBITMAP BlinkerBitmap = ResourceManager::GetBitmap(BitmapID::CityBlinkerOFF);
	HDC BlinkerDC = CreateCompatibleDC(hdc);
	HBITMAP oldBlinkerBitmap = (HBITMAP)SelectObject(BlinkerDC, BlinkerBitmap);
	HBITMAP BlinkerBitmapON = ResourceManager::GetBitmap(BitmapID::CityBlinkerON);
	HBITMAP oldBlinkerBitmapON = (HBITMAP)SelectObject(BlinkerDC, BlinkerBitmapON);

	// 아이템 리소스
    HBITMAP itemBitmap[4];
    itemBitmap[0] = ResourceManager::GetBitmap(BitmapID::ItemPower);
    itemBitmap[1] = ResourceManager::GetBitmap(BitmapID::ItemBombCount);
    itemBitmap[2] = ResourceManager::GetBitmap(BitmapID::ItemSpeed);
    itemBitmap[3] = ResourceManager::GetBitmap(BitmapID::CityClock);
    HDC itemDC = CreateCompatibleDC(hdc);
    HBITMAP oldItemBitmap = (HBITMAP)SelectObject(itemDC, itemBitmap[0]);

   
    // 1. 기존 클리핑 영역 설정
    SaveDC(hdc); // 현재 DC 상태 저장 (복원 시 필요)
    IntersectClipRect(hdc, boardRect.left, boardRect.top, boardRect.right, boardRect.bottom);

    // 2. 특정 영역 내에서만 그리기
   
    for (int i = 0; i < MAP_TILES; ++i) {
        for (int j = 0; j < MAP_TILES; ++j) {
            if (board[i][j].obstacle==1) {
                if (board[i][j].obstacle_tpye == 1) {
					TransparentBlt(hdc, board[i][j].pos.x - MAP_TILES, board[i][j].pos.y - 90, 40, 112, obstacleDC, 560, 0, 40, 112, RGB(255, 0, 255));
				}
				else if (board[i][j].obstacle_tpye == 2) {
					TransparentBlt(hdc, board[i][j].pos.x - 25, board[i][j].pos.y - 25, 50, 50, TrashboxDC, 0, 0, 64, 64, RGB(255, 0, 255));
				}
                else if (board[i][j].obstacle_tpye == 3) {

                }
				
            }
            else if (board[i][j].obstacle == 3) {
				
				TransparentBlt(hdc, board[i][j].pos.x - MAP_TILES, board[i][j].pos.y - 120, 80,140, building1DC, 0, 0, 68, 149, RGB(255, 0, 255));
				
            }
            else if (board[i][j].obstacle == 4) {
               
                TransparentBlt(hdc, board[i][j].pos.x - MAP_TILES, board[i][j].pos.y - 140, 80, 160, building2DC, 0, 0, 68, 166, RGB(255, 0, 255));
                
            }
            else if (board[i][j].obstacle == 5) {
                TransparentBlt(hdc, board[i][j].pos.x - MAP_TILES, board[i][j].pos.y - 120, 80, 140, building3DC, 0, 0, 68, 109, RGB(255, 0, 255));

            }
            else if (board[i][j].obstacle == 6) {
                if (train.is_spawned) {
                    SelectObject(BlinkerDC, BlinkerBitmap);
                    TransparentBlt(hdc, board[i][j].pos.x - MAP_TILES, board[i][j].pos.y - 90, 40, 110, BlinkerDC, 0, 0, 40, 110, RGB(255, 0, 255));
                }
                else {
                    SelectObject(BlinkerDC, BlinkerBitmapON);
                    TransparentBlt(hdc, board[i][j].pos.x - MAP_TILES, board[i][j].pos.y - 90, 40, 110, BlinkerDC, 0, 0, 40, 110, RGB(255, 0, 255));
                }
            }
            if(i==10&&j==0) draw_train(hdc, train);
            if (board[i][j].item >= 1 && board[i][j].obstacle == 0) {
                oldItemBitmap = (HBITMAP)SelectObject(itemDC, itemBitmap[board[i][j].item - 1]);
				if (board[i][j].item == 4) // 시계 아이템
					TransparentBlt(hdc, board[i][j].pos.x - MAP_TILES, board[i][j].pos.y - MAP_TILES - 10, 2 * MAP_TILES, 2 * MAP_TILES, itemDC, 0, 0, 40, 46, RGB(255, 0, 255));
				else
                TransparentBlt(hdc, board[i][j].pos.x - MAP_TILES, board[i][j].pos.y - MAP_TILES - 10, 2 * MAP_TILES, 2 * MAP_TILES, itemDC, 0, 0, 38, 38, RGB(255, 0, 255));
                SelectObject(itemDC, oldItemBitmap);
            }
            // 플레이어 1을 그리기
            if (j == p1->GetX() && i == p1->GetY()) {
                p1->Draw_Player(hdc, p1, p1->GetSelectedCharacter());
            }

            // 플레이어 2를 그리기
            if (j == p2->GetX() && i == p2->GetY()) {
                p2->Draw_Player(hdc, p2, p2->GetSelectedCharacter());
            }
        }
    }
    for (int i = 0;i < 6;++i) {
       
      
        if (train.item[i].is_active) {
            SelectObject(itemDC, itemBitmap[train.item[i].type-1]);
            if (train.item[i].type == 4) // 시계 아이템
                TransparentBlt(hdc, train.item[i].pos.x - MAP_TILES, train.item[i].pos.y - MAP_TILES - 10, 2 * MAP_TILES, 2 * MAP_TILES, itemDC, 0, 0, 40, 46, RGB(255, 0, 255));
            else
                TransparentBlt(hdc, train.item[i].pos.x - MAP_TILES, train.item[i].pos.y - MAP_TILES, 2 * MAP_TILES, 2 * MAP_TILES, itemDC, 0, 0, 38, 38, RGB(255, 0, 255));
        }
    }
    SelectObject(building1DC, oldbuilding1Bitmap);
    DeleteDC(building1DC);
    SelectObject(building2DC, oldbuilding2Bitmap);
    DeleteDC(building2DC);
	SelectObject(building3DC, oldbuilding3Bitmap);
	DeleteDC(building3DC);
	SelectObject(obstacleDC, oldObstacleBitmap);
	DeleteDC(obstacleDC);
	SelectObject(TrashboxDC, oldTrashboxBitmap);
	DeleteDC(TrashboxDC);
	SelectObject(BlinkerDC, oldBlinkerBitmap);
	SelectObject(BlinkerDC, oldBlinkerBitmapON);
    DeleteDC(BlinkerDC);
	SelectObject(itemDC, oldItemBitmap);
	DeleteDC(itemDC);
	// 3. 클리핑 영역 복원
	SelectClipRgn(hdc, NULL); // 클리핑 영역 제거
	// 4. DC 상태 복원


	
    // 3. DC 상태 복원
    RestoreDC(hdc, -1); // SaveDC로 저장했던 상태를 복원
}

void DrawGrid_Ice(HDC hdc, HINSTANCE g_hInst, Board board[MAP_TILES][MAP_TILES], Player* p1, Player* p2)
{
    int mapSize = min(WINDOW_WIDTH, WINDOW_HEIGHT);
    int tile = mapSize / MAP_TILES / 2;
    int ox = (WINDOW_WIDTH - mapSize) / 2;
    int oy = (WINDOW_HEIGHT - mapSize) / 2;

    
    HBITMAP IceBlockBitmap[2];
    IceBlockBitmap[0] = ResourceManager::GetBitmap(BitmapID::IceBlock1);
	IceBlockBitmap[1] = ResourceManager::GetBitmap(BitmapID::IceBlock2);
    HDC IceBlockDC = CreateCompatibleDC(hdc);
    HBITMAP oldIceBlockBitmap = (HBITMAP)SelectObject(IceBlockDC, IceBlockBitmap[0]);

	HBITMAP obstacleBitmap;
	obstacleBitmap = ResourceManager::GetBitmap(BitmapID::IceObstacles);
	HDC obstacleDC = CreateCompatibleDC(hdc);
	HBITMAP oldObstacleBitmap = (HBITMAP)SelectObject(obstacleDC, obstacleBitmap);

    HBITMAP itemBitmap[3];
    itemBitmap[0] = ResourceManager::GetBitmap(BitmapID::ItemPower);
    itemBitmap[1] = ResourceManager::GetBitmap(BitmapID::ItemBombCount);
    itemBitmap[2] = ResourceManager::GetBitmap(BitmapID::ItemSpeed);
    HDC itemDC = CreateCompatibleDC(hdc);
    HBITMAP oldItemBitmap = (HBITMAP)SelectObject(itemDC, itemBitmap[0]);
    //얼음집
    TransparentBlt(hdc, board[1][5].pos.x - MAP_TILES, board[2][5].pos.y - MAP_TILES, 120, 120, obstacleDC, 160, 0, 120, 120, RGB(255, 0, 255));
    //얼음집
    TransparentBlt(hdc, board[1][12].pos.x - MAP_TILES, board[2][5].pos.y - MAP_TILES, 120, 120, obstacleDC, 160, 0, 120, 120, RGB(255, 0, 255));

    for (int i = 0; i < MAP_TILES; ++i) {
        for (int j = 0; j < MAP_TILES; ++j) {
           
            if (board[i][j].item >= 1 && board[i][j].obstacle == 0) {
                oldItemBitmap = (HBITMAP)SelectObject(itemDC, itemBitmap[board[i][j].item - 1]);
                TransparentBlt(hdc, board[i][j].pos.x - MAP_TILES, board[i][j].pos.y - MAP_TILES - 10, 2 * MAP_TILES, 2 * MAP_TILES, itemDC, 0, 0, 38, 38, RGB(255, 0, 255));
                SelectObject(itemDC, oldItemBitmap);
            }
            if (j == p1->GetX() && i == p1->GetY()) {


                p1->Draw_Player(hdc, p1, p1->GetSelectedCharacter());
            }
            if (j == p2->GetX() && i == p2->GetY()) {
                p2->Draw_Player(hdc, p2, p2->GetSelectedCharacter());
            }
            if (board[i][j].obstacle == 1) {
                if (board[i][j].obstacle_tpye == 0) {
                    (HBITMAP)SelectObject(IceBlockDC, IceBlockBitmap[board[i][j].obstacle_tpye]);
                    TransparentBlt(hdc, board[i][j].pos.x - MAP_TILES, board[i][j].pos.y - MAP_TILES-25, 2 * MAP_TILES, 2 * MAP_TILES + 30, IceBlockDC, 0, 0, 42, 54, RGB(255, 0, 255));
                    SelectObject(IceBlockDC, oldIceBlockBitmap);
                }
                else  if (board[i][j].obstacle_tpye == 1) {
                    (HBITMAP)SelectObject(IceBlockDC, IceBlockBitmap[board[i][j].obstacle_tpye]);
                    TransparentBlt(hdc, board[i][j].pos.x - MAP_TILES , board[i][j].pos.y - MAP_TILES -15, 2 * MAP_TILES , 2 * MAP_TILES + 20, IceBlockDC, 0, 0, 42, 48, RGB(255, 0, 255));
                    SelectObject(IceBlockDC, oldIceBlockBitmap);
                }
                
            }
           




            if (board[i][j].obstacle == 1) {

             
            }
           


        }

    }

	SelectObject(itemDC, oldItemBitmap);
    DeleteDC(itemDC);
	SelectObject(obstacleDC, oldObstacleBitmap);
    DeleteDC(obstacleDC);
    SelectObject(IceBlockDC, oldIceBlockBitmap);
	DeleteDC(IceBlockDC);
 
 

}

void RGBtoHSV(BYTE r, BYTE g, BYTE b, double& h, double& s, double& v) {
    double R = r / 255.0, G = g / 255.0, B = b / 255.0;
    double maxc = max(R, max(G, B));
    double minc = min(R, min(G, B));
    v = maxc;

    double delta = maxc - minc;
    if (delta == 0) {
        h = 0;
        s = 0;
        return;
    }

    s = delta / maxc;

    if (maxc == R)
        h = fmod((G - B) / delta, 6.0);
    else if (maxc == G)
        h = ((B - R) / delta) + 2;
    else
        h = ((R - G) / delta) + 4;

    h *= 60;
    if (h < 0) h += 360;
}
void HSVtoRGB(double h, double s, double v, BYTE& r, BYTE& g, BYTE& b) {
    double C = v * s;
    double X = C * (1 - fabs(fmod(h / 60.0, 2) - 1));
    double m = v - C;

    double r1, g1, b1;

    if (h < 60) { r1 = C; g1 = X; b1 = 0; }
    else if (h < 120) { r1 = X; g1 = C; b1 = 0; }
    else if (h < 180) { r1 = 0; g1 = C; b1 = X; }
    else if (h < 240) { r1 = 0; g1 = X; b1 = C; }
    else if (h < 300) { r1 = X; g1 = 0; b1 = C; }
    else { r1 = C; g1 = 0; b1 = X; }

    r = BYTE((r1 + m) * 255);
    g = BYTE((g1 + m) * 255);
    b = BYTE((b1 + m) * 255);
}
bool IsInColorRange(BYTE r, BYTE g, BYTE b, int hueMin, int hueMax) {
    double h, s, v;
    RGBtoHSV(r, g, b, h, s, v);
    return (h >= hueMin && h <= hueMax);
}

HBITMAP AdjustBitmapHueWithFilter(HBITMAP hBitmap, double hueShift, int hueFilterMin, int hueFilterMax) {
    BITMAP bmp;
    GetObject(hBitmap, sizeof(BITMAP), &bmp);
    int colorHueTable[] = {
    0,   // 빨강
    30,  // 주황
    60,  // 노랑
    120, // 초록
    150, // 민트
    210, // 파랑
    270, // 보라
    
    };
    hueShift = colorHueTable[(int)hueShift];
   
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = bmp.bmWidth;
    bmi.bmiHeader.biHeight = -bmp.bmHeight;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HDC hdc = GetDC(NULL);
    HBITMAP newBitmap = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pixels, NULL, 0);
    ReleaseDC(NULL, hdc);

    if (!newBitmap || !pixels) return NULL;

    HDC srcDC = CreateCompatibleDC(NULL);
    HDC dstDC = CreateCompatibleDC(NULL);
    SelectObject(srcDC, hBitmap);
    SelectObject(dstDC, newBitmap);
    BitBlt(dstDC, 0, 0, bmp.bmWidth, bmp.bmHeight, srcDC, 0, 0, SRCCOPY);

    BYTE* bytePixels = (BYTE*)pixels;
    for (int y = 0; y < bmp.bmHeight; ++y) {
        for (int x = 0; x < bmp.bmWidth; ++x) {
            int idx = (y * bmp.bmWidth + x) * 3;
            BYTE& B = bytePixels[idx];
            BYTE& G = bytePixels[idx + 1];
            BYTE& R = bytePixels[idx + 2];

            // 마젠타 배경은 스킵
            if (R == 255 && G == 0 && B == 255) continue;

            if (IsInColorRange(R, G, B, hueFilterMin, hueFilterMax)) {
                double h, s, v;
                RGBtoHSV(R, G, B, h, s, v);
                h = fmod(h + hueShift, 360.0);
                if (h < 0) h += 360.0;
                HSVtoRGB(h, s, v, R, G, B);
            }
        }
    }

    DeleteDC(srcDC);
    DeleteDC(dstDC);
    return newBitmap;
}
