#pragma once
#include "pch.h"
#include "Board.h"

// 논리 칸 좌표를 기존 화면 배치에 맞는 픽셀 좌표로 변환한다.
POINT GetBoardScreenPosition(int x, int y);
void draw_tile_forest(HDC hdc);
void draw_tile_city(HDC hdc);
void draw_tile_ice(HDC hdc);
void DrawDarkOverlay(HDC hDC, int width, int height);
