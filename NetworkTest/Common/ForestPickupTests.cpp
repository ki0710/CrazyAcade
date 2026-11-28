// Deterministic game ticks with production Room, framing and client codecs.
#include "SocketUtils.h"
#include <cstdlib>
#include <map>

namespace {
std::map<SOCKET, std::vector<std::uint8_t>> wireBySocket;
int CaptureSend(SOCKET socket, const char* bytes, int size, int) {
    auto& output = wireBySocket.at(socket);
    output.insert(output.end(), bytes, bytes + size);
    return size;
}
}

#define send CaptureSend
#include "SocketUtils.cpp"
#undef send
#define main ForestPickupTestServerMain
#include "../Server/server.cpp"
#undef main

namespace {
void Check(bool ok, const char* reason) {
    if (ok) return;
    std::fprintf(stderr, "FAIL: %s\n", reason);
    std::exit(1);
}

struct TestRoom {
    Room room;
    MapItemState views[2];
    int counts[2][2][3]{};

    TestRoom() {
        room.mapItems = {};
        room.mapItems.stage = 1;
        room.phase = Room::Phase::Playing;
        room.matchId = 1;
        room.moveTick = 1;
        room.gameEpoch = Clock::time_point{};
        room.nextMoveTick = room.gameEpoch + std::chrono::milliseconds(20);
        MoveTo(0, 2, 6);
        MoveTo(1, 12, 6);
        for (int i = 0; i < 2; ++i) {
            const auto socket = ::socket(AF_INET, SOCK_STREAM, 0);
            Check(socket != INVALID_SOCKET, "test socket");
            wireBySocket[socket].clear();
            room.clients.emplace_back(new Client(socket, i + 1, room.receiveEvents));
            room.slots[i] = room.clients.back().get();
            room.slots[i]->playerId = i + 1;
        }
    }

    void MoveTo(int player, int x, int y) {
        auto& p = room.players[player];
        p.position = {{x, y}, {x, y}, 0, kInputRight};
        p.direction = kInputNone;
    }

    void CopyInitialState() {
        views[0] = views[1] = room.mapItems;
    }

    std::vector<Packet> Tick(int ms) {
        room.TickMovement(room.gameEpoch + std::chrono::milliseconds(ms));
        for (auto* client : room.slots) Check(client->Flush(), "flush Room output");
        auto& first = wireBySocket[room.slots[0]->socket];
        auto& second = wireBySocket[room.slots[1]->socket];
        Check(first == second, "both clients receive the same bytes");
        ReceiveBuffer buffer;
        Check(buffer.Append(first.data(), first.size()), "buffer Room output");
        first.clear();
        second.clear();
        std::vector<Packet> output;
        for (Packet packet; buffer.TryExtract(packet) == ExtractResult::PacketReady;)
            output.push_back(std::move(packet));
        Check(!buffer.HasIncompletePacket(), "complete Room frames");
        return output;
    }

    void Apply(const Packet& packet) {
        for (int i = 0; i < 2; ++i) {
            if (packet.header.packetType == PacketType::SC_BOMB_STATE) {
                BombFrame frame;
                Check(ReadBombState(packet.payload, frame), "valid Bomb frame");
                ApplyBombOccupancy(views[i].board, frame);
            } else if (packet.header.packetType != PacketType::SC_PLAYER_STATE) {
                ItemPickup pickup;
                Check(ApplyMapItemPacket(packet, views[i], &pickup), "apply Room event in wire order");
                if (pickup.playerId && pickup.item < kCityClockItem) ++counts[i][pickup.playerId - 1][pickup.item - 1];
            }
        }
    }
    void Apply(const std::vector<Packet>& frames) {
        for (const auto& frame : frames) Apply(frame);
    }
};

void ExposureAndOccupancy() {
    TestRoom hidden;
    hidden.MoveTo(0, 5, 6); // Hold the contact cell constant to isolate exposure in one tick.
    auto& board = hidden.room.mapItems.board;
    board[6][5] = {1, 1, 1, 1};
    hidden.CopyInitialState();
    hidden.Apply(hidden.Tick(20));
    Check(board[6][5].item == 1 && hidden.counts[0][0][0] == 0, "hidden item cannot be collected");

    hidden.MoveTo(1, 4, 6);
    Check(TryPlaceBomb(hidden.room.bombs, hidden.room.nextBombId, 2, hidden.room.players[1],
        hidden.room.mapItems.abilities[1], board, 0), "Bomb adjacent to hidden item");
    hidden.Apply(hidden.Tick(40));
    const auto exposed = hidden.Tick(1060);
    Check(exposed.size() == 3 &&
          exposed[0].header.packetType == PacketType::SC_EVENT &&
          exposed[0].payload == (std::vector<std::uint8_t>{1, 0, 126, 1}) &&
          exposed[1].header.packetType == PacketType::SC_BOMB_STATE &&
          exposed[2].header.packetType == PacketType::SC_EVENT && exposed[2].payload[0] == 2,
          "expose original item, send blast, then collect");
    hidden.Apply(exposed);
    hidden.Apply(exposed.back());
    for (int i = 0; i < 2; ++i) {
        const auto& view = hidden.views[i];
        Check(view.board[6][5].obstacle == 0 && view.board[6][5].item == 0 &&
              view.abilities[0].blastRange == 2 && hidden.counts[i][0][0] == 1,
              "two receivers apply range, removal and one pickup despite repeat");
    }

    TestRoom occupied;
    occupied.MoveTo(1, 12, 6);
    auto& world = occupied.room.mapItems;
    world.board[6][12].item = 3;
    Check(TryPlaceBomb(occupied.room.bombs, occupied.room.nextBombId, 2, occupied.room.players[1],
        world.abilities[1], world.board, 0), "Bomb occupying an item cell");
    occupied.CopyInitialState();
    occupied.Apply(occupied.Tick(20));
    Check(world.board[6][12].item == 3, "planted Bomb blocks pickup");
    const auto released = occupied.Tick(1060);
    Check(released.size() == 2 && released[0].payload[0] == 5 &&
          released[1].header.packetType == PacketType::SC_BOMB_STATE, "Bomb destroys item on its own tile");
    occupied.Apply(released);
    for (int i = 0; i < 2; ++i)
        Check(occupied.views[i].board[6][12].item == 0 && occupied.counts[i][1][2] == 0,
              "both receivers remove the item without awarding it");
}

void ExposedItemDestruction() {
    TestRoom test;
    auto& room = test.room;
    auto& world = room.mapItems;
    world.board[6][5] = {1, 1, 1, 2};
    test.CopyInitialState();
    test.MoveTo(0, 4, 6);
    test.MoveTo(1, 6, 6);
    for (int i = 0; i < 2; ++i)
        Check(TryPlaceBomb(room.bombs, room.nextBombId, i + 1, room.players[i],
                          world.abilities[i], world.board, 0), "Bomb beside hidden item");
    test.Apply(test.Tick(20));
    const auto exposed = test.Tick(1060);
    test.Apply(exposed);
    Check(exposed.size() == 2 && exposed[0].payload == (std::vector<std::uint8_t>{1, 0, 126, 2}),
          "same-tick blasts break the wall and preserve the newly exposed item");
    test.Apply(test.Tick(1080));
    for (const auto& view : test.views)
        Check(view.board[6][5].obstacle == 0 && view.board[6][5].item == 2,
              "both clients retain the exposed item during the original blasts");

    Check(TryPlaceBomb(room.bombs, room.nextBombId, 1, room.players[0],
                       world.abilities[0], world.board, 1080), "place a later Bomb");
    test.Apply(test.Tick(1100));
    const auto destroyed = test.Tick(2140);
    test.Apply(destroyed);
    Check(destroyed.size() == 2 && destroyed[0].payload == (std::vector<std::uint8_t>{5, 0, 126, 2}),
          "later blast sends exactly one destruction event");
    Check(world.board[6][5].item == 0, "server removes exposed item");
    for (const auto& view : test.views)
        Check(view.board[6][5].item == 0 && view.abilities[0].bombLimit == 2,
              "both clients remove the exposed item without applying a bonus");
}

std::vector<CityHitEvent> ApplyCityFrames(TestRoom& test, const std::vector<Packet>& frames) {
    std::vector<CityHitEvent> hits;
    for (const auto& frame : frames) {
        if (frame.header.packetType != PacketType::SC_EVENT || frame.payload.empty() ||
            frame.payload[0] != kCityHitEventKind) continue;
        CityHitEvent hit;
        Check(frame.header.matchId == test.room.matchId && ReadCityHitEvent(frame.payload, hit),
              "City hit identifies current match and decodes");
        hits.push_back(hit);
    }
    test.Apply(frames);
    return hits;
}

void CityHitCodec() {
    const auto payload = EncodeCityHitEvent({2, CityHitCause::Bomb, 0x12345678});
    Check(payload == (std::vector<std::uint8_t>{7, 2, 2, 0x12, 0x34, 0x56, 0x78}), "City hit byte layout");
    CityHitEvent hit;
    Check(ReadCityHitEvent(payload, hit) && hit.playerId == 2 && hit.cause == CityHitCause::Bomb &&
          hit.sequence == 0x12345678, "City hit round trip");
    for (int value : {0, 3}) {
        auto invalid = payload;
        invalid[1] = static_cast<std::uint8_t>(value);
        Check(!ReadCityHitEvent(invalid, hit), "invalid hit player");
        invalid = payload;
        invalid[2] = static_cast<std::uint8_t>(value);
        Check(!ReadCityHitEvent(invalid, hit), "invalid hit cause");
    }
    Check(!ReadCityHitEvent({7, 1, 1, 0, 0, 0, 0}, hit), "invalid zero hit sequence");
    Check(!ReadCityHitEvent({7, 1, 1, 0, 0, 1}, hit), "invalid short hit payload");
    Check(EncodeCityHitEvent({0, CityHitCause::Bomb, 1}).empty(), "invalid hit cannot be encoded");
    Packet packet;
    packet.header.packetType = PacketType::SC_EVENT;
    packet.payload = payload;
    MapItemState state;
    for (int stage : {0, 1, 3}) {
        state.stage = stage;
        Check(!ApplyMapItemPacket(packet, state), "City hit is not accepted for other stages");
    }
    state.stage = 2;
    Check(ApplyMapItemPacket(packet, state) && state.city.remaining[1] == kCityStartSeconds,
          "valid City hit does not change persistent state");
}

void CityDamage() {
    TestRoom test;
    auto& room = test.room;
    auto& world = room.mapItems;
    world.stage = 2;
    test.MoveTo(0, 3, 7); // P1 on the track, P2 safely north of it.
    test.CopyInitialState();
    Check(ApplyCityFrames(test, test.Tick(2400)).empty(), "natural time decrease is not a hit");
    Check(world.city.remaining[0] == 28 && world.city.remaining[1] == 28, "train still short of P1");
    auto hits = ApplyCityFrames(test, test.Tick(2800));
    Check(hits.size() == 1 && hits[0].playerId == 1 && hits[0].cause == CityHitCause::Train &&
          hits[0].sequence == 1, "only P1 receives the first train hit");
    Check(world.city.remaining[0] == 23 && world.city.remaining[1] == 28, "train subtracts five seconds from P1");
    Check(ApplyCityFrames(test, test.Tick(2820)).empty(), "same train pass sends no duplicate");
    Check(world.city.remaining[0] == 23, "same train does not repeatedly hit P1");
    Check(TryPlaceBomb(room.bombs, room.nextBombId, 2, room.players[1],
                       world.abilities[1], world.board, 2820), "City Bomb");
    test.Apply(test.Tick(2840));
    hits = ApplyCityFrames(test, test.Tick(3880));
    Check(hits.size() == 1 && hits[0].playerId == 2 && hits[0].cause == CityHitCause::Bomb &&
          hits[0].sequence == 2, "only P2 receives the Bomb hit");
    Check(world.city.remaining[0] == 22 && world.city.remaining[1] == 25,
          "City Bomb deducts two seconds from the player it hits");
    Check(ApplyCityFrames(test, test.Tick(3900)).empty(), "same Bomb sends no duplicate");
    Check(world.city.remaining[1] == 25, "same blast does not deduct again");
    hits = ApplyCityFrames(test, test.Tick(9600));
    Check(hits.size() == 1 && hits[0].sequence == 3, "next train sends a new sequence");
    for (const auto& view : test.views)
        Check(view.city.remaining[0] == 11 && view.city.remaining[1] == 19,
              "next train can deduct again; both receive the same time");
}

void CitySimultaneousHitsAndEnd() {
    TestRoom test;
    auto& room = test.room;
    auto& world = room.mapItems;
    world.stage = 2;
    test.MoveTo(0, 3, 7);
    test.MoveTo(1, 3, 8);
    test.CopyInitialState();
    auto hits = ApplyCityFrames(test, test.Tick(2800));
    Check(hits.size() == 2 && hits[0].playerId == 1 && hits[1].playerId == 2 &&
          hits[0].cause == CityHitCause::Train && hits[1].cause == CityHitCause::Train &&
          hits[0].sequence == 1 && hits[1].sequence == 2, "one train hits both players");
    Check(ApplyCityFrames(test, test.Tick(2820)).empty(), "neither train victim repeats");
    Check(TryPlaceBomb(room.bombs, room.nextBombId, 1, room.players[0],
                       world.abilities[0], world.board, 2820), "City Bomb within reach of both");
    hits = ApplyCityFrames(test, test.Tick(3880));
    Check(hits.size() == 2 && hits[0].playerId == 1 && hits[1].playerId == 2 &&
          hits[0].cause == CityHitCause::Bomb && hits[1].cause == CityHitCause::Bomb &&
          hits[0].sequence == 3 && hits[1].sequence == 4, "one Bomb hits both players");
    Check(ApplyCityFrames(test, test.Tick(3900)).empty(), "neither Bomb victim repeats");

    TestRoom end;
    end.room.mapItems.stage = 2;
    end.room.mapItems.city.remaining[0] = end.room.mapItems.city.remaining[1] = 3;
    end.MoveTo(1, 3, 6);
    Check(TryPlaceBomb(end.room.bombs, end.room.nextBombId, 1, end.room.players[0],
                       end.room.mapItems.abilities[0], end.room.mapItems.board, 0), "round-ending City Bomb");
    end.CopyInitialState();
    const auto terminal = end.Tick(1060);
    Check(ApplyCityFrames(end, terminal).empty() && end.views[0].city.result == CityResult::Draw &&
          terminal.back().header.packetType == PacketType::SC_MATCH_END, "terminal damage shows the draw only");
    Check(ApplyCityFrames(end, end.Tick(1620)).empty() && end.room.bombs.empty(), "finished blast expires quietly");
}

void CityDropsAndClock() {
    TestRoom drops;
    auto& room = drops.room;
    auto& world = room.mapItems;
    world.stage = 2;
    for (auto& row : world.board) for (auto& tile : row) tile.obstacle = 2;
    for (int col = 0; col < kCityDropCount; ++col) world.board[14][col] = {};
    drops.CopyInitialState();
    drops.Apply(drops.Tick(4520));
    Check(world.city.fallingItems[0].item == 0, "no drop before the train reaches the drop point");
    drops.Apply(drops.Tick(4540));
    for (const auto& view : drops.views)
        for (const auto& item : view.city.fallingItems)
            Check(item.item && item.x == kCityDropStartX && item.y == kCityDropStartY,
                  "six items appear at the same place on both clients");
    drops.Apply(drops.Tick(4560));
    Check(drops.views[0].city.fallingItems[0].x != kCityDropStartX, "clients receive in-flight positions");
    drops.Apply(drops.Tick(5480));
    for (const auto& view : drops.views) {
        for (int col = 0; col < kCityDropCount; ++col)
            Check(view.board[14][col].item > 0 && view.board[14][col].item == world.board[14][col].item,
                  "one item per empty tile and both boards agree");
        for (const auto& item : view.city.fallingItems) Check(item.item == 0, "landing removes in-flight item");
    }
    drops.Apply(drops.Tick(5500));
    Check(world.city.fallingItems[0].item == 0, "same train does not start a second drop");

    TestRoom clock;
    auto& city = clock.room.mapItems;
    city.stage = 2;
    city.city.remaining[0] = 1;
    clock.MoveTo(0, 3, 7);
    city.board[7][3].item = kCityClockItem;
    MovingPlayer bomber;
    bomber.position.cell = bomber.position.target = {4, 7};
    Check(TryPlaceBomb(clock.room.bombs, clock.room.nextBombId, 2, bomber,
                       city.abilities[1], city.board, 0), "Bomb adjacent to clock");
    clock.CopyInitialState();
    clock.Apply(clock.Tick(1060));
    for (const auto& view : clock.views)
        Check(view.board[7][3].item == 0 && view.city.remaining[0] == 3 && view.city.remaining[1] == 29 &&
              view.city.result == CityResult::Playing && view.abilities[0].blastRange == 1,
              "clock adds five to its collector before decay and two-second Bomb damage");
    clock.Apply(clock.Tick(1080));
    Check(city.city.remaining[0] == 3, "clock bonus and Bomb damage are not applied twice");

    TestRoom clockOnly;
    clockOnly.room.mapItems.stage = 2;
    clockOnly.room.mapItems.city.remaining[0] = 10;
    clockOnly.room.mapItems.board[6][2].item = kCityClockItem;
    clockOnly.CopyInitialState();
    Check(ApplyCityFrames(clockOnly, clockOnly.Tick(1020)).empty() &&
          clockOnly.views[0].city.remaining[0] == 14, "clock pickup and decay without any hit");
}

void CityLandingAtBlast() {
    for (const int explosion : {1980, 2020, 0}) {
        TestRoom test;
        auto& room = test.room;
        auto& map = room.mapItems;
        map.stage = 2;
        const GridCell landing{5, 6};
        room.cityDropTargets[0] = landing;
        const auto end = BoardCellCenter(landing.x, landing.y);
        const double distance = std::hypot(end.x - kCityDropStartX, end.y - kCityDropStartY);
        room.cityDropStartedAt = 2000 - static_cast<int>(std::ceil(
            distance * kCityTrainStepMs / kCityDropStepPixels));
        map.city.fallingItems[0] = {kCityDropStartX, kCityDropStartY, 1};
        if (explosion) {
            MovingPlayer owner;
            owner.position.cell = owner.position.target = {4, 6};
            Check(TryPlaceBomb(room.bombs, room.nextBombId, 2, owner, map.abilities[1],
                               map.board, explosion - kBombFuseMs), "Bomb near a landing");
        } else {
            test.MoveTo(0, landing.x, landing.y);
        }
        test.CopyInitialState();
        test.Apply(test.Tick(1980));
        Check(map.city.fallingItems[0].item == 1 && map.board[6][5].item == 0, "item is still airborne");
        test.Apply(test.Tick(2000));
        Check(!map.city.fallingItems[0].item && map.board[6][5].item == (explosion ? 1 : 0),
              "landing after explosion or immediate pickup");
        test.Apply(test.Tick(2020));
        const int expectedItem = explosion == 1980 ? 1 : 0;
        for (int i = 0; i < 2; ++i)
            Check(test.views[i].board[6][5].item == expectedItem &&
                  test.counts[i][0][0] == (explosion ? 0 : 1), "both views agree on landing, blast and pickup");
    }
}

void ContestAndPlacement() {
    TestRoom test;
    auto& room = test.room;
    auto& board = room.mapItems.board;
    Check(TryPlaceBomb(room.bombs, room.nextBombId, 1, room.players[0],
        room.mapItems.abilities[0], board, 0), "initial range-one Bomb");
    room.players[0].position = {{4, 6}, {5, 6}, 950, kInputRight};
    room.players[1].position = {{6, 6}, {5, 6}, 950, kInputLeft};
    board[6][5].item = 1;
    test.CopyInitialState();
    room.pendingBombs.push_back(1);
    test.Apply(test.Tick(20));
    Check(room.players[0].position.cell.x == 5 && room.players[1].position.cell.x == 5,
          "both players reach the same item in one step");
    Check(room.bombs.size() == 2 && room.bombs[0].range == 1 && room.bombs[1].range == 2,
          "old Bomb keeps range, new Bomb uses bonus");
    Check(board[6][5].item == 0 && board[6][5].obstacle == 9, "pickup before same-step placement");
    for (int i = 0; i < 2; ++i)
        Check(test.counts[i][0][0] == 1 && test.counts[i][1][0] == 0 &&
              test.views[i].abilities[1].blastRange == 1, "only first claimant gets the item");
    test.Apply(test.Tick(40));
    Check(test.counts[0][0][0] == 1, "no second pickup after removal");
    test.Apply(test.Tick(1080));
    Check(room.bombs[0].state == 2 && room.bombs[0].left == 1 &&
          room.bombs[1].state == 2 && room.bombs[1].left == 2,
          "both actual explosions use the ranges stored at placement");
}

void CatchUpAndAbilities() {
    TestRoom test;
    auto& room = test.room;
    auto& ability = room.mapItems.abilities[1];
    ability.speed = 1.4f;
    room.players[1].position = {{4, 6}, {5, 6}, 1100, kInputRight};
    room.players[1].direction = kInputRight;
    auto& board = room.mapItems.board;
    board[6][5].item = board[6][6].item = 3;
    test.CopyInitialState();
    test.Apply(test.Tick(100)); // First step finishes col 5, next four at 1.5 finish col 6.
    Check(room.players[1].position.cell.x == 6 && room.players[1].position.progress == 0 &&
          ability.speed == 1.5f && board[6][5].item == 0 && board[6][6].item == 0,
          "catch-up consumes each intermediate cell and uses increased speed");
    for (int i = 0; i < 2; ++i)
        Check(test.counts[i][1][2] == 2 && test.views[i].abilities[1].speed == 1.5f,
              "second speed pickup still counts at cap");
    board[6][7].item = test.views[0].board[6][7].item = test.views[1].board[6][7].item = 2;
    test.MoveTo(1, 7, 6);
    test.Apply(test.Tick(120));
    Check(ability.bombLimit == 3 && test.counts[0][1][1] == 1 && test.counts[1][1][1] == 1,
          "P2 receives limit bonus");
    for (int i = 0; i < 4; ++i) {
        test.MoveTo(1, 7 + i, 6);
        room.pendingBombs.push_back(2);
        test.Apply(test.Tick(140 + 20 * i));
    }
    Check(room.bombs.size() == 3, "exactly three active Bombs after the limit bonus");
}

void ForestCombatAndFinish() {
    TestRoom combat;
    auto& room = combat.room;
    auto& forest = room.mapItems.forest;
    forest.carried[0] = 2;
    forest.deposited[0] = 4;
    room.mapItems.board[6][5].obstacle = 2;
    combat.MoveTo(0, 4, 6);
    Check(TryPlaceBomb(room.bombs, room.nextBombId, 1, room.players[0],
        room.mapItems.abilities[0], room.mapItems.board, 0), "tree Bomb");
    combat.MoveTo(0, 4, 8);
    combat.CopyInitialState();
    combat.Apply(combat.Tick(1060));
    Check(forest.carried[0] == 3 && combat.views[1].forest.carried[0] == 3 &&
          room.mapItems.board[6][5].obstacle == 2, "tree reward and carry cap");
    combat.MoveTo(0, 4, 6);
    combat.Apply(combat.Tick(1080));
    Check(forest.carried[0] == 0 && combat.views[0].forest.carried[0] == 0 &&
          combat.views[1].forest.deposited[0] == 4, "self hit clears carried apples only");

    TestRoom finish;
    auto& final = finish.room.mapItems;
    final.forest.carried[1] = 2;
    final.forest.deposited[1] = 8;
    finish.MoveTo(1, kForestBasketColumn[1], kForestBasketRow - 1);
    final.board[kForestBasketRow - 1][kForestBasketColumn[1]].item = 2;
    finish.CopyInitialState();
    const auto frames = finish.Tick(20);
    Check(frames.size() == 4 && frames[0].payload[0] == 2 &&
          frames[2].header.packetType == PacketType::SC_STATE &&
          frames[3].header.packetType == PacketType::SC_MATCH_END,
          "confirmed pickup and final Forest state precede result");
    finish.Apply(frames);
    Check(finish.room.phase == Room::Phase::Finished && finish.views[0].forest.winner == 2 &&
          finish.views[1].forest.deposited[1] == 10 && finish.counts[1][1][1] == 1,
          "Forest deposit and result remain authoritative");
    Check(finish.Tick(40).empty(), "finished game does not publish another result or pickup");
}
}

int main() {
    WSADATA data{};
    Check(WSAStartup(MAKEWORD(2, 2), &data) == 0, "Winsock startup");
    ExposureAndOccupancy();
    ExposedItemDestruction();
    CityHitCodec();
    CityDamage();
    CitySimultaneousHitsAndEnd();
    CityDropsAndClock();
    CityLandingAtBlast();
    ContestAndPlacement();
    CatchUpAndAbilities();
    ForestCombatAndFinish();
    WSACleanup();
    std::puts("PASS: Forest pickups and combat; City hit events, time, blast and landing order");
}
