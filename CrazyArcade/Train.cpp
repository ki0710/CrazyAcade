#include"Train.h"
#include"ResourceManager.h"

void init_train(Train& train) {
	train.Pos.left = -690;
	train.Pos.top = 320;
	train.Pos.right = train.Pos.left + 1029; // 기차 폭
	train.Pos.bottom = train.Pos.top + 101; // 기차 높이

	train.collisionP1 = false;
	train.collisionP2 = false;
	train.speed = 5; // 기차 속도
	train.spawnTime = 0; // 기차 생성 시간
	train.spawnInterval = 1000; // 기차 생성 간격 (10초)
	train.is_spawned = false; // 기차가 생성되었는지 여부
}
int move_train(Train& train) {
	if (train.is_spawned) {
		train.Pos.left += train.speed;
		train.Pos.right += train.speed;
		
		if (train.Pos.left > 1125) {

			train.Pos.left = -690;
			train.Pos.right = train.Pos.left + 1029; // 기차 폭
			train.is_spawned = false; // 기차가 다시 생성되지 않도록 설정
			train.collisionP1 = false; // 충돌 상태 초기화
			train.collisionP2 = false; // 충돌 상태 초기화
		}
		if (train.Pos.left == 200) {
			return 1;
		}
	}
	else {
		train.spawnTime += 5;
		if (train.spawnTime >= train.spawnInterval) {
			train.is_spawned = true;
			train.spawnTime = 0; // 리셋
			train.collisionP1 = false; // 충돌 상태 초기화
			train.collisionP2 = false; // 충돌 상태 초기화
		}
	}
	
	return 0;
}

void draw_train(HDC hdc, Train& train)
{
	if (train.is_spawned) {
		HBITMAP trainBitmap = ResourceManager::GetBitmap(BitmapID::CityTrain);
		HDC trainDC = CreateCompatibleDC(hdc);
		HBITMAP oldTrainBitmap = (HBITMAP)SelectObject(trainDC, trainBitmap);
		TransparentBlt(hdc, train.Pos.left, train.Pos.top,
			train.Pos.right - train.Pos.left, train.Pos.bottom - train.Pos.top,
			trainDC, 0, 0,
			1029, 101,
			RGB(255, 0, 255));
		SelectObject(trainDC, oldTrainBitmap);
		DeleteDC(trainDC);
	}
}


void make_item(Train& train, Board board[MAP_TILES][MAP_TILES])
{
	if (train.is_spawned) {
		for (int i = 0;i < 6;++i) {
			int itemType = rand() % 4 + 1;
			while (1) {
				int posX = rand() % MAP_TILES;
				int posY = rand() % MAP_TILES;
				if (board[posY][posX].obstacle == 0 && board[posY][posX].item == 0) {
					train.item[i].on_board_pos.x = posX;
					train.item[i].on_board_pos.y = posY;
					train.item[i].pos.x = train.Pos.right - train.Pos.left-250;
					train.item[i].pos.y = train.Pos.bottom-40;
					train.item[i].type = itemType;
					train.item[i].is_active = true;
					
					break;
				}
			}
		}
	}
	
}

void move_item(Train& train, Board board[MAP_TILES][MAP_TILES])
{
	for (int i = 0;i < 6;++i) {
		if (train.item[i].is_active) {
			POINT& cur = train.item[i].pos;
			POINT target = board[train.item[i].on_board_pos.y][train.item[i].on_board_pos.x].pos;

			// 방향 벡터 계산
			int dx = target.x - cur.x;
			int dy = target.y - cur.y;

			
			float distance = sqrtf(dx * dx + dy * dy);

			if (distance < 10.0f) {
				board[train.item[i].on_board_pos.y][train.item[i].on_board_pos.x].item = train.item[i].type; // 아이템을 보드에 추가
				train.item[i].is_active = false; // 아이템 비활성화
			}
			else {
				// 방향 벡터 정규화 후 10픽셀 이동
				float nx = dx / distance;
				float ny = dy / distance;

				train.item[i].pos.x += (int)(nx * 10);
				train.item[i].pos.y += (int)(ny * 10);
			}
		}
	}
}

