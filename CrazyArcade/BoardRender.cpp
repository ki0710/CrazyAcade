#include "BoardRender.h"
#include "ResourceManager.h"

POINT GetBoardScreenPosition(int x, int y)
{
    int mapSize = min(WINDOW_WIDTH, WINDOW_HEIGHT);
    int tile = mapSize / MAP_TILES / 2;
    int ox = (WINDOW_WIDTH - mapSize) / 2;
    return {85 + ox + tile + x * tile * 2, 2 * tile + y * tile * 2};
}

void draw_tile_forest(HDC hdc)
{
    
    HBITMAP backgroundBitmap = ResourceManager::GetBitmap(BitmapID::ForestTileMax);
    HDC backgroundDC = CreateCompatibleDC(hdc);
    HBITMAP oldBackgroundBitmap = (HBITMAP)SelectObject(backgroundDC, backgroundBitmap);
    int mapSize = min(WINDOW_WIDTH, WINDOW_HEIGHT);
    int tile = mapSize / MAP_TILES / 2;
    int ox = (WINDOW_WIDTH - mapSize) / 2;



    StretchBlt(hdc, 85 + ox + tile - 20, 2 * tile - 20, 800, 800, backgroundDC, 0, 0, 600, 520, SRCCOPY);
    SelectObject(backgroundDC, oldBackgroundBitmap);
    
    DeleteDC(backgroundDC);
    
}

void draw_tile_city(HDC hdc)
{
    HBITMAP backgroundBitmap = ResourceManager::GetBitmap(BitmapID::CityTileMax);
    HDC backgroundDC = CreateCompatibleDC(hdc);
    HBITMAP oldBackgroundBitmap = (HBITMAP)SelectObject(backgroundDC, backgroundBitmap);
    int mapSize = min(WINDOW_WIDTH, WINDOW_HEIGHT);
    int tile = mapSize / MAP_TILES / 2;
    int ox = (WINDOW_WIDTH - mapSize) / 2;



    StretchBlt(hdc, 85 + ox + tile - 20, 2 * tile - 20, 800, 803, backgroundDC, 0, 3, 1400, 1400, SRCCOPY);
   
    

    SelectObject(backgroundDC, oldBackgroundBitmap);
    DeleteDC(backgroundDC);
  
   
}

void draw_tile_ice(HDC hdc)
{
    HBITMAP backgroundBitmap = ResourceManager::GetBitmap(BitmapID::IceTileMax);
    HDC backgroundDC = CreateCompatibleDC(hdc);
    HBITMAP oldBackgroundBitmap = (HBITMAP)SelectObject(backgroundDC, backgroundBitmap);
    int mapSize = min(WINDOW_WIDTH, WINDOW_HEIGHT);
    int tile = mapSize / MAP_TILES / 2;
    int ox = (WINDOW_WIDTH - mapSize) / 2;



    StretchBlt(hdc, 85 + ox + tile - 20, 2 * tile - 20, 800, 800, backgroundDC, 0, 0, 1400, 1400, SRCCOPY);
    SelectObject(backgroundDC, oldBackgroundBitmap);

    DeleteDC(backgroundDC);
}


void DrawDarkOverlay(HDC hDC, int width, int height) {
    // 1. 반투명 블렌딩 설정
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 128; // 투명도 조절 (0=완전투명, 255=완전불투명)

    // 2. 호환 DC 및 비트맵 생성
    HDC hMemDC = CreateCompatibleDC(hDC);
    HBITMAP hBitmap = CreateCompatibleBitmap(hDC, width, height);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemDC, hBitmap);

    // 3. 검은색으로 채우기
    HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));
    RECT rect = { 0, 0, width, height };
    FillRect(hMemDC, &rect, hBrush);

    // 4. 원본 DC에 반투명하게 출력
    AlphaBlend(hDC, 0, 0, width, height, hMemDC, 0, 0, width, height, blend);

    // 5. 리소스 해제
    SelectObject(hMemDC, hOldBitmap);
    DeleteObject(hBrush);
    DeleteObject(hBitmap);
    DeleteDC(hMemDC);
}