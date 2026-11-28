// Compile with IntegrationTests.cs and BroadcastIntegrationTests.cs.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net.Sockets;

public static partial class NetworkIntegrationTests
{
    sealed class ForestPickupPair
    {
        readonly NetworkStream[] peers;
        readonly uint[] sequence = new uint[2];
        readonly int[] obstacles = new int[400], items = new int[400];
        readonly List<byte[]> pickups = new List<byte[]>();
        readonly int[][] abilities = { new int[] {1, 2, 10}, new int[] {1, 2, 10} };
        byte[] positions;

        public ForestPickupPair(NetworkStream a, NetworkStream b)
        {
            peers = new[] { a, b };
            Send(a, 1, 0, 0, 1, 0, 17); Receive(a, 2); Receive(a, 3);
            Send(b, 1, 0, 0, 1, 0, 17); Receive(b, 2); Receive(b, 3); Receive(a, 3);
            Send(a, 11, 0, 1); Receive(a, 3); Receive(b, 3);
            Send(b, 11, 0, 1); Receive(a, 3); Receive(b, 3);
            var map = Receive(a, 6, 1);
            Check(map.Length == 1622 && map[0] == 1 &&
                  Convert.ToBase64String(map) == Convert.ToBase64String(Receive(b, 6, 1)), "Forest snapshot");
            for (int i = 0; i < 400; ++i) {
                obstacles[i] = map[1 + i * 4];
                items[i] = map[4 + i * 4];
            }
            Send(a, 12, 1); Send(b, 12, 1);
            Receive(a, 13, 1); Receive(b, 13, 1);
            positions = ReadSamePlayers(a, b, 1);
        }

        public int FindItem(int player, int requiredItem)
        {
            for (int x = 7 + 4 * player; x <= 8 + 4 * player; ++x) {
                int cell = 17 * 20 + x;
                if (obstacles[cell + 20] == 0 && obstacles[cell] == 1 && items[cell] != 0 &&
                    (requiredItem == 0 || items[cell] == requiredItem)) return cell;
            }
            return -1;
        }

        public bool ContestPickup()
        {
            int x = items[17 * 20 + 8] != 0 ? 8 : items[17 * 20 + 11] != 0 ? 11 : -1;
            if (x < 0) return false;
            int cell = 17 * 20 + x;
            SendInput(0, 0, true); SendInput(1, 0, true);
            ReadUntil(() => obstacles[18 * 20 + 9] == 0 && obstacles[18 * 20 + 10] == 0 &&
                            obstacles[cell] == 0);
            for (int p = 0; p < 2; ++p)
                while (X(p) != x) MoveOne(p, X(p) < x ? (byte)8 : (byte)4);
            int prior = pickups.Count;
            SendInput(0, 1); SendInput(1, 1);
            ReadUntil(() => Y(0) == 17 && Y(1) == 17 && pickups.Count > prior);
            SendInput(0, 0); SendInput(1, 0);
            Check(pickups.Count == prior + 1 && items[cell] == 0 &&
                  Word(pickups[prior], 1) == cell + 1, "one winner for simultaneous approaches");
            Console.WriteLine("PASS: both live players entered ({0},17), with one pickup sent to both.", x);
            return true;
        }

        void SendInput(int player, byte direction, bool bomb = false)
        {
            Send(peers[player], 8, 1, Input(direction, ++sequence[player], bomb));
        }
        static int Word(byte[] b, int at) { return (b[at] << 8) | b[at + 1]; }
        int X(int player) { return positions[4 + player * 7]; }
        int Y(int player) { return positions[5 + player * 7]; }
        int Progress(int player) { return Word(positions, 8 + player * 7); }
        void ReadUntil(Func<bool> done)
        {
            var timer = Stopwatch.StartNew();
            while (!done()) {
                var frame = BroadcastRead(peers[0], timer);
                BroadcastSame(frame, BroadcastRead(peers[1], timer));
                Check(frame.Match == 1, "expected active Forest match");
                var data = frame.Payload;
                if (frame.Type == 16) positions = data;
                if (frame.Type != 7) continue;
                if (data[0] != 1 && data[0] != 2) continue;
                int cell = Word(data, 1) - 1;
                if (data[0] == 1) {
                    Check(data.Length == 4 && obstacles[cell] == 1 &&
                          items[cell] == data[3], "preserve hidden item in exposure event");
                    obstacles[cell] = 0;
                    continue;
                }
                Check(data.Length == 10 && data[4] >= 1 && data[4] <= 2 &&
                      obstacles[cell] == 0 && items[cell] == data[3] && data[3] != 0,
                      "one pickup only, after exposure");
                items[cell] = 0;
                int p = data[4] - 1;
                int item = data[3] - 1;
                abilities[p][item] = item == 2 ? Math.Min(15, abilities[p][item] + 1) : abilities[p][item] + 1;
                Check(Word(data, 5) == abilities[p][0] && Word(data, 7) == abilities[p][1] &&
                      data[9] == abilities[p][2], "server's absolute ability after pickup");
                pickups.Add(data);
            }
        }

        void MoveOne(int player, byte direction, int expectedStep = 0)
        {
            int x = X(player) + (direction == 8 ? 1 : direction == 4 ? -1 : 0);
            int y = Y(player) + (direction == 1 ? -1 : direction == 2 ? 1 : 0);
            SendInput(player, direction);
            ReadUntil(() => Progress(player) > 0);
            SendInput(player, 0);
            if (expectedStep != 0)
                Check(Progress(player) % expectedStep == 0, "movement must use the acquired speed");
            ReadUntil(() => X(player) == x && Y(player) == y && Progress(player) == 0);
        }

        public bool Pickup(int player, int cell)
        {
            int x = cell % 20;
            while (X(player) != x) MoveOne(player, X(player) < x ? (byte)8 : (byte)4);
            Check(Y(player) == 18 && obstacles[cell] == 1 && items[cell] != 0, "Bomb below the hidden item");
            int original = items[cell], count = pickups.Count;
            SendInput(player, 0, true);
            ReadUntil(() => obstacles[cell] == 0);
            MoveOne(player, 1);
            ReadUntil(() => pickups.Count > count);
            var eventData = pickups[count];
            Check(pickups.Count == count + 1 && items[cell] == 0 && Word(eventData, 1) == cell + 1 &&
                  eventData[3] == original && eventData[4] == player + 1, "correct pickup owner and cell");
            Console.WriteLine("PASS: P{0} revealed and collected item {1} at ({2},17); both TCP streams match.",
                              player + 1, original, x);
            if (original == 3) {
                MoveOne(player, 2, 25 * abilities[player][2]);
                Console.WriteLine("PASS: P{0}'s following movement used the updated server speed.", player + 1);
            }
            return original == 3;
        }
    }

    public static void RunForestPickups(string directory)
    {
        bool[] covered = new bool[2];
        bool contested = false, speedChecked = false;
        for (int attempt = 0; attempt < 16 && (!covered[0] || !covered[1] || !contested || !speedChecked); ++attempt) {
            using (var server = new App(Path.Combine(directory, "Server.exe"))) {
                server.Wait("Server listening");
                using (var a = Connect())
                using (var b = Connect()) {
                    var game = new ForestPickupPair(a.GetStream(), b.GetStream());
                    if (covered[0] && covered[1] && !contested) {
                        contested = game.ContestPickup();
                    } else {
                        for (int i = 0; i < 2; ++i) {
                            if (covered[i] && speedChecked) continue;
                            int cell = game.FindItem(i, covered[i] ? 3 : 0);
                            if (cell < 0) continue;
                            speedChecked |= game.Pickup(i, cell);
                            covered[i] = true;
                        }
                    }
                }
                server.Stop();
            }
        }
        Check(covered[0] && covered[1], "No usable Forest pickup for both slots in 16 maps");
        Check(contested, "No usable Forest contested pickup in 16 maps");
        Check(speedChecked, "No reachable speed pickup in 16 maps");
    }
}
