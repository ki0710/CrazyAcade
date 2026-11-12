# NetworkTest packet connection

The Winsock test programs use the shared framing and I/O utilities
(port 9000, client 127.0.0.1). The test server generates one forest map with
the game's board initializer and sends its state to both approved clients.

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

Run Client.exe again for the third-client rejection test. Press Enter or
Ctrl+C in a running application to shut it down. The server keeps accepting
connections after either player leaves. A rejected client prints SC_ERROR
and exits with code 2; normal shutdown exits with code 0; local communication
or protocol failures exit with code 1. Startup connect remains a blocking TCP
connect; subsequent communication uses nonblocking sockets.

## TCP movement input test

Build the server and clients together for protocol version **3**; version 2
binaries are rejected by the existing HELLO version check. Existing packet
numbers 1 through 7 are unchanged. The server address remains 127.0.0.1:9000.

1. Start Server.exe, then Client.exe in two separate console windows.
2. Wait for SC_WELCOME assigning Player 1 and Player 2 and for both map snapshots.
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
There is no player-ID field. The server uses the approved Session's playerId,
requires matchId=0, and prints the received sequence without ordering/replay
correction. Invalid input receives SC_ERROR (invalid request) and closes that
connection. The client stops rather than wrapping its sequence after uint32 max.

This test does not call HandlePlayerMovement, change Player or Board positions,
send SC_STATE, or connect CrazyArcade's keyboard input to TCP.

## Wire contract

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | packetSize, including header |
| 4 | 2 | packetType |
| 6 | 4 | matchId (0 throughout this stage) |
| 10 | packetSize - 10 | payload |

The header is exactly 10 explicitly serialized bytes, independent of C++
structure padding. All multibyte integers, including payload version/error
fields, use big-endian network byte order. Valid lengths are 10 through
1,048,576 bytes. The recv scratch buffer is 4096 bytes; it is not a packet
size limit. Unknown packet types and invalid lengths are rejected.

| Type | Value | Payload |
| --- | --- | --- |
| CS_HELLO | 1 | gameVersion uint16 (1), protocolVersion uint16 (3) |
| SC_WELCOME | 2 | playerId uint8 (1 or 2) |
| SC_ROOM_STATE | 3 | approved player count uint8 (0 to 2) |
| SC_ERROR | 4 | errorCode uint16 |
| SC_PEER_LEFT | 5 | departed playerId uint8 (1 or 2) |
| SC_SNAPSHOT | 6 | map and basic item state |
| SC_EVENT | 7 | obstacle broken or basic item collected |
| CS_INPUT | 8 | directionMask uint8, sequence uint32 |

Error codes: 1 room full, 2 invalid request, 3 version mismatch, 4 HELLO timeout.
Only one CS_HELLO is permitted per connection; server-direction packets,
nonzero matchId, repeated HELLO, or wrong payload lengths are invalid requests.
CS_INPUT is accepted only after HELLO approval and validates its direction mask.
A full room can reject immediately on accept. Otherwise a slot is assigned
only after validating HELLO. WELCOME precedes ROOM_STATE for a new player.
ROOM_STATE is broadcast after approval and departure; PEER_LEFT identifies
the departing approved player. The client validates response direction,
state, payload length, IDs, error codes, and matchId before printing.

## Map and basic item state

Build the server and both clients together for protocol version 3. Once two
players are approved, the server queues the same snapshot for both.
The snapshot contains the stage byte, 400 row-major cells, and the two
players' abilities in player-ID order. Each cell contains four bytes:
obstacle, obstacle subtype, tile, and item type. A nonzero obstacle hides
the item. Ability fields are blast range and bomb limit (uint16 each) and
speed in tenths (uint8).

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
