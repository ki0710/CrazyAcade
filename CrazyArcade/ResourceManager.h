#pragma once
#include <windows.h>
#include <map>

enum class BitmapID {
    //폭탄관련
    BombSet,
    BombCenter,
    BombPop,
    BombDown2,
    BombUp2,
    BombLeft2,
    BombRight2,
    //숲맵관련
    ForestTile1,
    ForestTile2,
    ForestTile3,
    ForestTile4,
    ForestTile5,
    ForestTile6,
    ForestTileMax,
    ForestObstacle1,
    ForestObstacle2,
    ForestObstacle3,
    ForestObstacle4,
    ForestObstacle5,
    ForestTree,
    ForestApple,
    ForestAppleBox,
    //도시
	CityTileMax,
    CityRail,
	CityTrain,
    CityBuilding1,
    CityBuilding2,
    CityBuilding3,
	CityObstacles,
    CityTrashbox,
    CityBlinkerOFF,
	CityBlinkerON,
    CityClock,
    //얼음맵
	IceTileMax,
    IceBlock1,
	IceBlock2,
	IceObstacles,
    Heart,
    
    //플레이어
    Player1Moving,
    Player2Moving,
    PlayerMovingMask,
    //아이템
	ItemPower,
	ItemBombCount,
	ItemSpeed,
    // 필요한 리소스 계속 추가
    Game_end,
    Count   //마지막 값
};

class ResourceManager {
public:
    static void LoadAll(HINSTANCE hInst);
    static void UnloadAll();
    static HBITMAP GetBitmap(BitmapID id);
 
   static  void SetBitmap(BitmapID id, HBITMAP hBitmap);


private:
    static std::map<BitmapID, HBITMAP> bitmapMap;
  
};

