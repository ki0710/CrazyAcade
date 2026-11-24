# NetworkTest packet connection

The Winsock test programs use the shared framing and I/O utilities
(port 9000, client 127.0.0.1). The test server generates one forest map with
the game's board initializer and sends its state when both clients are ready.

## Build and run

Open **x64 Native Tools Command Prompt for VS 2022** at the repository root:

```bat
NetworkTest\Common\build.cmd
```

The script builds the server, client, and standalone unit tests with MSVC
`/EHsc /W4 /std:c++14`, links `Ws2_32.lib`, and runs the unit tests. Output is
in `NetworkTest\Common\build\` (ignored executable/object files).
The server and client each compile their own entry point together with
`Packet.cpp`, `SocketUtils.cpp`, and `MapItemSync.cpp`. The server also compiles
`CrazyArcade/Board.cpp` for map generation. Do not add `PacketTests.cpp` to either
application: that test includes the socket implementation with scripted I/O.
The build does not change the CrazyArcade Visual Studio solution.

## Broadcast stability tests (November 23)

`build.cmd` also builds and runs `BroadcastTests.exe`. Like the receive-queue
tests, it includes the actual server with its entry point renamed. Only `send`
and its error lookup in the actual `SocketUtils.cpp` are substituted. Do not
link that source a second time or add this test to an application target.

The deterministic checks cover both recipients' exact FIFO wire bytes across
multiple queued packets, independent short sends and repeated WSAEWOULDBLOCK
resumption (including a packet boundary), and no resend after the queue drains.
They fill the real 64-packet Session queue in each slot order, verify only the
overflowed recipient is marked dead, and verify slot removal, departure
notifications and continued broadcasts to the survivor. Reset and zero-return
send failures are forced after a partial frame. Queued receive EOF between
broadcast and flush checks match cancellation/notification ordering; closing
connections are excluded. A full 256-event receive queue with a blocked producer
must leave Broadcast and Flush operational and preserve all input events.
A 30-second watchdog bounds deadlock regressions.

The test sockets are owned real Winsock handles without receive threads;
network outcomes are scripted, not evidence of OS TCP buffer saturation or
two-PC timing. Existing IntegrationTests and BombIntegrationTests provide live
TCP coverage. No protocol, gameplay rule, queue limit or production code changes
are made for these tests.

## Live TCP Broadcast tests (November 23)

After `build.cmd`, run in a fresh PowerShell process with port 9000 free:

```powershell
$ErrorActionPreference = 'Stop'
Add-Type -Path NetworkTest/Common/IntegrationTests.cs,NetworkTest/Common/BroadcastIntegrationTests.cs
[NetworkIntegrationTests]::RunBroadcast((Join-Path (Get-Location) 'NetworkTest/Common/build'), 3)
```

The partial test class reuses IntegrationTests' existing process lifecycle,
connection, framing, ready/loading and initial Player/Bomb comparison helpers.
`IntegrationTests.cs` can still be compiled and run alone as before. Each repeat
starts its own production Server.exe and two loopback TCP clients, alternating
the slow/disconnected slot (use at least two repeats to cover both slots).
Each socket has read/write timeouts; movement, delayed drain and departure
receive phases each have one seven-second deadline. Process waits and cleanup
reuse the existing bounded harness. Repetitions are limited to 1..20.

One client does not read during P1's traversal of the existing unobstructed City
lane. The other must receive multiple increasing-tick Player updates and reach
the edge before the slow reader resumes. The delayed reader then reads at most
three bytes at a time, with a delay once per complete packet, and compares every
recorded header identity and payload in FIFO order. A second movement starts,
then the slow connection is closed abortively with zero linger. The survivor
must receive cancellation, departure and lobby updates, keep the same socket,
and communicate with a replacement connection through a new match and movement.

This validates delayed application reads and an actual TCP reset during an
active Broadcast stream, not a client-process kill or an exact overlap with a
server send syscall. The 4096-byte requested receive buffer and small reads do
not prove OS buffer saturation, TCP segmentation, server partial sends or
WSAEWOULDBLOCK. Those send outcomes remain deterministically covered by
BroadcastTests; this live suite does not instrument or assert their occurrence.
Bomb/Ice result checks remain in the existing BombIntegrationTests suite rather
than being duplicated here. Production protocol and gameplay are unchanged.

## Final game-state verification (November 23)

Existing movement tests cover both players; BombIntegrationTests.Run covers P2
winning and a same-tick draw. It now applies each recipient's packets to a
separate test receiver world (tile obstacles/items, both player positions,
Bomb IDs/occupancy, hearts and final result). Each reconstructed world is
compared after every applied packet, in addition to the existing wire checks.
Player bodies excluding tick, Bomb bodies excluding tick/sequence, and Ice
state bodies must change between successive broadcasts of the same type.
SC_MATCH_END is intentionally allowed to repeat final SC_STATE contents once.
The initial loading Ice state and initial gameplay packets retain their contract.

The new final-state scenario reuses the same Pair and adds the missing P1-win /
P2-loss direction through three actual P2 self-hits, cleanup, unchanged NONE
inputs and bounded quiet observations. Existing BombIntegrationTests.cs still
compiles alone. To run the additional scenario in a fresh PowerShell process:

```powershell
$ErrorActionPreference = 'Stop'
Add-Type -Path NetworkTest/Common/BombIntegrationTests.cs,NetworkTest/Common/GameStateIntegrationTests.cs
$buildPath = Join-Path (Get-Location) 'NetworkTest/Common/build'
[BombIntegrationTests]::Run($buildPath)
[BombIntegrationTests]::RunFinalState($buildPath)
```

RunBroadcast also checks both players' initial positions/progress in match 2,
an empty initial Bomb list, and old-match movement/placement inputs with maximum
sequences producing no new-match updates before fresh sequence-1 gameplay.
These receivers are test models, not the GUI renderer or its production decoder.
Quiet checks are bounded observations, not proof of no duplicates indefinitely.
Live socket/read/write and phase deadlines reuse the existing harness; no
production rules or existing assertions are relaxed.

### Manual GUI check (not automated visual verification)

From the repository root, keep a PowerShell terminal running the server:

```powershell
& .\NetworkTest\Common\build\Server.exe
```

In another PowerShell terminal at the repository root, start two game windows:

```powershell
$guiDirectory = Join-Path (Get-Location) 'Output\bin_debug'
1..2 | ForEach-Object {
    Start-Process -FilePath (Join-Path $guiDirectory 'CrazyArcade.exe') -WorkingDirectory $guiDirectory -ArgumentList '--online','127.0.0.1'
}
```

The working directory preserves the game's relative sound paths. Window titles
identify P1/P2. The host selects 3 for Ice (recommended for damage/results),
1 for Forest or 2 for City. Left/Right selects each player's color; R readies
each window, C cancels before start. GUI snapshot loading sends CS_LOADED
automatically. Both online players use WASD and Space in their focused window;
key release or focus loss sends NONE. With one keyboard, switch focus between
windows to test each player; this is not simultaneous two-PC input testing.

Compare both views for identical positions, obstacles/items, Bomb placement,
explosion/removal, Ice hearts and result. A stationary Ice player can self-hit
three times to confirm the other player wins; use a fresh session for the other
direction. For a draw, move both into the same blast area for their final heart.
During an unfinished match close one window: the survivor should return to
lobby. Relaunch the closed client, ready both, and check fresh positions, no
old Bombs and normal movement. Online rematch after a completed result is not
implemented; restart clients for another completed-match scenario. Stop the
server with Enter after closing game windows. City combat is outside this
verification; GUI rendering, sound, focus behavior and perceived timing still
require observation by a person.

## Receive queue synchronization tests (November 22)

`build.cmd` also builds and runs `ReceiveQueueTests.exe`. This standalone target
includes the actual server implementation with its entry point renamed, so it
tests `ReceiveQueue` and `Client::Receive` without copying them or adding test
hooks to production. Never add this test source to the server or GUI targets.

The added checks cover:

- Critical-section exception unwinding and noncopyable/nonmovable ownership.
- Four producers and one consumer exchanging 8192 events, checking exact payloads,
  counts, capacity and FIFO order within each connection. Cross-connection order
  is intentionally unspecified.
- A full 256-event queue blocking a producer, allowing the consumer to drain it,
  resuming exactly once, and canceling a waiting push without changing queued data.
- Two barrier-released TCP senders and the real per-client receive threads,
  verifying 2048 movement/placement input payloads and one EOF per connection.
- Eight full-queue shutdown cycles with two live receivers, joining the threads
  before queue/critical-section destruction. A 45-second watchdog bounds deadlocks.

The loopback tests use ephemeral ports. They validate transport and queue behavior;
they do not apply gameplay or establish multi-PC latency. Existing
`IntegrationTests.Run` continues to check stale/duplicate sequences, movement,
disconnect/reconnect and server shutdown. Existing `BombIntegrationTests.Run`
checks placement consumption, replay rejection, damage and match results.
Its `Bomb TCP checks passed` number counts runtime `Check` calls, not fixed test
cases. Randomized map retries/route lengths, timed state updates and TCP read
chunking change that total; success requires both scenario PASS messages and
normal completion, not a particular count. The deterministic BombTests total
is reported separately.
Run both live suites using the commands below after the build. No production
queue size, protocol, game rule or existing test assertion is changed.

In separate terminals, start the server once, then two clients:

```bat
NetworkTest\Common\build\Server.exe
NetworkTest\Common\build\Client.exe
```

For the ice-map state test, start the server with `Server.exe 3`, connect two
clients, and press R in both. Both print the ice map and initial hearts 3,3.
The room's `ResolveIceHits` accepts confirmed player hits from one game update,
applies all damage, and then resolves the result once. Bomb handling must
report each bomb/player pair once, as in the existing game hit flags.
The server Bomb update supplies this confirmed hit batch. Both games apply
the server's final hearts and result, including the same-tick draw case.

Run Client.exe again for the third-client rejection test. Press Enter or
Ctrl+C in a running application to shut it down. The server keeps accepting
connections after either player leaves. A rejected client prints SC_ERROR
and exits with code 2; normal shutdown exits with code 0; local communication
or protocol failures exit with code 1. Startup connect remains a blocking TCP
connect; subsequent communication uses nonblocking sockets.

## CrazyArcade online movement and Bomb/Ice combat

Build Server.exe, Client.exe and CrazyArcade.exe from the same revision
(protocol **13**). Run Server.exe on the host, then run
`CrazyArcade.exe --online <server IPv4>` on each PC. `--online` alone uses
127.0.0.1:9000; launching without arguments keeps the local game.

The window title shows the assigned slot and readiness. The host selects
1/2/3 (Forest/City/Ice). Left and Right cycle each player's own seven colors;
the check mark follows the color confirmed by the server. R readies the local
player, and C cancels their readiness before the match starts.
The game applies the shared map snapshot before automatically sending
CS_LOADED. After both load, both players use WASD to move and Space to place a Bomb.
Releasing movement keys or losing focus sends NONE; an active tile step is
allowed to finish. Space requests one Bomb on the first key-down (autorepeat
is ignored). Placement uses the server's
last completed tile, preserves the current direction, and is blocked by
occupied cells and the owner's active Bomb limit (including visible blasts).
Bombs, obstacle changes and Ice damage/results now come from the server.
Online basic-item pickup, City combat and rematches are not implemented.

## Console movement diagnostic

Old binaries are rejected by the HELLO version check. Existing packet
numbers 1 through 16 are unchanged. The default server address is
127.0.0.1:9000; Client.exe also accepts a server IPv4 argument.

1. Start Server.exe, then Client.exe in two separate console windows.
2. Wait for SC_WELCOME assigning Player 1 and Player 2.
3. In each client, press W/A/S/D (upper or lower case) for UP/LEFT/DOWN/RIGHT.
4. Press X for NONE. No Enter is needed for movement; Enter still exits.
5. Verify server lines such as `Player 1 -> RIGHT, sequence=1` and
   `Player 2 -> UP, sequence=1`. Input can be entered at both clients concurrently.
6. Exit one client with Enter or Ctrl+C, verify SC_PEER_LEFT at the other,
   then reconnect and check that the freed slot is reused.

The client prints `Queued CS_INPUT` when it queues a valid key after WELCOME.
Each connection starts at sequence 1; unrelated keys do not advance it. Console
key repeat may send additional inputs. Releasing a key does not produce a
console key-up request: X explicitly sends NONE for this transport-only test.
Enter/Ctrl+C exits, and input received before WELCOME is not sent.

CS_INPUT has a six-byte payload: one directionMask byte, a big-endian uint32
sequence, and placeBomb uint8 (0 or 1). Only NONE=0x00, UP=0x01, DOWN=0x02, LEFT=0x04,
RIGHT=0x08 are valid; combined masks and any other payload length are rejected.
There is no player-ID field. The server uses the approved session's playerId.
Zero matchId is a diagnostic only and cannot move a player. During play, only
the current match and increasing sequences can change the intended direction.
Old match IDs are ignored; unapproved sessions, malformed input or future
match IDs receive SC_ERROR. The client stops rather than wrapping its sequence.

The server main loop advances movement every 20 ms. SC_PLAYER_STATE sends the
initial positions and subsequent changes as a pair, with the server tick.
The client applies server positions; it does not simulate movement locally.
Ice and Forest keep their existing state formats.

## Wire contract

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | packetSize, including header |
| 4 | 2 | packetType |
| 6 | 4 | matchId (0 for lobby/control, nonzero for each match) |
| 10 | packetSize - 10 | payload |

The header is exactly 10 explicitly serialized bytes, independent of C++
structure padding. All multibyte integers, including payload version/error
fields, use big-endian network byte order. Valid lengths are 10 through
1,048,576 bytes. The recv scratch buffer is 4096 bytes; it is not a packet
size limit. Unknown packet types and invalid lengths are rejected.

| Type | Value | Payload |
| --- | --- | --- |
| CS_HELLO | 1 | gameVersion uint16 (1), protocolVersion uint16 (13) |
| SC_WELCOME | 2 | playerId uint8 (1 or 2) |
| SC_ROOM_STATE | 3 | count, P1 ready, P2 ready, map, P1 color, P2 color, hostId (uint8 each) |
| SC_ERROR | 4 | errorCode uint16 |
| SC_PEER_LEFT | 5 | departed playerId uint8 (1 or 2) |
| SC_SNAPSHOT | 6 | map and basic item state |
| SC_EVENT | 7 | obstacle broken or basic item collected |
| CS_INPUT | 8 | directionMask uint8, sequence uint32, placeBomb uint8 |
| SC_STATE | 9 | Ice hearts/result, Forest apples/baskets/result, or City time/result |
| SC_MATCH_END | 10 | final Ice, Forest, or City state |
| CS_READY | 11 | ready uint8 (0 cancel, 1 ready), matchId=0 |
| CS_LOADED | 12 | empty, matchId=current snapshot ID |
| SC_MATCH_START | 13 | empty, matchId=current snapshot ID |
| SC_MATCH_CANCEL | 14 | reason uint8 (1 departure, 2 timeout, 3 unready), matchId=canceled ID |
| CS_SELECT | 15 | kind uint8 (1 map, 2 self color), value uint8; matchId=0 |
| SC_PLAYER_STATE | 16 | serverTick uint32; P1 then P2, each [x, y, targetX, targetY, progress uint16, facing] |
| SC_BOMB_STATE | 17 | serverTick uint32, Bomb sequence uint32, count uint16, full Bomb list |

SC_PLAYER_STATE uses the active match ID and exactly 18 payload bytes.
Coordinates are tile indices (x=column, y=row). Progress is measured in
1/1200 of a tile; facing uses the existing direction-mask values except NONE.

Error codes: 1 room full, 2 invalid request, 3 version mismatch, 4 HELLO timeout,
5 selection denied (recoverable; the connection remains open).
Only one CS_HELLO is permitted per connection; server-direction packets,
wrong matchId, repeated HELLO, or wrong payload lengths are invalid requests.
CS_INPUT is accepted only after HELLO approval and validates its direction mask.
A full room can reject immediately on accept. Otherwise a slot is assigned
only after validating HELLO. WELCOME precedes ROOM_STATE for a new player.
ROOM_STATE is broadcast after approval, departure, and readiness changes; PEER_LEFT identifies
the departing approved player. The client validates response direction,
state, payload length, IDs, error codes, and matchId before printing.

## Map and basic item state

Build the server and both clients together for protocol version 13. Once both
players are ready, the server queues the same initial snapshot for both.
The snapshot contains the stage byte, 400 row-major cells, and the two
players' abilities in player-ID order. Each cell contains four bytes:
obstacle, obstacle subtype, tile, and item type. A nonzero obstacle hides
the item. Ability fields are blast range and bomb limit (uint16 each) and
speed in tenths (uint8).

An ice snapshot appends three bytes: Player 1 hearts, Player 2 hearts, and
result. SC_STATE and SC_MATCH_END use those same three bytes. Hearts are 0
through 3. Results are 0 playing, 1 Player 1 win, 2 Player 2 win, 3 draw.
Both players start with three hearts. Confirmed hits remove one heart each,
and resolution occurs after all hits in the update. If both hearts reach
zero in that update, the result is a draw. A resolved result is fixed.

Protocol version 8 adds match loading/start/cancel (12-14) to lobby readiness (11), movement input (8), ice state (9), and
match result (10). Rebuild the server and both clients together.

An item event contains a kind byte, the cell ID (uint16, row * 20 + column + 1)
and the item type byte. Kind 1 clears a breakable obstacle and preserves the
hidden item (0 if empty). Kind 2 clears a collected basic item and appends
the winner's player ID (uint8) and resulting abilities. The receiver applies
the server's resulting values. The test server sends the initial snapshot.
The gameplay server can use the event encoders after authoritative obstacle
and pickup handling.

## Connection handling

The server owns its connection list and two player slots on one select event
loop. There are no concurrent reads/writes of player state. The console
control handler shares only an Interlocked-controlled stop flag. A departed
player's slot is released, and notifications are queued for remaining peers.
Up to 16 total pending/approved/rejected sockets are serviced; further TCP
connections remain in the OS listen backlog until capacity is available.
Unapproved connections have a 5-second HELLO deadline. Rejection has a bounded
2-second drain deadline so an unresponsive peer cannot occupy resources
indefinitely. The client waits at most 10 seconds for WELCOME.

Each connection owns a ReceiveBuffer and a bounded FIFO of PendingSend
objects (64 packets). ReceiveOnce reads at most 4096 bytes; TryExtract drains
all complete frames and retains incomplete headers/payloads. recv returning
0 terminates a clean connection; an incomplete frame at EOF is a protocol
error. Invalid framing poisons the parser and ends the session.

PendingSend preserves the partial-send cursor. On WSAEWOULDBLOCK the queue
is retained and resumed on select writability; packet bytes never interleave.
A queue overflow or socket error ends the affected connection. Error responses
are flushed before send-side shutdown; in-flight input is drained to avoid
losing SC_ERROR to a TCP reset. All sockets close before WSACleanup.
The select timeout (100 ms) allows console shutdown and deadline checks.
There are no sleep calls or busy retry loops.

## Automated verification

After building, run this from PowerShell at the repository root:

```powershell
Add-Type -Path NetworkTest/Common/IntegrationTests.cs
[NetworkIntegrationTests]::Run((Join-Path (Get-Location) 'NetworkTest/Common/build'))
```

Use a new PowerShell process after changing the test source, since Add-Type
cannot replace an already loaded type. Port 9000 must be free. The harness
starts and cleans up only its own application processes and uses redirected
stdin for normal Enter shutdown.

Actual executable tests cover two concurrent clients and distinct IDs,
ROOM_STATE at both clients, third-client SC_ERROR, normal client departure,
PEER_LEFT, slot reuse, renewed room broadcasts, game/protocol version mismatch,
simultaneous console movement inputs, independent sequences and Session IDs,
invalid movement masks/lengths/matchId and input before HELLO approval,
invalid matchId/direction/payload/length/type, segmented writes, coalesced
repeated requests, truncated EOF, handshake timeout, and normal server shutdown
with a connected client. Segmented writes alone do not force TCP segmentation;
the unit tests deterministically check every two-part frame split position.

PacketTests checks exact wire bytes, split/merged frames, maximum-size frames,
all five input directions, uint32 sequence round trips, all 256 direction-byte
values, invalid input sizes, and every split of the 16-byte CS_INPUT frame,
invalid lengths/types, short sends, WSAEWOULDBLOCK resumption, 4096-byte recv,
EOF/truncated EOF, and socket errors. Short sends and would-block are forced
using scripted Winsock substitutes in the actual SocketUtils implementation;
the integration harness does not claim to force those conditions on TCP.

## Lobby ready/cancel test

Build both applications together for protocol version 13. Versions 1 through 12
are rejected at HELLO because the ROOM_STATE/selection contract changed. Existing packet
numbers 1 through 10 are unchanged; CS_READY is 11.

After WELCOME, press R/r to ready and C/c to cancel, without Enter. W/A/S/D,
X, Enter and Ctrl+C retain their existing behavior. ROOM_STATE prints, for
example, `SC_ROOM_STATE: count=2 ready=1,0` (Player 1 ready, Player 2 unready).
Both clients initially show `ready=0,0`; R at Player 1 produces `1,0`, C
produces `0,0`. Ready Player 1 again, exit it, and reconnect: the reused slot
must show `0,0` at both clients. Both ready players now enter the loading flow described below.

CS_READY requires an approved Player ID 1 or 2, matchId=0, exactly one byte,
and value 0 or 1. Invalid requests use BadRequest and close the connection.
Repeated requests for the current value are accepted without a broadcast.
SC_ROOM_STATE is exactly seven bytes `[count, player1Ready, player2Ready, map, player1Color, player2Color, hostId]`;
readiness values must be 0 or 1. An empty slot is always reported as unready.
Readiness belongs to the connection and is reset on approval, including slot reuse.

The integration harness checks both players' ready/cancel inputs, broadcasts
at both real clients, ready-player departure/replacement, invalid CS_READY
lengths/values/matchId and requests before approval. PacketTests checks every
ready byte value, exact CS_READY wire bytes and every split, plus ROOM_STATE
payload lengths and value bounds using the client's shared validator.

## Match loading and start (protocol 8)

No existing CS_LOADED, SC_MATCH_START or countdown contract was present in the
repository. This transport test starts immediately after both loading replies;
there is no countdown. Packet numbers 1-14 remain unchanged. The expanded
ROOM_STATE is described below. Older binaries are rejected at HELLO. Rebuild both applications.

1. Start Server.exe and two Client.exe processes. Both begin unready.
2. Press R in one client: the room waits, without sending a snapshot.
3. Press R in the other: both receive SC_SNAPSHOT with the same nonzero matchId.
4. Press L/l in one client after its snapshot is printed. Repeating L cannot
   complete the other player's loading. No start is sent yet.
5. Press L in the other client within 10 seconds of loading beginning. Each
   client prints SC_MATCH_START with the same matchId exactly once.
6. To test cancellation, reconnect as needed, ready both clients, then exit one
   during loading, press C, or let 10 seconds elapse without both loading replies.
   Remaining clients print SC_MATCH_CANCEL and ROOM_STATE with readiness reset.
   Reconnect an absent player, press R in both, and L in both to retry.

SC_SNAPSHOT keeps the existing MapItemSync payload, including ice hearts; only
its header now carries the match ID. The snapshot is generated once per attempt
and broadcast unchanged to both clients. SC_STATE/SC_EVENT/SC_MATCH_END also use
that ID. The client validates it before applying state. L is deliberately manual
so the two-client loading barrier and timeout can be tested. It does not claim
to measure rendering or game asset loading. W/A/S/D/X use matchId=0 before
start and the current match ID after start.

The server has Lobby, Loading and Playing phases. Loading completion is a bool
per approved connection, not a reply count. Starting is deferred until after
disconnect handling on the select loop; the deadline is checked before start.
Match IDs increase for this server lifetime and are never reused. A well-formed
LOADED for an older/canceled/started match is ignored, even during a new attempt.
Zero/future IDs, nonempty payloads and unapproved senders receive BadRequest.
The server refuses ID exhaustion rather than wrapping. Ready repeats are harmless.
C during Loading cancels the pending match for both clients and clears both
loaded flags. Only the sender loses their ready flag. Ready commands during
Playing are ignored.
Departure during Playing also returns survivors to the lobby in this console test.

The integration harness additionally checks one ready waits, one/duplicate
LOADED waits, start once per client, loading departure, real 10-second timeout,
successful retry, byte-identical snapshots, stale replies in the lobby and next
attempt, start IDs/empty payloads, and malformed LOADED requests. PacketTests
checks new packet framing, nonzero match IDs and every two-part split.

## Map and character color selection (protocol 8)

This implements the page-30 rules supplied in the task. The plan PDF is outside
the repository. Values come from CrazyArcade.cpp's selected_stage branches and
Player.cpp's colorHueTable indices; no new game map/color IDs are introduced.

CS_SELECT=15 has exactly two payload bytes `[kind, value]` and matchId=0.
Kind 1 selects a map: 1 Forest, 2 City, 3 Ice. Kind 2 selects the sender's own
color: 0 red, 1 orange, 2 yellow, 3 green, 4 mint, 5 blue, 6 purple.
There is no player-ID field; the approved connection supplies identity.
SC_ROOM_STATE=3 now contains seven uint8 fields in this order:
`[count, P1 ready, P2 ready, map, P1 color, P2 color, hostId]`.
The original count/ready prefix remains. Host ID is 0 for an empty room and 1
or 2 otherwise. Vacant slots report color 0 and unready; new clients start at
color 0. Both players may select the same color, as the existing game permits.

Only the earliest connected, currently approved player can change the map.
The server retains TCP accept order, so after that player leaves, the surviving
player has permission even if a newcomer reuses Player 1's slot. The selected
map persists across departures. Selection is allowed only in Lobby. Changing
the map clears both ready flags; changing color clears only the sender's flag.
Selecting an unchanged value preserves readiness and does not broadcast.
Loading generates the selected map with the existing Forest/City/Ice initializer.
The color choices stay frozen in ROOM_STATE through loading and start; this
console task does not change game rendering or the MapItemSync snapshot payload.

Invalid lengths, kinds, values, matchId, or unapproved senders receive the existing
BadRequest and disconnect. A valid request without map permission, or outside
Lobby, receives SC_ERROR code 5 (Selection denied) without disconnecting or
changing room state. Both server and client use protocol version 12 so version-7
clients cannot interpret the expanded ROOM_STATE accidentally.

After WELCOME, use these keys without Enter (letters accept either case):

| Keys | Selection |
| --- | --- |
| 1 / 2 / 3 | Forest / City / Ice map (host only) |
| Z / V / B / N / M / J / K | Red / Orange / Yellow / Green / Mint / Blue / Purple (self) |
| R / C / L | Ready / cancel / loaded after snapshot (existing flow) |

Example output: `SC_ROOM_STATE: count=2 ready=0,1 map=2 colors=2,5 host=1`.
Build and launch the applications with the commands above. Change selections
before both clients ready, compare both outputs, then press R at both and L
at both after receiving snapshots to verify the selected map starts.

Selection tests run in IntegrationTests.Run alongside all existing tests. They
cover identical client displays, guest denial with connection retained, both
readiness reset rules, unchanged choices, host transfer/slot reuse, selection
denial during Loading, and City/Ice loading/start. A combined ready/select write
also exercises clearing both flags before loading. As with other live TCP tests,
one write alone does not guarantee a specific receive segmentation. Unit tests
cover all 256 value/kind bytes, bad lengths, ROOM_STATE bounds, exact CS_SELECT
wire bytes, and every two-part frame split.


## Forest apples and baskets (protocol 12)

Forest players carry up to three apples. A new tree hit awards its Bomb owner
one apple when there is room. A player hit clears carried apples and preserves
their basket total. Each Bomb's tree directions and player hits are processed
once as part of the existing server combat update.

The server checks each player's own basket after gains and hits. P1's basket
is at (8,19) and P2's at (11,19). Reaching the tile above or either side of that
basket automatically transfers all carried apples. A total of ten or more wins.
When both players qualify within the same update, the server records the first
deposit it processes (P1, then P2) and sends both final totals before the result.

The Forest snapshot appends the 11-byte Forest state, for 1622 payload bytes.
SC_STATE and SC_MATCH_END use the same Forest layout. Player order is P1, P2:
two big-endian uint32 carried counts, two uint8 basket totals, and one uint8
winner (0 playing, 1 P1, 2 P2). Forest SC_EVENT gain/loss keeps the existing
format. Ice and Forest layouts are unchanged in protocol 13.

## Bomb state, time and final results (protocol 11)

Each SC_BOMB_STATE entry is 17 bytes: bombId uint32, owner uint8,
column uint8, row uint8, state uint8 (1 planted, 2 exploding),
left/right/up/down reach uint16 each, and displayTime uint8.
All multibyte values are big endian. Match identity stays in the header.
The full list is sent when its state or server-supplied animation frame changes.
Missing IDs are removed; a new ID owns a same-cell replacement independently.
The Bomb sequence is separate from SC_PLAYER_STATE's tick. BombView contains
no handles, pixels or C++ object bytes. Clients validate length, IDs, cells,
state and displayTime before replacing their view.

The server consumes accepted placement events once at a game update, even
when a placement fails. A later NONE never overwrites these events.
Only the approved player's current match and increasing input sequence apply.
Bomb IDs originate on the server, starting at 1 for each match.

The fuse deadline is 1050 ms and removal is 550 ms after that deadline,
using steady_clock elapsed milliseconds checked by the 20 ms game update.
The local game's 21/11 callbacks at 50 ms provide these nominal durations.
If a long server stall spans a blast's whole lifetime, its Board transition
is still resolved once, then the expired blast is removed without retroactive
damage against the players' new positions. Client paint/timer counts cannot
start, delay or finish a blast.

The shared Board uses 9 for planted Bombs. Obstacle 1 is broken in the server
and its preassigned hidden item is retained; the existing SC_EVENT carries
that change. Bomb occupancy uses the Bomb list, not an obstacle-break event.
Obstacle 2 stops an arm without destruction and awards apples only in Forest.
Bombs (9) and 3..6 keep their existing pass-through rule, without chain reactions.
Players can escape their own planted tile and cannot re-enter it until explosion.
Each Bomb can hit each player once; independent Bombs each cause damage.

All new hits in one update reach the existing Ice rules before result resolution.
On finish, final Player/Board/Bomb state precedes SC_STATE and SC_MATCH_END.
The server's Finished phase blocks new movement, placement and damage.
Unexploded Bombs are canceled and visible blasts expire without damage; Bomb
cleanup packets remain valid after the result. The UI applies the same final
hearts and game_winner, redraws the result, and does not advertise a local R
restart as an online rematch. Cancellation/disconnect clears the online view.

Run the existing integration suite first, then these additional live checks:

```powershell
Add-Type -Path NetworkTest/Common/BombIntegrationTests.cs
[BombIntegrationTests]::Run((Join-Path (Get-Location) 'NetworkTest/Common/build'))
```

BombTests.exe covers deterministic deadlines (including stalled updates),
placement/caps, Board occupancy and item preservation, all arms/edges,
moving hit cells, owner/opponent deduplication, Ice results, and codecs.
The live suite compares both recipients' bytes, checks consumed failed and
replayed requests, repeated self damage and one match end, and walks P2
through an actual walkable Ice route to test same-tick opponent hits/draws.
It never changes the server world through a testing packet.

## Project Progress Report notes: November 18

### Server state and reuse

- Related plan: 3-4, 3-5, 5-2, 5-3, 5-4, 8-1.
- Original plan: GameWorld/BombState own authoritative combat; reuse the old
  Bomb operations after separating simulation from rendering.
- Implementation: the existing Room owns a small pure ServerBomb list and
  uses BombRules. The Win32 Bomb gains only a display-state adapter.
- Reason: the integrated server already owns Player and Board state in Room;
  linking the Win32 implementation would also import rendering dependencies.
- Benefit: authoritative combat fits the current server without a GameWorld
  migration; existing local Bomb function bodies remain unchanged.
- Caution: pure server rules and local rules are separate implementations and
  must be checked together when a combat rule changes.
- Integration/follow-up: Room, BombRules.h, BombSync.h and Bomb::ApplyOnlineState
  are affected. Player movement, IceState, Board and receiver threads are reused.
- Report text: Server Bomb state was integrated into the existing Room instead
  of introducing the full planned GameWorld. A small display adapter lets the
  actual game render authoritative Bombs while retaining its local game path.

### Synchronization packet

- Related plan: 3-4, 5-6, 6.
- Original plan: synchronize authoritative state/events through the planned
  SC_STATE/SC_EVENT flow.
- Implementation: append one CS_INPUT placement flag and add SC_BOMB_STATE
  (17), a complete Bomb list with its own sequence; use protocol 11.
- Reason: the existing Ice/Forest SC_STATE decoders have fixed payload layouts.
- Benefit: those layouts and the MapItemSync event format remain compatible
  within the new build; the Bomb list also defines removal and same-cell IDs.
- Caution: protocol 10 binaries cannot participate. This is a Bomb snapshot,
  not reconnect support or full-world resynchronization.
- Integration/follow-up: Protocol/Packet, server, console client, GUI and tests
  must be rebuilt together. Future world snapshots must include Bombs explicitly.
- Report text: A dedicated Bomb-state packet preserves the existing Ice/Forest
  state formats. Match identity and an independent Bomb sequence keep Bomb
  replacement and removal distinct from Player movement ticks.

### Concrete server timing

- Related plan: 5-3, 5-5, 7.
- Original plan: move combat decisions to server updates; the old local Bomb
  implementation uses a 50 ms callback counter.
- Implementation: the existing 20 ms scheduler checks steady_clock deadlines,
  with a 1050 ms fuse and a further 550 ms blast lifetime.
- Reason: callback counts depend on scheduling and paint load. Absolute server
  deadlines preserve the nominal local durations independently of the clients.
- Benefit: both clients display the same server transition and damage result.
- Caution: a stall spanning the complete blast lifetime resolves Board changes
  but does not apply historical damage to current Player positions.
- Integration/follow-up: only Room and Bomb rules decide time; the client uses
  supplied display frames. Receive/UI queues remain; no TCP_NODELAY, event wake,
  batching or prediction changes are included. Multi-PC timing is tested later.
- Report text: Server updates now check real elapsed deadlines rather than
  replaying local timer counts. This implements the planned server authority
  while preserving the local Bomb's nominal fuse and blast durations.

### Schedule

- Related plan: 8-2.
- Original plan: Bomb state networking on November 17 and joint testing on
  November 18.
- Implementation: actual Player synchronization was completed November 17;
  Bomb/Ice integration moved to November 18 and the two-PC test to November 19.
- Reason: actual-game movement synchronization was completed before connecting
  authoritative Bomb hits and results to that Player state.
- Benefit: combat integration uses verified Player cells and movement progress.
- Caution: two-PC latency, simultaneous input and user-perceived timing still
  need the joint test; the one-PC tests do not establish those results.
- Integration/follow-up: no packet or code change follows from the schedule.
  Forest basket/results, City rules, resync and rematches retain their later scope.
- Report text: Actual-game movement synchronization required November 17.
  Bomb/Ice integration was completed November 18, with two-PC joint testing
  rescheduled to November 19.

## Project Progress Report notes: November 19

- Related plan: 3-3, 3-5, 6, 8-2.
- Original plan: each client controls its own Player and sends its own ready
  status. Each Player owns its color; the host owns map selection. The schedule
  did not list a separate pass for usability corrections after the joint test.
- Joint test findings: movement, Bombs, Ice damage and wins/losses worked across
  two PCs. The lobby check used local positions, online controls differed by
  slot, draw text was off center, and canceling during Loading cleared both
  ready flags. A live draw was not confirmed in that joint test.
- Changes: Left/Right cycle seven colors, and the check follows the local
  Player's server-confirmed color. Both online slots use WASD/Space. Each online
  result uses the visible canvas center. Canceling a pending match preserves
  the other Player's readiness. Color changes clear only the sender's ready
  flag, and host map changes clear both flags.
- Reason and benefit: the UI reflects the room state and controls behave the
  same on either PC, while the server keeps the existing Player/host authority.
- Caution: C can cancel readiness until the server starts the match. Canceling
  during Loading invalidates that attempt and both load confirmations. The
  retained ready flag does not count as confirmation of the next snapshot.
- Schedule impact: this pass follows the November 19 joint test and precedes
  Forest/City work. The observed input lag is tracked separately.
- Report text: The November 19 two-PC test verified core network gameplay.
  The follow-up standardized online controls, fixed server-confirmed color
  indication and result alignment, and corrected readiness cancellation before
  the next theme work.

## Forest basic item pickup (November 21 work completed November 23)

In Forest, the game tick checks both players' last completed cells after each
movement step. It checks again after Bomb updates for newly exposed or released
items. P1 is checked first if both reach a cell in the same step. The existing
CollectBasicItem removes an exposed item and updates the owner's abilities:
1 increases blast range, 2 increases Bomb limit, and 3 adds 0.1 speed up to 1.5.

The server uses the existing protocol 12 SC_EVENT kind 2 with the cell, item,
owner and absolute abilities. Pickups from movement precede later changes to
Bomb occupancy. Break events retain the pre-pickup item and late pickups follow
exposure and Bomb release. Both clients apply the event to the Board and to the
owner's displayed Player. The HUD counts actual removals, including speed
items at the cap. Duplicate and earlier-match events do not count, and a new
match starts with zero pickup counts.

`build.cmd` runs ForestPickupTests.exe against the production Room and codec
with deterministic ticks. The live test uses the stock Server.exe, two TCP
streams and real movement/Bomb inputs to expose and collect map items:

```powershell
Add-Type -Path NetworkTest/Common/IntegrationTests.cs,NetworkTest/Common/BroadcastIntegrationTests.cs,NetworkTest/Common/ForestPickupIntegrationTests.cs
[NetworkIntegrationTests]::RunForestPickups((Join-Path (Get-Location) 'NetworkTest/Common/build'))
```

### Project Progress Report notes

- Related plan: 2-6, 2-8, 3-5, 5-5, 5-6, 6, 8-2.
- The November 21 Forest combat, abilities and apple display work was delayed
  by a personal schedule. The November 22 shared input queue and November 23
  Broadcast verification went ahead to keep the team schedule moving.
- The integrated November 20 work already provides apple/basket display,
  deposits and results. This follow-up connects only basic pickups and
  play/display abilities, reusing the existing queues and packet format.
- The plan describes tree hits producing apples and describes loss from an
  opponent's Bomb. The existing game awards the Bomb owner directly on a tree
  hit and clears carried apples on own or opponent hits. Neither rule changed.
- The event carries changes for a live match. A later recovery snapshot will
  need the actual accumulated pickup counts to restore the HUD. Those counts
  cannot be inferred from capped abilities. The November 25 Forest joint test
  can use the existing protocol 12 binaries rebuilt from this revision.

## City survival clock (protocol 13)

Both players start with 30 seconds. The server starts the clock on match start
and subtracts whole seconds from its steady-clock elapsed time, even with no
input. When both reach zero in the same update, it broadcasts a draw and
stops the match. The client displays the server's numbers.

City SC_SNAPSHOT appends the 5-byte City state (total 1616 payload bytes).
City SC_STATE and SC_MATCH_END use the same layout: two big-endian uint16
remaining times in seconds, followed by result (0 playing, 1/2 winner, 3 draw).
