#include "ResourceManager.h"
#include "resource2.h" // 리소스 ID 정의된 헤더 포함

std::map<BitmapID, HBITMAP> ResourceManager::bitmapMap;


void ResourceManager::LoadAll(HINSTANCE hInst) {
    bitmapMap[BitmapID::BombSet] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_BOMB_CENTER));
    bitmapMap[BitmapID::BombCenter] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_BOMB_POP));
    bitmapMap[BitmapID::BombPop] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_BOMB_POP));
    bitmapMap[BitmapID::BombDown2] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_BOMB_DOWN2));
    bitmapMap[BitmapID::BombUp2] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_BOMB_UP2));
    bitmapMap[BitmapID::BombLeft2] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_BOMB_LEFT2));
    bitmapMap[BitmapID::BombRight2] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_BOMB_RIGHT2));
    //숲맵
    bitmapMap[BitmapID::ForestTile1] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_GRASS_1));
    bitmapMap[BitmapID::ForestTile2] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_GRASS_2));
    bitmapMap[BitmapID::ForestTile3] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_GRASS_3));
    bitmapMap[BitmapID::ForestTile4] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_GRASS_4));
    bitmapMap[BitmapID::ForestTile5] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_GRASS_5));
    bitmapMap[BitmapID::ForestTile6] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_GRASS_6));
	bitmapMap[BitmapID::ForestTileMax] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_FOREST_TILE_MAX));
    bitmapMap[BitmapID::ForestObstacle1] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_FOREST_OBSTACLE_1));
    bitmapMap[BitmapID::ForestObstacle2] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_FOREST_OBSTACLE_2));
    bitmapMap[BitmapID::ForestObstacle3] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_FOREST_OBSTACLE_3));
    bitmapMap[BitmapID::ForestObstacle4] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_FOREST_OBSTACLE_4));
    bitmapMap[BitmapID::ForestObstacle5] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_FOREST_OBSTACLE_5));
    bitmapMap[BitmapID::ForestTree] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_BIG_TREE));
    bitmapMap[BitmapID::Player1Moving] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_PLAYER));
    bitmapMap[BitmapID::Player2Moving] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_PLAYER));
    bitmapMap[BitmapID::PlayerMovingMask] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_PLAYER_MASK));
    bitmapMap[BitmapID::ForestApple] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_Forest_APPLE));
    bitmapMap[BitmapID::ForestAppleBox] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_FOREST_APPLEBOX));
    bitmapMap[BitmapID::Game_end] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_GAME_END));
    bitmapMap[BitmapID::ItemPower] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_ITEM_POWER));
    bitmapMap[BitmapID::ItemBombCount] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_ITEM_BOMBS));
    bitmapMap[BitmapID::ItemSpeed] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_ITEM_SPEED));
    //도시맵
	bitmapMap[BitmapID::CityTileMax] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_TILE_CITY));
	bitmapMap[BitmapID::CityRail] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_CITY_RAIL));
	bitmapMap[BitmapID::CityTrain] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_CITY_TRAIN));
	bitmapMap[BitmapID::CityBuilding1] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_CITY_BUILDING1));
	bitmapMap[BitmapID::CityBuilding2] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_CITY_BUILDING2));
	bitmapMap[BitmapID::CityBuilding3] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_CITY_BUILDING3));
	bitmapMap[BitmapID::CityObstacles] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_CITY_OBJECTS));
	bitmapMap[BitmapID::CityTrashbox] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_CITY_TRASHBOX));
	bitmapMap[BitmapID::CityBlinkerON] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_BLINKER_ON));
	bitmapMap[BitmapID::CityBlinkerOFF] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_CITY_BLINKER));
	bitmapMap[BitmapID::CityClock] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_CITY_CLOCK));
    //얼음맵
	bitmapMap[BitmapID::IceTileMax] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_ICE_TILE_MAX));
	bitmapMap[BitmapID::IceBlock1] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_ICE_BLOCK1));
	bitmapMap[BitmapID::IceBlock2] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_ICE_BLOCK2));
	bitmapMap[BitmapID::IceObstacles] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_ICE_OBSTACLES));
	bitmapMap[BitmapID::Heart] = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_HEART));

    // 여기에 추가 비트맵도 계속 등록

}

void ResourceManager::UnloadAll() {
    for (auto& pair : bitmapMap)
        DeleteObject(pair.second);
    bitmapMap.clear();

   
}
HBITMAP ResourceManager::GetBitmap(BitmapID id) {
    return bitmapMap[id];
}

void ResourceManager::SetBitmap(BitmapID id, HBITMAP hBitmap) {
    // 기존 비트맵 있으면 삭제
    if (bitmapMap[id]) {
        DeleteObject(bitmapMap[id]);
    }
    bitmapMap[id] = hBitmap;
}

