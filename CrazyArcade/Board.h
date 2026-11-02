#pragma once

#include"pch.h"



typedef struct Board {
	POINT pos;
	int obstacle;
	int obstacle_tpye;
	int tile;
	int item;	// 0없음 1 물풍선사거리 2물풍선 개수 3 이동속도

}Board;
#define MAP_TILES      20
void init_board_Forest(Board board[MAP_TILES][MAP_TILES]);
void init_board_City(Board board[MAP_TILES][MAP_TILES]);
void init_board_Ice(Board board[MAP_TILES][MAP_TILES]);
void draw_tile_forest(HDC hdc);
void draw_tile_city(HDC hdc);
void draw_tile_ice(HDC hdc);
void DrawDarkOverlay(HDC hDC, int width, int height);