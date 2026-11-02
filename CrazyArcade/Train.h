#pragma once
#include"pch.h"
#include"Board.h"

typedef struct FloatingItem {
	POINT pos;
	POINT on_board_pos; // 보드 상의 위치
	int type; // 1: 물풍선 사거리, 2: 물풍선 개수, 3: 이동속도 4:시계
	bool is_active;
} FloatingItem;;
typedef struct Train {
	RECT Pos;
	bool collisionP1;
	bool collisionP2;
	int speed;
	int spawnTime;
	int spawnInterval;
	bool is_spawned; // 기차가 생성되었는지 여부
	FloatingItem item[6]; // 기차에서 생성되는 아이템
} Train;

void init_train(Train& train);
int move_train(Train& train);
void draw_train(HDC hdc, Train& train);
void make_item(Train& train, Board board[MAP_TILES][MAP_TILES]);
void move_item(Train& train, Board board[MAP_TILES][MAP_TILES]);
