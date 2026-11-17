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
The console connection program currently supplies the initial map; the
combat caller provides the confirmed hit batch.

Run Client.exe again for the third-client rejection test. Press Enter or
Ctrl+C in a running application to shut it down. The server keeps accepting
connections after either player leaves. A rejected client prints SC_ERROR
and exits with code 2; normal shutdown exits with code 0; local communication
or protocol failures exit with code 1. Startup connect remains a blocking TCP
connect; subsequent communication uses nonblocking sockets.

## CrazyArcade online movement

Build Server.exe, Client.exe and CrazyArcade.exe from the same revision
(protocol **10**). Run Server.exe on the host, then run
`CrazyArcade.exe --online <server IPv4>` on each PC. `--online` alone uses
127.0.0.1:9000; launching without arguments keeps the local game.

The window title shows the assigned slot and readiness. The host selects
1/2/3 (Forest/City/Ice); either player may select its own color with
Z/V/B/N/M/J/K. Both press R to ready (C cancels before start).
The game applies the shared map snapshot before automatically sending
CS_LOADED. After both load, P1 uses WASD and P2 uses the arrow keys.
Releasing movement keys or losing focus sends NONE; an active tile step is
allowed to finish. Online mode in this revision synchronizes movement only.

## Console movement diagnostic

Old binaries are rejected by the HELLO version check. Existing packet
numbers 1 through 15 are unchanged. The default server address is
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

CS_INPUT has a five-byte payload: one directionMask byte followed by a
big-endian uint32 sequence. Only NONE=0x00, UP=0x01, DOWN=0x02, LEFT=0x04,
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
| CS_HELLO | 1 | gameVersion uint16 (1), protocolVersion uint16 (10) |
| SC_WELCOME | 2 | playerId uint8 (1 or 2) |
| SC_ROOM_STATE | 3 | count, P1 ready, P2 ready, map, P1 color, P2 color, hostId (uint8 each) |
| SC_ERROR | 4 | errorCode uint16 |
| SC_PEER_LEFT | 5 | departed playerId uint8 (1 or 2) |
| SC_SNAPSHOT | 6 | map and basic item state |
| SC_EVENT | 7 | obstacle broken or basic item collected |
| CS_INPUT | 8 | directionMask uint8, sequence uint32 |
| SC_STATE | 9 | ice hearts/result or Forest carried apples |
| SC_MATCH_END | 10 | final ice hearts and result |
| CS_READY | 11 | ready uint8 (0 cancel, 1 ready), matchId=0 |
| CS_LOADED | 12 | empty, matchId=current snapshot ID |
| SC_MATCH_START | 13 | empty, matchId=current snapshot ID |
| SC_MATCH_CANCEL | 14 | reason uint8 (1 departure, 2 timeout, 3 unready), matchId=canceled ID |
| CS_SELECT | 15 | kind uint8 (1 map, 2 self color), value uint8; matchId=0 |
| SC_PLAYER_STATE | 16 | serverTick uint32; P1 then P2, each [x, y, targetX, targetY, progress uint16, facing] |

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

Build the server and both clients together for protocol version 9. Once both
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
values, invalid input sizes, and every split of the 15-byte CS_INPUT frame,
invalid lengths/types, short sends, WSAEWOULDBLOCK resumption, 4096-byte recv,
EOF/truncated EOF, and socket errors. Short sends and would-block are forced
using scripted Winsock substitutes in the actual SocketUtils implementation;
the integration harness does not claim to force those conditions on TCP.

## Lobby ready/cancel test

Build both applications together for protocol version 9. Versions 1 through 8
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
The server refuses ID exhaustion rather than wrapping. Ready repeats are harmless;
C during Loading cancels for both. Ready commands during Playing are ignored.
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
changing room state. Both server and client use protocol version 9 so version-7
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


## Forest carried apples (protocol 9)

Forest snapshots append two big-endian uint32 carried-apple counts (P1, P2),
making the payload 1619 bytes. Counts are nonnegative values representable by
the game's int. Forest SC_STATE carries these same eight bytes. City and ice
payloads keep their existing layouts; rebuild the server and both clients.

SC_EVENT kind 3 means an apple was gained and kind 4 means all carried apples
were lost. Both contain `[kind, playerId, carried uint32]`, six bytes total.
Clients apply the server's absolute count, rather than adding an event twice.

The game and server use the same CollectForestApple/LoseForestApples rules.
A new tree hit gives its bomb owner one apple immediately, using the existing
bomb direction flags to avoid repeated awards. Own/opponent hits clear only
the victim's carried count; they do not transfer apples or change basket totals
or bomb capacity. Existing basket handling is not changed in this work.

The combat loop calls Room::ResolveForestChanges with newly confirmed tree
owners and newly hit players for one update, after deduplicating each bomb's
tree directions/player hits. The room applies gains before losses, sends the
related events and then the final carried counts. It only runs during forest
play. The current console server supplies loading/start and initial counts;
the combat caller still supplies confirmed hits, as for ResolveIceHits.
