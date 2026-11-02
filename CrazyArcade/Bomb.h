#pragma once
#include"pch.h"
#include"Player.h"


class Bomb {
	POINT pos;
	POINT logicalPos;
	int time;
	int state;		//0이면 아직 설정 안된 상태 1이면 설치된 상태 2면 터진 상태
	int reach;
	int reachLeft;
	int reachRight;
	int reachUp;
	int reachDown;
	bool isCollisionLeft;
	bool isCollisionRight;
	bool isCollisionUp;
	bool isCollisionDown;

	bool is_collision_player;
public:
	void make_Bomb(Player player,Board board[MAP_TILES][MAP_TILES]);
	bool tick_Bomb();
	int collision_bomb_player(Bomb bomb, Player player);

	int GetX() const { return pos.x; }
	int GetY() const { return pos.y; }
	int GetTime() const { return time; }
	int GetState()const { return state; }
	int GetReach()const { return reach; }
	int GetReachLeft() const { return reachLeft; }
	int GetReachRight() const { return reachRight; }
	int GetReachUp() const { return reachUp; }
	int GetReachDown() const { return reachDown; }
	int GetLogicalX()const { return logicalPos.x; }
	int GetLogicalY()const { return logicalPos.y; }
	bool GetIsCollisionLeft() const { return isCollisionLeft; }
	bool GetIsCollisionRight() const { return isCollisionRight; }
	bool GetIsCollisionUp() const { return isCollisionUp; }
	bool GetIsCollisionDown() const { return isCollisionDown; }
	bool GetIsCollisionPlayer() const { return is_collision_player; }

	void SetReachLeft(int value) { reachLeft = value; }
	void SetReachRight(int value) { reachRight = value; }
	void SetReachUp(int value) { reachUp = value; }
	void SetReachDown(int value) { reachDown = value; }
	void SetIsCollisionLeft(bool value) { isCollisionLeft = value; }
	void SetIsCollisionRight(bool value) { isCollisionRight = value; }
	void SetIsCollisionUp(bool value) { isCollisionUp = value; }
	void SetIsCollisionDown(bool value) { isCollisionDown = value; }
	void SetIsCollisionPlayer(bool value) { is_collision_player = value; }

	
};
void draw_Bomb(HDC hDC, HINSTANCE g_hInst, Bomb bombs[], int n, Board board[MAP_TILES][MAP_TILES], Player* player);