#pragma once

#include "pch.h"
#include"Train.h"


#include"Board.h"
static bool keyStates[256] = { 0 };
class Player {
    int x, y;
    int destX, destY;
    int dir;
	float speed;
    float px, py;
    int bomb_distance;
    int maxBomb;
    int currrentApple;
	int selected_character;
    int move_cnt;
    int color;
    int player_num;
    bool is_hit;
    HBITMAP hSprite;
public:
    Player();
    ~Player();
    Player get_player(Player player);
   
    void Init_Player(POINT pos);

    void Draw_Player(HDC hdc, Player* p, int character);

    void on_applebox(Player* player, int* applebox, int boxX, int boxY);
    int collision_item(Player* player, Board board[MAP_TILES][MAP_TILES],int& Pcurrenttime);
	int collision_train(Player* player, Train& train, Board board[MAP_TILES][MAP_TILES], int& Pcurrenttime);

public:
    // Getter
    int GetX() const { return x; }
    int GetY() const { return y; }
    int GetDestX() const { return destX; }
    int GetDestY() const { return destY; }
	float Getspeed() const { return speed; }    
    int GetDir() const { return dir; }
    float GetPx() const { return px; }
    float GetPy() const { return py; }
    int GetBomb_distance()const { return bomb_distance; }
    int GetMaxBomb()const { return maxBomb; }
	int GetCurrentApple() const { return currrentApple; }
	int GetSelectedCharacter() const { return selected_character; }
	int GetColor() const { return color; }
	int GetMoveCount() const { return move_cnt; }
	int GetPlayerNum() const { return player_num; }
	bool GetIsHit() const { return is_hit; }
    // Setter
    void SetX(int value) { x = value; }
    void SetY(int value) { y = value; }
    void SetDestX(int value) { destX = value; }
    void SetDestY(int value) { destY = value; }
	void SetSpeed(float value) { speed = value; }
    void SetDir(int value) { dir = value; }
    void SetPx(float value) { px = value; }
    void SetPy(float value) { py = value; }
    void SetBomb_distance(int value) { bomb_distance = value; }
    void SetMaxBomb(int value) { maxBomb = value; }
	void SetCurrentApple(int value) { currrentApple = value; }
	void SetSelectedCharacter(int value) { selected_character = value; }
	void SetColor(int value) { color = value; }
	void SetMoveCount(int value) { move_cnt = value; }
	void SetPlayerNum(int value) { player_num = value; }
	void SetIsHit(bool value) { is_hit = value; }
    // 위치 비교
    bool IsStopped() const { return (px == x && py == y); }
    bool IsMoving() const { return (px != destX || py != destY); }

 
    void SnapToDest() {
        x = destX;
        y = destY;
        px = destX;
        py = destY;
    }
};

   //이동 관리 함수
void HandlePlayerMovement(Player& player, const bool* keyStates, int upKey, int downKey, int leftKey, int rightKey, Board board[MAP_TILES][MAP_TILES]);
void UpdatePlayerPosition(Player& player, float speed);

//맵 그리기 플레이어 연동
void DrawGrid_Forest(HDC hdc, HINSTANCE g_hInst, Board board[MAP_TILES][MAP_TILES], Player* p1, Player* p2);
void DrawGrid_City(HDC hdc, HINSTANCE g_hInst, Board board[MAP_TILES][MAP_TILES], Player* p1, Player* p2,Train train);  
void DrawGrid_Ice(HDC hdc, HINSTANCE g_hInst, Board board[MAP_TILES][MAP_TILES], Player* p1, Player* p2);

//플레이어 색상 변경을 위해 필요한 함수들

void RGBtoHSV(BYTE r, BYTE g, BYTE b, double& h, double& s, double& v);
// HSV → RGB
void HSVtoRGB(double h, double s, double v, BYTE& r, BYTE& g, BYTE& b);
bool IsInColorRange(BYTE r, BYTE g, BYTE b, int hueMin, int hueMax);

HBITMAP AdjustBitmapHueWithFilter(HBITMAP hBitmap, double hueShift, int hueFilterMin, int hueFilterMax);