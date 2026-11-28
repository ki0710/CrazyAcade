#include"pch.h"
#include "BoardRender.h"
#include "IceState.h"
#include"Train.h"
#include"Bomb.h"
#include"ResourceManager.h"
#include "OnlineConnection.h"
#include "../NetworkTest/Common/MapItemSync.h"
#include "../NetworkTest/Common/PlayerMovement.h"
#include "../NetworkTest/Common/BombSync.h"
#include <memory>
#include <sstream>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#include <shlwapi.h>  // PathFileExists를 사용하기 위해 추가

#pragma comment(lib, "shlwapi.lib")  // shlwapi 라이브러리 링크
HINSTANCE g_hInst;
LPCTSTR lpszClass = L"Window Class Name";
LPCTSTR lpszWindowName = L"CrazyArcade";
// Windows input stays local; movement code consumes only the selected direction.
static int GetMovementDirection(const bool* keys, int upKey, int downKey, int leftKey, int rightKey) {
    if (keys[upKey]) return DIR_UP;
    if (keys[downKey]) return DIR_DOWN;
    if (keys[leftKey]) return DIR_LEFT;
    if (keys[rightKey]) return DIR_RIGHT;
    return DIR_NONE;
}

static bool IsWasdKey(WPARAM key) {
    return key == 'W' || key == 'S' || key == 'A' || key == 'D';
}

static bool IsMovementKey(WPARAM key) {
    return IsWasdKey(key) || key == VK_UP || key == VK_DOWN || key == VK_LEFT || key == VK_RIGHT;
}

static std::unique_ptr<OnlineConnection> onlineConnection;
constexpr UINT_PTR kOnlineTimer = 3;

static std::uint8_t MovementMask(int dir) {
    switch (dir) {
    case DIR_UP: return network::kInputUp;
    case DIR_DOWN: return network::kInputDown;
    case DIR_LEFT: return network::kInputLeft;
    case DIR_RIGHT: return network::kInputRight;
    default: return network::kInputNone;
    }
}

static void ApplyOnlineAbilities(Player& player, const ItemAbilities& abilities) {
    player.SetSpeed(abilities.speed);
    player.SetBomb_distance(abilities.blastRange);
    player.SetMaxBomb(abilities.bombLimit);
}

static void InitOnlinePlayer(Player& player, int id, int stage, int color, const ItemAbilities& abilities) {
    const auto start = PlayerStartCell(stage, id);
    player.Init_Player({start.x, start.y});
    player.SetPlayerNum(id);
    player.SetColor(color);
    ApplyOnlineAbilities(player, abilities);
    if (HBITMAP original = LoadBitmap(g_hInst, MAKEINTRESOURCE(IDB_BITMAP_PLAYER))) {
        HBITMAP sprite = color ? AdjustBitmapHueWithFilter(original, color, 0, 20) : nullptr;
        if (sprite) DeleteObject(original);
        ResourceManager::SetBitmap(id == 1 ? BitmapID::Player1Moving : BitmapID::Player2Moving,
                                   sprite ? sprite : original);
    }
}

static void ApplyOnlinePosition(Player& player, const network::PlayerPosition& p) {
    const float progress = p.progress / static_cast<float>(network::kCellUnits);
    const float px = p.cell.x + (p.target.x - p.cell.x) * progress;
    const float py = p.cell.y + (p.target.y - p.cell.y) * progress;
    if (px != player.GetPx() || py != player.GetPy())
        player.SetMoveCount((player.GetMoveCount() + 1) % 16);
    player.SetX(p.cell.x);
    player.SetY(p.cell.y);
    player.SetDestX(p.target.x);
    player.SetDestY(p.target.y);
    player.SetPx(px);
    player.SetPy(py);
    int direction = DIR_DOWN;
    if (p.facing == network::kInputUp) direction = DIR_UP;
    if (p.facing == network::kInputLeft) direction = DIR_LEFT;
    if (p.facing == network::kInputRight) direction = DIR_RIGHT;
    player.SetDir(direction);
}

void DrawOutlinedText(HDC hdc, int x, int y, const wchar_t* text, bool centered = false) {
    // 폰트 생성 (크고 두꺼운 글씨)
    HFONT hFont = CreateFont(
        48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"맑은 고딕"
    );

    HFONT oldFont = (HFONT)SelectObject(hdc, hFont);
    const int length = static_cast<int>(wcslen(text));
    if (centered) {
        SIZE extent{};
        if (GetTextExtentPoint32(hdc, text, length, &extent))
            x -= extent.cx / 2;
    }

    // 배경 투명하게 설정
    SetBkMode(hdc, TRANSPARENT);

    // 외곽선(Stroke)을 위한 경로 시작
    BeginPath(hdc);
    TextOut(hdc, x, y, text, length);
    EndPath(hdc);

    // 외곽선 색
    HPEN hPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
    HPEN oldPen = (HPEN)SelectObject(hdc, hPen);

    // 안쪽 채우기 색
    HBRUSH hBrush = CreateSolidBrush(RGB(255, 165, 0));
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, hBrush);

    StrokeAndFillPath(hdc);

    // 정리
    SelectObject(hdc, oldBrush);
    DeleteObject(hBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(hPen);
    SelectObject(hdc, oldFont);
    DeleteObject(hFont);
}

static int frameCount = 0;
static DWORD lastTime = GetTickCount();  // 시작 시간(ms)
static int currentFPS = 0;
LRESULT CALLBACK WndProc(HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lParam);
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpszCmdParam, _In_ int nCmdShow) {
    // No arguments keeps the original two-player local game.
    std::istringstream args(lpszCmdParam);
    std::string mode, ip = "127.0.0.1", extra;
    if (args >> mode) {
        if (mode != "--online" || ((args >> ip) && (args >> extra))) {
            MessageBox(nullptr, L"Usage: CrazyArcade.exe [--online [server IPv4]]", L"CrazyArcade", MB_OK);
            return 1;
        }
        onlineConnection.reset(new OnlineConnection(ip));
    }
    HWND hWnd;
    MSG Message;
    WNDCLASSEX WndClass;
  

   

    g_hInst = hInstance;
    ResourceManager::LoadAll(g_hInst);
  
    WndClass.cbSize = sizeof(WndClass);
    WndClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    WndClass.lpfnWndProc = (WNDPROC)WndProc;
    WndClass.cbClsExtra = 0;
    WndClass.cbWndExtra = 0;
    WndClass.hInstance = hInstance;
    WndClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    WndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    WndClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    WndClass.lpszMenuName = NULL;
    WndClass.lpszClassName = lpszClass;
    WndClass.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    //위의 12개의 항목을 다 만들어야 윈도우가 생길거임.
    RegisterClassEx(&WndClass);//만든 윈도우를 레지스터에 등록?하는거라함

    hWnd = CreateWindow(lpszClass, lpszWindowName, WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL | CS_DBLCLKS,
        0, 0, 1500, 900, NULL, (HMENU)NULL, hInstance, NULL);
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    
   
    while (GetMessage(&Message, 0, 0, 0)) {
        TranslateMessage(&Message);
        DispatchMessage(&Message);
    }
    onlineConnection.reset();
    ResourceManager::UnloadAll();
    return Message.wParam;
  

}





LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    
    
    PAINTSTRUCT ps;
    HDC hdc;
    static Player p1, p2;
    static bool keyStates[256] = { 0 };
    static Board board[MAP_TILES][MAP_TILES];
    static int selected_stage = 0;
    static std::vector<Bomb> p1bombs;
    static std::vector<Bomb> p2bombs;
	static bool game_started = false;
    static bool game_end = false;
    static int game_winner = 0; 
    static int p1_check_color_pos;
	static int p2_check_color_pos;
	static int p1_hit_cnt = 0;
	static int p2_hit_cnt = 0;
    //스테이지 1
    static int p1Applebox = 0;
    static int p2Applebox = 0;

    //스테이지 2
	static Train train;
    static int p1Time = 0;
	static int p2Time = 0;
	static int p1currentTime = 100;
	static int p2currentTime = 100;
    //스테이지 3
    static IceState ice;
    int& p1CurrentHeart = ice.hearts[0];
    int& p2CurrentHeart = ice.hearts[1];

    //현재 아이템 개수 파악
	static int p1_current_item[3] = { 0,0,0 }; 
	static int p2_current_item[3] = { 0,0,0 }; 

    // Online mode uses this window for input and rendering, without local gameplay timers.
    static std::uint8_t onlinePlayerId = 0;
    static std::uint32_t onlineMatchId = 0, previousMatchId = 0, inputSequence = 0, playerTick = 0;
    static bool onlinePlaying = false;
    static std::uint8_t sentDirection = network::kInputNone;
    static std::vector<std::uint8_t> roomState;
    static int requestedColor = -1; // Input cursor for presses before the server responds.
    static network::MapItemState onlineMap;
    static network::BombFrame onlineBombFrame;

    if (onlineConnection) {
        using namespace network;
        const auto send = [&](PacketType type, std::uint32_t match, std::vector<std::uint8_t> payload) {
            if (onlineConnection->Send(type, match, std::move(payload))) return true;
            onlineConnection->Stop();
            return false;
        };
        const auto clearOnlineDisplay = [&] {
            p1bombs.clear();
            p2bombs.clear();
            onlineBombFrame = BombFrame{};
            for (int y = 0; y < MAP_TILES; ++y)
                for (int x = 0; x < MAP_TILES; ++x)
                    if (board[y][x].obstacle == 9) board[y][x].obstacle = 0;
            ice = {};
            game_end = false;
            game_winner = 0;
            sentDirection = kInputNone;
            for (bool& key : keyStates) key = false;
            for (int& count : p1_current_item) count = 0;
            for (int& count : p2_current_item) count = 0;
            p1.SetIsHit(false);
            p2.SetIsHit(false);
        };
        const auto sendDirection = [&](bool placeBomb = false) {
            if (!onlinePlaying) return;
            const int direction = GetMovementDirection(keyStates, 'W', 'S', 'A', 'D');
            const auto mask = MovementMask(direction);
            if (mask == sentDirection && !placeBomb) return;
            if (inputSequence == (std::numeric_limits<std::uint32_t>::max)()) {
                onlineConnection->Stop();
                return;
            }
            if (send(PacketType::CS_INPUT, onlineMatchId, InputPayload(mask, inputSequence + 1, placeBomb))) {
                ++inputSequence;
                sentDirection = mask;
            }
        };
        const auto showLobby = [&] {
            if (roomState.empty()) return;
            const auto title = L"CrazyArcade Online | P" + std::to_wstring(onlinePlayerId) +
                L" | " + std::to_wstring(roomState[0]) + L"/2 | map " + std::to_wstring(roomState[3]) +
                L" | ready " + std::to_wstring(roomState[1]) + L"/" + std::to_wstring(roomState[2]) +
                (roomState[6] == onlinePlayerId ? L" | 1-3 map, " : L" | ") +
                L"\u2190/\u2192 color, R ready, C cancel";
            SetWindowText(hWnd, title.c_str());
        };

        switch (msg) {
        case WM_CREATE:
            SetWindowText(hWnd, L"CrazyArcade Online | Connecting...");
            SetTimer(hWnd, kOnlineTimer, kMoveTickMs, nullptr);
            return 0;
        case WM_CHAR:
        case WM_LBUTTONDOWN:
            return 0;
        case WM_KEYDOWN:
        case WM_KEYUP:
            if (onlinePlaying) {
                if (IsWasdKey(wParam)) {
                    keyStates[wParam] = msg == WM_KEYDOWN;
                    sendDirection();
                } else if (wParam == VK_SPACE && msg == WM_KEYDOWN && !(lParam & (1L << 30))) {
                    sendDirection(true);
                }
                return 0;
            }
            if (msg != WM_KEYDOWN || roomState.empty() || game_end || (lParam & (1L << 30))) return 0;
            if (wParam == 'C' || (!onlineMatchId && wParam == 'R')) {
                send(PacketType::CS_READY, 0, {static_cast<std::uint8_t>(wParam == 'R')});
            } else if (!onlineMatchId) {
                if (wParam >= '1' && wParam <= '3' && roomState[6] == onlinePlayerId) {
                    send(PacketType::CS_SELECT, 0, {static_cast<std::uint8_t>(SelectKind::Map),
                                                   static_cast<std::uint8_t>(wParam - '0')});
                } else if (wParam == VK_LEFT || wParam == VK_RIGHT) {
                    const int current = requestedColor < 0 ? roomState[3 + onlinePlayerId] : requestedColor;
                    const auto color = static_cast<std::uint8_t>((current + (wParam == VK_LEFT ? 6 : 1)) % 7);
                    if (send(PacketType::CS_SELECT, 0, {static_cast<std::uint8_t>(SelectKind::Color), color}))
                        requestedColor = color;
                }
            }
            return 0;
        case WM_KILLFOCUS:
            for (bool& key : keyStates) key = false;
            sendDirection();
            break;
        case WM_TIMER: {
            if (wParam != kOnlineTimer) return 0;
            bool redraw = false, invalid = false;
            for (const auto& packet : onlineConnection->TakeReceived()) {
                const auto id = packet.header.matchId;
                const auto& payload = packet.payload;
                switch (packet.header.packetType) {
                case PacketType::SC_WELCOME:
                    invalid = id || onlinePlayerId || payload.size() != 1 || payload[0] < 1 || payload[0] > 2;
                    if (!invalid) {
                        onlinePlayerId = payload[0];
                        game_started = true;
                        redraw = true;
                    }
                    break;
                case PacketType::SC_ROOM_STATE:
                    invalid = id || !onlinePlayerId || !ValidRoomStatePayload(payload);
                    if (!invalid) {
                        roomState = payload;
                        if (!onlineMatchId) {
                            showLobby();
                            redraw = true;
                        }
                    }
                    break;
                case PacketType::SC_SNAPSHOT: {
                    invalid = !onlinePlayerId || onlineMatchId || id <= previousMatchId ||
                        roomState.empty() || !ApplyMapItemPacket(packet, onlineMap);
                    if (invalid) break;
                    clearOnlineDisplay();
                    onlineMatchId = id;
                    requestedColor = -1;
                    playerTick = inputSequence = 0;
                    sentDirection = kInputNone;
                    for (bool& key : keyStates) key = false;
                    for (int y = 0; y < MAP_TILES; ++y)
                        for (int x = 0; x < MAP_TILES; ++x)
                            board[y][x] = onlineMap.board[y][x];
                    InitOnlinePlayer(p1, 1, onlineMap.stage, roomState[4], onlineMap.abilities[0]);
                    InitOnlinePlayer(p2, 2, onlineMap.stage, roomState[5], onlineMap.abilities[1]);
                    ice = onlineMap.ice;
                    p1.SetCurrentApple(onlineMap.forest.carried[0]);
                    p2.SetCurrentApple(onlineMap.forest.carried[1]);
                    p1Applebox = onlineMap.forest.deposited[0];
                    p2Applebox = onlineMap.forest.deposited[1];
                    p1currentTime = onlineMap.city.remaining[0];
                    p2currentTime = onlineMap.city.remaining[1];
                    if (onlineMap.stage == 2) {
                        train = Train{};
                        init_train(train);
                        ApplyOnlineTrain(train, onlineMap.city);
                    }
                    SetWindowText(hWnd, L"CrazyArcade Online | Waiting for both players, C cancel");
                    send(PacketType::CS_LOADED, onlineMatchId, {});
                    break;
                }
                case PacketType::SC_MATCH_START:
                    invalid = !onlineMatchId || id != onlineMatchId || onlinePlaying || !payload.empty();
                    if (!invalid) {
                        onlinePlaying = true;
                        for (bool& key : keyStates) key = false;
                    }
                    break;
                case PacketType::SC_PLAYER_STATE: {
                    PlayerFrame frame;
                    invalid = !onlinePlaying || id != onlineMatchId || !ReadPlayerState(payload, frame);
                    if (invalid || frame.tick <= playerTick) break;
                    playerTick = frame.tick;
                    ApplyOnlinePosition(p1, frame.players[0]);
                    ApplyOnlinePosition(p2, frame.players[1]);
                    if (!selected_stage) {
                        selected_stage = onlineMap.stage;
                        PlaySound(nullptr, nullptr, 0);
                        const auto title = L"CrazyArcade Online | P" + std::to_wstring(onlinePlayerId) +
                            L" (WASD + Space)";
                        SetWindowText(hWnd, title.c_str());
                    }
                    redraw = true;
                    break;
                }
                case PacketType::SC_BOMB_STATE: {
                    BombFrame frame;
                    invalid = !onlineMatchId || id != onlineMatchId || !ReadBombState(payload, frame);
                    if (invalid || frame.sequence <= onlineBombFrame.sequence) break;
                    bool placed = false, exploded = false;
                    for (const auto& view : frame.bombs) {
                        const auto previous = std::find_if(onlineBombFrame.bombs.begin(), onlineBombFrame.bombs.end(),
                            [&](const BombView& b) { return b.id == view.id; });
                        placed |= previous == onlineBombFrame.bombs.end() && view.state == 1;
                        exploded |= view.state == 2 &&
                            (previous == onlineBombFrame.bombs.end() || previous->state != 2);
                    }
                    ApplyBombOccupancy(onlineMap.board, frame);
                    for (int y = 0; y < MAP_TILES; ++y)
                        for (int x = 0; x < MAP_TILES; ++x)
                            board[y][x] = onlineMap.board[y][x];
                    p1bombs.clear();
                    p2bombs.clear();
                    for (const auto& view : frame.bombs) {
                        Bomb display;
                        display.ApplyOnlineState(view.col, view.row, view.state, view.time,
                                                 view.left, view.right, view.up, view.down);
                        (view.owner == 1 ? p1bombs : p2bombs).push_back(display);
                    }
                    onlineBombFrame = std::move(frame);
                    if (exploded)
                        PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Bubble.wav"), nullptr, SND_FILENAME | SND_ASYNC);
                    else if (placed)
                        PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Ballon.wav"), nullptr, SND_FILENAME | SND_ASYNC);
                    redraw = true;
                    break;
                }
                case PacketType::SC_MATCH_CANCEL:
                    invalid = !onlineMatchId || id != onlineMatchId || payload.size() != 1 ||
                        payload[0] < 1 || payload[0] > 3;
                    if (!invalid) {
                        clearOnlineDisplay();
                        onlineMap = MapItemState{};
                        previousMatchId = onlineMatchId;
                        onlineMatchId = 0;
                        onlinePlaying = false;
                        selected_stage = 0;
                        redraw = true;
                    }
                    break;
                case PacketType::SC_PEER_LEFT:
                    invalid = id || !onlinePlayerId || payload.size() != 1 || payload[0] < 1 ||
                        payload[0] > 2 || payload[0] == onlinePlayerId;
                    break;
                case PacketType::SC_STATE:
                case PacketType::SC_EVENT:
                case PacketType::SC_MATCH_END: {
                    if (packet.header.packetType == PacketType::SC_EVENT && id &&
                        (id <= previousMatchId || (onlineMatchId && id < onlineMatchId))) break;
                    ItemPickup pickup;
                    invalid = !onlineMatchId || id != onlineMatchId || !ApplyMapItemPacket(packet, onlineMap, &pickup);
                    if (!invalid) {
                        if (packet.header.packetType == PacketType::SC_EVENT) {
                            for (int y = 0; y < MAP_TILES; ++y)
                                for (int x = 0; x < MAP_TILES; ++x)
                                    board[y][x] = onlineMap.board[y][x];
                        }
                        ice = onlineMap.ice;
                        p1.SetCurrentApple(onlineMap.forest.carried[0]);
                        p2.SetCurrentApple(onlineMap.forest.carried[1]);
                        p1Applebox = onlineMap.forest.deposited[0];
                        p2Applebox = onlineMap.forest.deposited[1];
                        if (onlineMap.stage == 2) {
                            p1currentTime = onlineMap.city.remaining[0];
                            p2currentTime = onlineMap.city.remaining[1];
                            ApplyOnlineTrain(train, onlineMap.city);
                        }
                        if (pickup.playerId && pickup.item < kCityClockItem) {
                            auto& player = pickup.playerId == 1 ? p1 : p2;
                            auto& itemCounts = pickup.playerId == 1 ? p1_current_item : p2_current_item;
                            ApplyOnlineAbilities(player, onlineMap.abilities[pickup.playerId - 1]);
                            ++itemCounts[pickup.item - 1];
                        }
                        if (packet.header.packetType == PacketType::SC_MATCH_END) {
                            onlinePlaying = false;
                            game_end = true;
                            game_winner = onlineMap.stage == 1 ? onlineMap.forest.winner :
                                onlineMap.stage == 2 ? static_cast<int>(onlineMap.city.result) : static_cast<int>(ice.result);
                            SetWindowText(hWnd, game_winner == 3 ? L"CrazyArcade Online | Draw" :
                                game_winner == 1 ? L"CrazyArcade Online | P1 wins" : L"CrazyArcade Online | P2 wins");
                        }
                        redraw = true;
                    }
                    break;
                }
                case PacketType::SC_ERROR: {
                    const auto code = payload.size() == 2 ? static_cast<ErrorCode>(PayloadWord(payload.data())) :
                        static_cast<ErrorCode>(0);
                    if (!id && code == ErrorCode::SelectionDenied) break;
                    if (!id && ErrorText(code)) {
                        const std::string reason = ErrorText(code);
                        const auto title = L"CrazyArcade Online | " + std::wstring(reason.begin(), reason.end());
                        SetWindowText(hWnd, title.c_str());
                        onlineConnection->Stop();
                    } else {
                        invalid = true;
                    }
                    break;
                }
                default:
                    invalid = true;
                    break;
                }
                if (invalid) {
                    onlineConnection->Stop();
                    break;
                }
            }
            if (invalid || onlineConnection->Ended()) {
                clearOnlineDisplay();
                onlineMap = MapItemState{};
                onlinePlaying = false;
                selected_stage = 0;
                game_started = false;
                KillTimer(hWnd, kOnlineTimer);
                if (invalid) SetWindowText(hWnd, L"CrazyArcade Online | Invalid server state");
                else if (!onlinePlayerId) SetWindowText(hWnd, L"CrazyArcade Online | Cannot connect (check IPv4, port and protocol)");
                else SetWindowText(hWnd, L"CrazyArcade Online | Disconnected");
                redraw = true;
            }
            if (redraw) InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(hWnd, kOnlineTimer);
            onlineConnection->Stop();
            break;
        }
    }

    switch (msg) {
    case WM_CREATE:
        p1_check_color_pos = 270;
        p2_check_color_pos = 270;
        p1.SetColor(0);
		p2.SetColor(0);
        //로그인 사운드 재생
        PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Login.wav"), NULL, SND_LOOP | SND_ASYNC);
        
        break;
    case WM_KEYDOWN:
        if (wParam < 256) keyStates[wParam] = true;
        if (game_end) break;
        if (game_started && selected_stage) {
            if (wParam == VK_SPACE || wParam == VK_RETURN) {
                Player& player = wParam == VK_SPACE ? p1 : p2;
                auto& bombs = wParam == VK_SPACE ? p1bombs : p2bombs;
                if (static_cast<int>(bombs.size()) < player.GetMaxBomb()) {
                    Bomb newBomb;
                    if (newBomb.make_Bomb(player, board)) {
                        bombs.push_back(newBomb);
                        PlaySound(NULL, NULL, NULL);
                        PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Ballon.wav"), NULL, SND_ASYNC);
                    }
                }
            }
        }
        else if (game_started) {
            if (wParam == VK_LEFT) {
                if (p2_check_color_pos > 270) {
					p2_check_color_pos -= 155;
                    p2.SetColor(p2.GetColor() - 1);
                }
            }
            else if (wParam == VK_RIGHT) {
                if (p2_check_color_pos < 1200) {
                    p2_check_color_pos += 155;
                    p2.SetColor(p2.GetColor() + 1);
                }
            }
        }
        if (selected_stage == 1) {
            p1.on_applebox(&p1, &p1Applebox, kForestBasketColumn[0], kForestBasketRow);
            p2.on_applebox(&p2, &p2Applebox, kForestBasketColumn[1], kForestBasketRow);
			if (p1Applebox >= kForestWinningTotal) {
				//p1승리 이제 win메시지 띄우고 전부 초기화
                game_end = true;
                game_winner = 1;
			}
            else if (p2Applebox >= kForestWinningTotal) {
                //p2승리 이제 win메시지 띄우고 전부 초기화
                game_end = true;
				game_winner = 2;
            }
        }
	
        // The game timer already redraws movement; key autorepeat needs no extra paint.
        // Keep the existing menu, bomb and apple-box key handling above.
        if (selected_stage == 0 || !IsMovementKey(wParam))
            InvalidateRect(hWnd, NULL, FALSE);
        break;
    case WM_KEYUP:
        if (wParam < 256) keyStates[wParam] = false;
        break;
    case WM_TIMER: {
        if (game_end) break;
        switch (wParam) {
        case 1: {
            
            if (selected_stage) {
                int p1Direction = GetMovementDirection(keyStates, 'W', 'S', 'A', 'D');
                HandlePlayerMovement(p1, p1Direction, board);
                UpdatePlayerPosition(p1, p1.Getspeed()/8.0f);

                int p2Direction = GetMovementDirection(keyStates, VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT);
                HandlePlayerMovement(p2, p2Direction, board);
                UpdatePlayerPosition(p2, p2.Getspeed()/8.0f);

                int temp = -1;
                temp=p1.collision_item(&p1, board, p1currentTime)-1;
				if (temp >= 0 && temp < 3) { // 아이템이 유효한 범위 내에 있는지 확인
					p1_current_item[temp]++; // 아이템 개수 증가
                    PlaySound(NULL, NULL, NULL);
                    PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Acting.wav"), NULL, SND_ASYNC);
				}
				
               temp= p2.collision_item(&p2, board, p2currentTime)-1;
			
               if (temp >= 0 && temp < 3) { // 아이템이 유효한 범위 내에 있는지 확인
				   p2_current_item[temp]++; // 아이템 개수 증가
                   PlaySound(NULL, NULL, NULL);
                   PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Acting.wav"), NULL, SND_ASYNC);
               }
            }
            frameCount++;

            DWORD currentTime = GetTickCount();
            if (currentTime - lastTime >= 1000) { // 1초 지남
                currentFPS = frameCount;
                frameCount = 0;
                lastTime = currentTime;
            }
            if (selected_stage == 2) {
                if (move_train(train)) {
                    make_item(train, board);

                }
                move_item(train, board);
				if (train.is_spawned) {
                    if (!train.collisionP1) {
                        if (p1.collision_train(&p1, train, board, p1currentTime)) {
                            train.collisionP1 = true; // 플레이어 1과 충돌한 경우
                            PlaySound(NULL, NULL, NULL);
                            PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Die.wav"), NULL, SND_ASYNC);
                        }
                    }
				}
				if (train.is_spawned) {
                    if (!train.collisionP2) {
                        if (p2.collision_train(&p2, train, board, p2currentTime)) {
                            train.collisionP2 = true; // 플레이어 2와 충돌한 경우
                            PlaySound(NULL, NULL, NULL);
                            PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Die.wav"), NULL, SND_ASYNC);
                        }
                    }
				}
			
			
				
                p1Time++;
                p2Time++;
                if (p1Time >= 50) {
                    p1Time = 0;
                    p1currentTime--;
                }
                if (p2Time >= 50) {
                    p2Time = 0;
                    p2currentTime--;
                }

				if (p1currentTime <= 0) {
					game_end = true;
					game_winner = 2; // 플레이어 2 승리
					KillTimer(hWnd, 1); // 타이머 중지
					KillTimer(hWnd, 2); // 타이머 중지
                    PlaySound(NULL, NULL, NULL);
                    PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Win.wav"), NULL, SND_ASYNC);
				}
				else if (p2currentTime <= 0) {
					game_end = true;
					game_winner = 1; // 플레이어 1 승리
                    KillTimer(hWnd, 1); // 타이머 중지
                    KillTimer(hWnd, 2); // 타이머 중지
                    PlaySound(NULL, NULL, NULL);
                    PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Win.wav"), NULL, SND_ASYNC);
				}
            }
           
            
        }
            break;
        case 2:
           
        {//충돌 시 깜빡이는거 구현하려고 필요한거
            if (p1.GetIsHit()) {
                p1_hit_cnt++;
                if (p1_hit_cnt >= 10) {
                    p1.SetIsHit(false);
                    p1_hit_cnt = 0;
                }
            }
            if (p2.GetIsHit()) {
                p2_hit_cnt++;
                if (p2_hit_cnt >= 10) {
                    p2.SetIsHit(false);
                    p2_hit_cnt = 0;
                }

            }
        }
            // Advance all timers before resolving newly started explosions.
            for (Bomb& bomb : p1bombs) bomb.tick_Bomb();
            for (Bomb& bomb : p2bombs) bomb.tick_Bomb();
            for (Bomb& bomb : p1bombs) {
                if (bomb.GetState() == 2 && bomb.GetTime() == 10)
                    update_BombExplosion(bomb, board, &p1, selected_stage == 1);
            }
            for (Bomb& bomb : p2bombs) {
                if (bomb.GetState() == 2 && bomb.GetTime() == 10)
                    update_BombExplosion(bomb, board, &p2, selected_stage == 1);
            }

            //플레이어 폭탄 충돌
            for (int i = 0;i < p1bombs.size();++i) {
                // 자기 물풍선도 상대 물풍선과 같은 피해를 한 번만 적용한다.
                if (p1bombs[i].GetState() == 2 &&
                    !p1bombs[i].GetIsCollisionOwner() &&
                    p1bombs[i].collision_bomb_player(p1bombs[i], p1)) {
                    if (!p1.GetIsHit()) {
                        PlaySound(NULL, NULL, NULL);
                        PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Die.wav"), NULL, SND_ASYNC);
                    }
                    p1.SetIsHit(true);
                    if (selected_stage == 1) {
                        p1.LoseApples();
                    }
                    else if (selected_stage == 2) {
                        p1currentTime -= 2;
                    }
                    else if (selected_stage == 3) {
                        ApplyIceHit(ice, 1);
                    }
                    if (selected_stage == 2 && p1currentTime <= 0) {
                        game_end = true;
                        game_winner = 2;
                        PlaySound(NULL, NULL, NULL);
                        PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Win.wav"), NULL, SND_ASYNC);
                    }
                    p1bombs[i].SetIsCollisionOwner(true);
                }

				if (!p1bombs.data()[i].GetIsCollisionPlayer()&&p1bombs.data()[i].GetState() == 2) {
                    if (p1bombs.data()[i].collision_bomb_player(p1bombs.data()[i], p2)) {
                        if (p2.GetIsHit() == false) {
                            PlaySound(NULL, NULL, NULL);
                            PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Die.wav"), NULL, SND_ASYNC);
                        }
						p2.SetIsHit(true); // 플레이어 2가 폭탄에 맞음
                       
                        if (selected_stage == 1) {
							//스테이지 1 충돌 처리
                            if (p2.GetCurrentApple() > 0) {
                                p2.LoseApples();
                            }
                            p1bombs.data()[i].SetIsCollisionPlayer(true); // 플레이어 2와 충돌한 경우
                        }
                        else if (selected_stage == 2) {
                            //스테이지 2 충돌 처리
							p2currentTime-=2;

							if (p2currentTime <= 0) {
								game_end = true;
								game_winner = 1; // 플레이어 1 승리
                                PlaySound(NULL, NULL, NULL);
                                PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Win.wav"), NULL, SND_ASYNC);
							}
                            p1bombs.data()[i].SetIsCollisionPlayer(true);
                        }
                        else if (selected_stage == 3) {
                            //스테이지 3 충돌 처리
                            ApplyIceHit(ice, 2);
                            p1bombs.data()[i].SetIsCollisionPlayer(true); // 플레이어 2와 충돌한 경우
                        }
                    }
				}
            }
          
            
                for (int i = 0;i < p2bombs.size();++i) {
                // 자기 물풍선도 상대 물풍선과 같은 피해를 한 번만 적용한다.
                if (p2bombs[i].GetState() == 2 &&
                    !p2bombs[i].GetIsCollisionOwner() &&
                    p2bombs[i].collision_bomb_player(p2bombs[i], p2)) {
                    if (!p2.GetIsHit()) {
                        PlaySound(NULL, NULL, NULL);
                        PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Die.wav"), NULL, SND_ASYNC);
                    }
                    p2.SetIsHit(true);
                    if (selected_stage == 1) {
                        p2.LoseApples();
                    }
                    else if (selected_stage == 2) {
                        p2currentTime -= 2;
                    }
                    else if (selected_stage == 3) {
                        ApplyIceHit(ice, 2);
                    }
                    if (selected_stage == 2 && p2currentTime <= 0) {
                        game_end = true;
                        game_winner = 1;
                        PlaySound(NULL, NULL, NULL);
                        PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Win.wav"), NULL, SND_ASYNC);
                    }
                    p2bombs[i].SetIsCollisionOwner(true);
                }

                    if (!p2bombs.data()[i].GetIsCollisionPlayer() && p2bombs.data()[i].GetState() == 2) {
						
                        if (p2bombs.data()[i].collision_bomb_player(p2bombs.data()[i], p1)) {
                            if (p1.GetIsHit() == false) {
                                PlaySound(NULL, NULL, NULL);
                                PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Die.wav"), NULL, SND_ASYNC);
                            }
                            p1.SetIsHit(true); // 플레이어 1이 폭탄에 맞음
                            
                            //스테이지 별 충돌 처리
                            if (selected_stage == 1) {
                                //스테이지 1 충돌 처리
                                if (p1.GetCurrentApple() > 0) {
                                    p1.LoseApples();
                                }
                                p2bombs.data()[i].SetIsCollisionPlayer(true); // 플레이어 1과 충돌한 경우
                            }
							else if (selected_stage == 2) {
								//스테이지 2 충돌 처리
								p1currentTime-=2;
								if (p1currentTime <= 0) {
									game_end = true;
									game_winner = 2; // 플레이어 2 승리
                                    PlaySound(NULL, NULL, NULL);
                                    PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Win.wav"), NULL, SND_ASYNC);
								}
                                p2bombs.data()[i].SetIsCollisionPlayer(true);
							}
							else if (selected_stage == 3) {
								//스테이지 3 충돌 처리
                                ApplyIceHit(ice, 1);
                                p2bombs.data()[i].SetIsCollisionPlayer(true); // 플레이어 1과 충돌한 경우
							}
                        }
                    }
                }
            
            
            // Resolve after every bomb in this update has applied its damage.
            if (selected_stage == 3 && FinishIceTick(ice)) {
                game_end = true;
                game_winner = static_cast<int>(ice.result);
                KillTimer(hWnd, 1);
                KillTimer(hWnd, 2);
                PlaySound(NULL, NULL, NULL);
                PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Win.wav"), NULL, SND_ASYNC);
            }

            // Remove inactive bombs after hit processing, without skipping entries.
            for (int i = static_cast<int>(p1bombs.size()) - 1; i >= 0; --i) {
                if (p1bombs[i].GetState() == 0)
                    p1bombs.erase(p1bombs.begin() + i);
            }
            for (int i = static_cast<int>(p2bombs.size()) - 1; i >= 0; --i) {
                if (p2bombs[i].GetState() == 0)
                    p2bombs.erase(p2bombs.begin() + i);
            }

            break;
            }
   
        InvalidateRect(hWnd, NULL, FALSE);
        break;
    }
    case WM_LBUTTONDOWN: {
		int pos_x = LOWORD(lParam);
        if (!game_started && pos_x < 800) {
            PlaySound(NULL, NULL, NULL);
			PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Menu.wav"), NULL, SND_ASYNC | SND_LOOP);
            game_started = true;
        }
        else if (!game_started &&pos_x > 800) {
            PostQuitMessage(0);
        }
        InvalidateRect(hWnd, NULL, FALSE);
        break;
    }
    case WM_CHAR:
       
        if (selected_stage == 0) {
            if (wParam == '1') {
                selected_stage = 1;
                PlaySound(NULL, NULL, NULL);
                PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\GameStart.wav"), NULL, SND_ASYNC );
                
                const auto p1_start_point = PlayerStartCell(1, 1);
                const auto p2_start_point = PlayerStartCell(1, 2);
                int hueFilterMin = 0;   // 원하는 색 필터 범위
                int hueFilterMax = 360;
             
                HBITMAP original = ResourceManager::GetBitmap(BitmapID::Player1Moving);
                
                HBITMAP recolored = AdjustBitmapHueWithFilter(original, p1.GetColor(), hueFilterMin, 20);
                ResourceManager::SetBitmap(BitmapID::Player1Moving, recolored);

				original = ResourceManager::GetBitmap(BitmapID::Player2Moving);
				recolored = AdjustBitmapHueWithFilter(original, p2.GetColor(), hueFilterMin, 20);
				ResourceManager::SetBitmap(BitmapID::Player2Moving, recolored);

                p1.Init_Player({p1_start_point.x, p1_start_point.y});
                p2.Init_Player({p2_start_point.x, p2_start_point.y});
                p2.SetPlayerNum(2);
                init_board_Forest(board);
               
                SetTimer(hWnd, TIMER_ID, TIMER_INTERVAL, NULL);
                SetTimer(hWnd, 2, 50, NULL);
              
              
            }
            else  if (wParam == '2') {
                PlaySound(NULL, NULL, NULL);
                PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\GameStart.wav"), NULL, SND_ASYNC);
                selected_stage = 2;
                const auto p1_start_point = PlayerStartCell(2, 1);
                const auto p2_start_point = PlayerStartCell(2, 2);
				init_board_City(board);
                p1.Init_Player({p1_start_point.x, p1_start_point.y});
                p2.Init_Player({p2_start_point.x, p2_start_point.y});
				init_train(train);
                p1currentTime = 30;
				p2currentTime = 30;
                int hueFilterMin = 0;   // 원하는 색 필터 범위
                int hueFilterMax = 360;
                HBITMAP original = ResourceManager::GetBitmap(BitmapID::Player1Moving);

                HBITMAP recolored = AdjustBitmapHueWithFilter(original, p1.GetColor(), hueFilterMin, 20);
                ResourceManager::SetBitmap(BitmapID::Player1Moving, recolored);

                original = ResourceManager::GetBitmap(BitmapID::Player2Moving);
                recolored = AdjustBitmapHueWithFilter(original, p2.GetColor(), hueFilterMin, 20);
                ResourceManager::SetBitmap(BitmapID::Player2Moving, recolored);
                p2.SetPlayerNum(2);
                SetTimer(hWnd, TIMER_ID, TIMER_INTERVAL, NULL);
                SetTimer(hWnd, 2, 50, NULL);
            }
            else  if (wParam == '3') {
                PlaySound(NULL, NULL, NULL);
                PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\GameStart.wav"), NULL, SND_ASYNC);
                selected_stage = 3;
                const auto p1_start_point = PlayerStartCell(3, 1);
                const auto p2_start_point = PlayerStartCell(3, 2);
                int hueFilterMin = 0;   // 원하는 색 필터 범위
                int hueFilterMax = 360;

                HBITMAP original = ResourceManager::GetBitmap(BitmapID::Player1Moving);

                HBITMAP recolored = AdjustBitmapHueWithFilter(original, p1.GetColor(), hueFilterMin, 20);
                ResourceManager::SetBitmap(BitmapID::Player1Moving, recolored);

                original = ResourceManager::GetBitmap(BitmapID::Player2Moving);
                recolored = AdjustBitmapHueWithFilter(original, p2.GetColor(), hueFilterMin, 20);
                ResourceManager::SetBitmap(BitmapID::Player2Moving, recolored);

                p1.Init_Player({p1_start_point.x, p1_start_point.y});
                p2.Init_Player({p2_start_point.x, p2_start_point.y});
                ice = {};
                p2.SetPlayerNum(2);
                init_board_Ice(board);
                SetTimer(hWnd, TIMER_ID, TIMER_INTERVAL, NULL);
                SetTimer(hWnd, 2, 50, NULL);
            }
            else if (wParam == 'a') {
             if (game_started) {
                 
                    if (p1_check_color_pos > 270) {
                        p1_check_color_pos -= 155;
                        p1.SetColor(p1.GetColor() - 1);
                    }
               
                
                 }
            }
            else if (wParam == 'd') {
                if (game_started) {
                if (p1_check_color_pos < 1200) {
                    p1_check_color_pos += 155;
                    p1.SetColor(p1.GetColor() + 1);
                }
            }
                
            }
        }
        if (selected_stage) {
			
            if (wParam == 'r' && game_end) {
            //게임 리셋
				PlaySound(NULL, NULL, NULL);
                PlaySound(TEXT("..\\..\\CrazyArcade\\Resource\\Sound\\Login.wav"), NULL, SND_LOOP | SND_ASYNC);
            if (selected_stage == 1) {
                p1.Init_Player({ 8, 18 });
                p1.Init_Player({ 11, 18 });
                p1Applebox = 0;
                p2Applebox = 0;
            }
            selected_stage = 0;
            p1bombs;
            p2bombs;
            game_started = false;
            game_end = false;
            game_winner = 0;
            p1_check_color_pos = 270;
            p2_check_color_pos = 270;
            p1Applebox = 0;
            p2Applebox = 0;
            for (int i = 0;i < 3;++i) {
				p1_current_item[i] = 0;
				p2_current_item[i] = 0;
            }
			p1bombs.clear();
			p2bombs.clear();
            //캐릭터 색상 원래대로 복구
			HBITMAP original = ResourceManager::GetBitmap(BitmapID::Player1Moving);
			DeleteObject(original);
			original = ResourceManager::GetBitmap(BitmapID::Player2Moving);
			DeleteObject(original);
			ResourceManager::SetBitmap(BitmapID::Player1Moving, LoadBitmap(g_hInst, MAKEINTRESOURCE(IDB_BITMAP_PLAYER)));
			ResourceManager::SetBitmap(BitmapID::Player2Moving, LoadBitmap(g_hInst, MAKEINTRESOURCE(IDB_BITMAP_PLAYER)));

			p1.SetColor(0);
			p2.SetColor(0);
			p1.SetCurrentApple(0);
			p2.SetCurrentApple(0);
			p1currentTime = 100;
			p2currentTime = 100;
		


			p1.SetPlayerNum(1);
			p2.SetPlayerNum(2);

            }
			
        }
        break;
    case WM_PAINT: {
      
            hdc = BeginPaint(hWnd, &ps);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, 1500, 900);
            HGDIOBJ oldBmp = SelectObject(mem, bmp);
            RECT rc = { 0,0,WINDOW_WIDTH,WINDOW_HEIGHT };
            FillRect(mem, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
            if (!game_started) {
                
				// 게임이 시작되지 않은 상태에서의 처리
				HBITMAP loginBitmap = LoadBitmap(g_hInst, MAKEINTRESOURCE(IDB_BITMAP_GAME_LOGIN));
				HDC loginDC = CreateCompatibleDC(mem);
				HBITMAP oldLoginBitmap = (HBITMAP)SelectObject(loginDC, loginBitmap);
				StretchBlt(mem, 0, 0, 1500, 900, loginDC, 0, 0, 800, 600, SRCCOPY);
				SelectObject(loginDC, oldLoginBitmap);
				DeleteObject(loginBitmap);
				DeleteDC(loginDC);
			
            }
            else if (game_started) {
				// 게임이 시작된 상태에서의 처리
                if (selected_stage == 0) {
					HBITMAP selectStageBitmap = LoadBitmap(g_hInst, MAKEINTRESOURCE(IDB_BITMAP_SELECT_STAGE));
					HDC selectStageDC = CreateCompatibleDC(mem);
					HBITMAP oldSelectStageBitmap = (HBITMAP)SelectObject(selectStageDC, selectStageBitmap);
					StretchBlt(mem, 0, 0, 1500, 900, selectStageDC, 0, 0, 1500, 900, SRCCOPY);
					SelectObject(selectStageDC, oldSelectStageBitmap);
					DeleteObject(selectStageBitmap);

                    //체크표시 그리기
                    constexpr int checkWidth = 100, checkHeight = 50;
                    const auto drawCheck = [&](int id, int x, int y) {
                        HBITMAP bitmap = LoadBitmap(g_hInst, MAKEINTRESOURCE(id));
                        HBITMAP oldBitmap = (HBITMAP)SelectObject(selectStageDC, bitmap);
                        TransparentBlt(mem, x, y, checkWidth, checkHeight, selectStageDC,
                            0, 0, 224, 150, RGB(255, 0, 255));
                        SelectObject(selectStageDC, oldBitmap);
                        DeleteObject(bitmap);
                    };
                    if (onlineConnection) {
                        if (!roomState.empty()) {
                            // Swatch centers in the stage selection bitmap, in color-index order.
                            constexpr int colorCenters[] = {264, 421, 570, 730, 885, 1040, 1195};
                            const int centerX = colorCenters[roomState[3 + onlinePlayerId]];
                            drawCheck(onlinePlayerId == 1 ? IDB_BITMAP_P1_CHECK : IDB_BITMAP_P2_CHECK,
                                centerX - checkWidth / 2, 738 - checkHeight / 2);
                        }
                    } else {
                        drawCheck(IDB_BITMAP_P1_CHECK, p1_check_color_pos, 670);
                        drawCheck(IDB_BITMAP_P2_CHECK, p2_check_color_pos, 640);
                    }
                    DeleteDC(selectStageDC);
                }
				else if (selected_stage >= 1) {
                    //공통UI 출력

					//게임 UI 출력
                    HBITMAP gameui = LoadBitmap(g_hInst, MAKEINTRESOURCE(IDB_BITMAP_GAMEUI));
                    HDC uiDC = CreateCompatibleDC(mem);
                    HBITMAP oldUIBitmap = (HBITMAP)SelectObject(uiDC, gameui);
                    StretchBlt(mem, 0, 0, 1450, 900, uiDC, 0, 0, 1040, 780, SRCCOPY);
                    SelectObject(uiDC, oldUIBitmap);
                    DeleteObject(gameui);
                    DeleteDC(uiDC);
					HBITMAP p1bitmap = ResourceManager::GetBitmap(BitmapID::Player1Moving);
					HBITMAP p2bitmap = ResourceManager::GetBitmap(BitmapID::Player2Moving);
					HDC p1DC = CreateCompatibleDC(mem);
					HDC p2DC = CreateCompatibleDC(mem);
					HBITMAP oldP1Bitmap = (HBITMAP)SelectObject(p1DC, p1bitmap);
					HBITMAP oldP2Bitmap = (HBITMAP)SelectObject(p2DC, p2bitmap);
					TransparentBlt(mem, 175, 150, 80, 50, p1DC, 0, 0, 50, 40, RGB(255, 0, 255));
					TransparentBlt(mem, 1195, 150, 80, 50, p2DC, 0, 0, 50, 40, RGB(255, 0, 255));
					SelectObject(p1DC, oldP1Bitmap);
					SelectObject(p2DC, oldP2Bitmap);
					DeleteDC(p1DC);
					DeleteDC(p2DC);
                    //플레이어 아이템 출력
					HBITMAP ItemPowerBitmap = ResourceManager::GetBitmap(BitmapID::ItemPower);
					HBITMAP ItemBombCountBitmap = ResourceManager::GetBitmap(BitmapID::ItemBombCount);
					HBITMAP ItemSpeedBitmap = ResourceManager::GetBitmap(BitmapID::ItemSpeed);
					HDC ItemPowerDC = CreateCompatibleDC(mem);
					HDC ItemBombCountDC = CreateCompatibleDC(mem);
					HDC ItemSpeedDC = CreateCompatibleDC(mem);
					HBITMAP oldItemPowerBitmap = (HBITMAP)SelectObject(ItemPowerDC, ItemPowerBitmap);
					HBITMAP oldItemBombCountBitmap = (HBITMAP)SelectObject(ItemBombCountDC, ItemBombCountBitmap);
                    HBITMAP oldItemSpeedBitmap = (HBITMAP)SelectObject(ItemSpeedDC, ItemSpeedBitmap);
					TransparentBlt(mem, 50, 700, 40, 40, ItemPowerDC, 0, 0, 38, 38, RGB(255, 0, 255));
					TransparentBlt(mem, 1200, 700, 40, 40, ItemPowerDC, 0, 0, 38, 38, RGB(255, 0, 255));
					TransparentBlt(mem, 50, 740, 40, 40, ItemBombCountDC, 0, 0, 38, 38, RGB(255, 0, 255));
					TransparentBlt(mem, 1200, 740, 40, 40, ItemBombCountDC, 0, 0, 38, 38, RGB(255, 0, 255));
					TransparentBlt(mem, 50, 780, 40, 40, ItemSpeedDC, 0, 0, 38, 38, RGB(255, 0, 255));
					TransparentBlt(mem, 1200, 780, 40, 40, ItemSpeedDC, 0, 0, 38, 38, RGB(255, 0, 255));
					//플레이어 아이템 개수 출력
					DrawOutlinedText(mem, 100, 700, std::to_wstring(p1_current_item[0]).c_str());
					DrawOutlinedText(mem, 1250, 700, std::to_wstring(p2_current_item[0]).c_str());
					DrawOutlinedText(mem, 100, 740, std::to_wstring(p1_current_item[1]).c_str());
					DrawOutlinedText(mem, 1250, 740, std::to_wstring(p2_current_item[1]).c_str());
					DrawOutlinedText(mem, 100, 780, std::to_wstring(p1_current_item[2]).c_str());
					DrawOutlinedText(mem, 1250, 780, std::to_wstring(p2_current_item[2]).c_str());

					SelectObject(ItemPowerDC, oldItemPowerBitmap);
					SelectObject(ItemBombCountDC, oldItemBombCountBitmap);
					SelectObject(ItemSpeedDC, oldItemSpeedBitmap);
					DeleteDC(ItemPowerDC);
					DeleteDC(ItemBombCountDC);
					DeleteDC(ItemSpeedDC);
                    if (selected_stage == 1) {
                        draw_tile_forest(mem);
                        draw_Bomb(mem, g_hInst, p1bombs.data(), static_cast<int>(p1bombs.size()), board);
                        draw_Bomb(mem, g_hInst, p2bombs.data(), static_cast<int>(p2bombs.size()), board);
                        DrawGrid_Forest(mem, g_hInst, board, &p1, &p2);
                        //p1.Draw_Player(mem, &p1, RGB(255, 0, 255));
                        //p2.Draw_Player(mem, &p2, RGB(0, 255, 255));
                       


                        HBITMAP appleboxBitmap = ResourceManager::GetBitmap(BitmapID::ForestAppleBox);
                        HDC appleboxDC = CreateCompatibleDC(hdc);
                        HBITMAP oldAppleboxBitmap = (HBITMAP)SelectObject(appleboxDC, appleboxBitmap);
                        TransparentBlt(mem, 50, 260, 64, 64, appleboxDC, 0, 0, 64, 64, RGB(255, 0, 255));
                        TransparentBlt(mem, 1200, 260, 64, 64, appleboxDC, 0, 0, 64, 64, RGB(255, 0, 255));

                        HBITMAP applexBitmap = ResourceManager::GetBitmap(BitmapID::ForestApple);
                        HDC applexDC = CreateCompatibleDC(hdc);
                        HBITMAP oldApplexBitmap = (HBITMAP)SelectObject(applexDC, applexBitmap);
                        TransparentBlt(mem, 50, 220, 64, 64, applexDC, 0, 0, 40, 40, RGB(255, 0, 255));
                        TransparentBlt(mem, 1200, 220, 64, 64, applexDC, 0, 0, 40, 40, RGB(255, 0, 255));
                        //임시로 플레이어 사과 출력
                        int tempapple = p1.GetCurrentApple();
                        int tempapple2 = p2.GetCurrentApple();
                        DrawOutlinedText(mem, 125, 220, L" : ");
                        DrawOutlinedText(mem, 1280, 220, L" : ");
                        DrawOutlinedText(mem, 215, 220, std::to_wstring(tempapple).c_str());
                        DrawOutlinedText(mem, 1370, 220, std::to_wstring(tempapple2).c_str());
                        DrawOutlinedText(mem, 125, 270, L" : ");
                        DrawOutlinedText(mem, 1280, 270, L" : ");
                        DrawOutlinedText(mem, 215, 270, std::to_wstring(p1Applebox).c_str());
                        DrawOutlinedText(mem, 1370, 270, std::to_wstring(p2Applebox).c_str());


                        SelectObject(appleboxDC, oldAppleboxBitmap);
                        SelectObject(applexDC, oldApplexBitmap);
                        
                      
                        
                        DeleteDC(appleboxDC);
                        DeleteDC(applexDC);
                    }
					else if (selected_stage == 2) {
						// 다른 스테이지 처리
                        draw_tile_city(mem);
                        draw_Bomb(mem, g_hInst, p1bombs.data(), static_cast<int>(p1bombs.size()), board);
                        draw_Bomb(mem, g_hInst, p2bombs.data(), static_cast<int>(p2bombs.size()), board);
						DrawGrid_City(mem, g_hInst, board, &p1, &p2,train);

                        //플레이어 시간 출력
						HBITMAP timeBitmap = ResourceManager::GetBitmap(BitmapID::CityClock);
						HDC timeDC = CreateCompatibleDC(mem);
						HBITMAP oldTimeBitmap = (HBITMAP)SelectObject(timeDC, timeBitmap);
						TransparentBlt(mem, 100, 250, 40, 46, timeDC, 0, 0, 40, 46, RGB(255, 0, 255));
						TransparentBlt(mem, 1255, 250, 40, 46, timeDC, 0, 0, 40, 46, RGB(255, 0, 255));
						DrawOutlinedText(mem, 155, 250, std::to_wstring(p1currentTime).c_str());
						DrawOutlinedText(mem, 1310, 250, std::to_wstring(p2currentTime).c_str());
						SelectObject(timeDC, oldTimeBitmap);
						DeleteDC(timeDC);

					}
					else if (selected_stage == 3) {
						// 다른 스테이지 처리
                        draw_tile_ice(mem);
                        draw_Bomb(mem, g_hInst, p1bombs.data(), static_cast<int>(p1bombs.size()), board);
                        draw_Bomb(mem, g_hInst, p2bombs.data(), static_cast<int>(p2bombs.size()), board);
						DrawGrid_Ice(mem, g_hInst, board, &p1, &p2);
                        wchar_t positionText[50];
                        swprintf(positionText, 50, L"Y: %d, X: %d", p1.GetY(), p1.GetX());

                        //플레이어 하트 출력
						HBITMAP heartBitmap = ResourceManager::GetBitmap(BitmapID::Heart);
						HDC heartDC = CreateCompatibleDC(mem);
						HBITMAP oldHeartBitmap = (HBITMAP)SelectObject(heartDC, heartBitmap);
						for (int i = 0; i < p1CurrentHeart; ++i) {
							TransparentBlt(mem, 10 + i * 90, 250, 80, 64, heartDC, 0, 0, 80, 64, RGB(255, 0, 255));
						}
						for (int i = 0; i < p2CurrentHeart; ++i) {
							TransparentBlt(mem, 1170 + i * 90, 250, 80, 64, heartDC, 0, 0, 80, 64, RGB(255, 0, 255));
						}
						SelectObject(heartDC, oldHeartBitmap);
						
						DeleteDC(heartDC);
					}
                    //화면 프레임 출력
                   // std::wstring fpsText = L"FPS: " + std::to_wstring(currentFPS);
                   // DrawOutlinedText(mem, 10, 10, fpsText.c_str());  

					
					
				}
            
            
            }
           
            if (game_end) {
				
                DrawDarkOverlay(mem, 1500, 900);
                HBITMAP gameEndBitmap = LoadBitmap(g_hInst, MAKEINTRESOURCE(IDB_BITMAP_GAME_END));
                HDC gameEndDC = CreateCompatibleDC(mem);
                HBITMAP oldGameEndBitmap = (HBITMAP)SelectObject(gameEndDC, gameEndBitmap);
                RECT client{};
                GetClientRect(hWnd, &client);
                const int resultCenterX = (std::min)(client.right, 1500L) / 2;
                if (game_winner == static_cast<int>(IceResult::Draw)) {
                    DrawOutlinedText(mem, resultCenterX, 400, L"무승부", true);
                } else if (onlineConnection) {
                    constexpr int bannerWidth = 348, bannerHeight = 65;
                    const int sourceY = game_winner == onlinePlayerId ? bannerHeight : 0;
                    TransparentBlt(mem, resultCenterX - bannerWidth / 2, 400, bannerWidth, bannerHeight,
                        gameEndDC, 0, sourceY, bannerWidth, bannerHeight, RGB(255, 0, 255));
                } else if (game_winner == 1) {
					TransparentBlt(mem, 1100, 400, 348, 65, gameEndDC, 0, 0, 348, 65, RGB(255, 0, 255));
                    TransparentBlt(mem, 50, 400, 348, 65, gameEndDC, 0, 65, 348, 65, RGB(255, 0, 255));
				}
                else if (game_winner == 2) {
                    // 플레이어 2 승리 처리
                    TransparentBlt(mem, 50, 400, 348, 65, gameEndDC, 0, 0, 348, 65, RGB(255, 0, 255));
                    TransparentBlt(mem, 1100, 400, 348, 65, gameEndDC, 0, 65, 348, 65, RGB(255, 0, 255));
                }
              
                if (onlineConnection)
                    DrawOutlinedText(mem, 400, 600, L"온라인 재경기는 아직 지원하지 않습니다.");
                else
                    DrawOutlinedText(mem, 500, 600,L"다시 시작하려면 R을 누르시오 .");
                SelectObject(gameEndDC, oldGameEndBitmap);
                DeleteObject(gameEndBitmap);
                DeleteDC(gameEndDC);
            }
            BitBlt(hdc, 0, 0, 1500, 900, mem, 0, 0, SRCCOPY);
            SelectObject(mem, oldBmp);
            DeleteObject(bmp);
            
            DeleteDC(mem);
            EndPaint(hWnd, &ps);
        
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hWnd, TIMER_ID);
       
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

