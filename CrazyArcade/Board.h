#pragma once

// 서버에서 사용할 맵 상태에는 화면 좌표와 Windows 자료형을 넣지 않는다.



typedef struct Board {
	int obstacle;
	int obstacle_tpye;
	int tile;
	int item;	// 0없음 1 물풍선사거리 2물풍선 개수 3 이동속도

}Board;
#define MAP_TILES      20
void init_board_Forest(Board board[MAP_TILES][MAP_TILES]);
void init_board_City(Board board[MAP_TILES][MAP_TILES]);
void init_board_Ice(Board board[MAP_TILES][MAP_TILES]);
