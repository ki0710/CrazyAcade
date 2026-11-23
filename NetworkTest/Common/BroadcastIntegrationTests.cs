// Compile together with IntegrationTests.cs to reuse its process/protocol helpers.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net.Sockets;
using System.Threading;

public static partial class NetworkIntegrationTests
{
    sealed class BroadcastFrame {
        public int Type;
        public uint Match;
        public byte[] Payload;
    }
    // One absolute deadline per receive phase, including small reads and delays.
    // Small application reads do not imply TCP segmentation or partial server sends.
    static byte[] BroadcastExactly(NetworkStream stream, int length, Stopwatch timer,
                                    int chunk) {
        byte[] bytes = new byte[length];
        int offset = 0;
        while (offset < length) {
            int remaining = 7000 - (int)timer.ElapsedMilliseconds;
            Check(remaining > 0, "Broadcast receive phase exceeded seven seconds");
            stream.ReadTimeout = remaining;
            int got = stream.Read(bytes, offset, Math.Min(chunk, length - offset));
            Check(got > 0, "Broadcast unexpected EOF");
            offset += got;
        }
        return bytes;
    }
    static uint BroadcastWord(byte[] bytes, int offset) {
        return ((uint)bytes[offset] << 24) | ((uint)bytes[offset + 1] << 16) |
               ((uint)bytes[offset + 2] << 8) | bytes[offset + 3];
    }
    static BroadcastFrame BroadcastRead(NetworkStream stream, Stopwatch timer, bool delayed = false) {
        int chunk = delayed ? 3 : 1048576;
        byte[] header = BroadcastExactly(stream, 10, timer, chunk);
        uint size = BroadcastWord(header, 0);
        Check(size >= 10 && size <= 1048576, "Broadcast invalid frame length");
        var frame = new BroadcastFrame {
            Type = (header[4] << 8) | header[5], Match = BroadcastWord(header, 6),
            Payload = BroadcastExactly(stream, (int)size - 10, timer, chunk)
        };
        // Delay once per packet: Windows timer granularity must not multiply a
        // nominal 1 ms delay for every three-byte read into an artificial timeout.
        if (delayed) Thread.Sleep(5);
        return frame;
    }
    static void BroadcastSame(BroadcastFrame expected, BroadcastFrame actual) {
        Check(expected.Type == actual.Type && expected.Match == actual.Match &&
              Convert.ToBase64String(expected.Payload) == Convert.ToBase64String(actual.Payload),
              "Delayed TCP recipient lost, duplicated, reordered or corrupted a frame");
    }
    static byte[] BroadcastStart(NetworkStream a, NetworkStream b, uint match) {
        Send(a, 11, 0, 1); Receive(a, 3); Receive(b, 3);
        Send(b, 11, 0, 1); Receive(a, 3); Receive(b, 3);
        byte[] snapshot = Receive(a, 6, match);
        Check(Convert.ToBase64String(snapshot) == Convert.ToBase64String(Receive(b, 6, match)),
              "Broadcast initial snapshots differ");
        Send(a, 12, match); Send(b, 12, match);
        Receive(a, 13, match); Receive(b, 13, match);
        return ReadSamePlayers(a, b, match); // Also consumes the initial empty Bomb frame.
    }
    static void BroadcastScenario(string directory, int slowId) {
        using (App server = new App(Path.Combine(directory, "Server.exe"))) {
            server.Wait("Server listening");
            using (TcpClient first = Connect())
            using (TcpClient second = Connect()) {
                NetworkStream a = first.GetStream(), b = second.GetStream();
                Send(a, 1, 0, 0, 1, 0, 12); Check(Receive(a, 2)[0] == 1, "first slot"); Receive(a, 3);
                Send(b, 1, 0, 0, 1, 0, 12); Check(Receive(b, 2)[0] == 2, "second slot");
                Receive(b, 3); Receive(a, 3);
                // Reuse the existing guaranteed City movement lane, independent of random obstacles.
                Send(a, 15, 0, 1, 2); Receive(a, 3); Receive(b, 3);
                byte[] initial = BroadcastStart(a, b, 1);
                Check(initial[4] == 8 && initial[5] == 5, "City P1 start");
                TcpClient slowClient = slowId == 1 ? first : second;
                NetworkStream slow = slowId == 1 ? a : b, healthy = slowId == 1 ? b : a;
                slowClient.ReceiveBufferSize = 4096;
                var recorded = new List<BroadcastFrame>();
                // Stop reading slow entirely while the healthy peer receives the complete lane traversal.
                Send(a, 8, 1, Input(8, 1));
                var timer = Stopwatch.StartNew();
                uint previousTick = BroadcastWord(initial, 0);
                do {
                    var frame = BroadcastRead(healthy, timer);
                    Check(frame.Type == 16 && frame.Match == 1 && frame.Payload.Length == 18,
                          "Expected City movement Broadcast");
                    Check(BroadcastWord(frame.Payload, 0) > previousTick, "movement tick must increase");
                    previousTick = BroadcastWord(frame.Payload, 0);
                    recorded.Add(frame);
                } while (recorded[recorded.Count - 1].Payload[4] != 19);
                Check(recorded.Count >= 2, "Need multiple updates while slow recipient is unread");
                Send(a, 8, 1, Input(0, 2));
                long pausedMs = timer.ElapsedMilliseconds;
                // Resume in small delayed reads; compare every frame, not just the final position.
                timer.Restart();
                foreach (var frame in recorded) BroadcastSame(frame, BroadcastRead(slow, timer, true));
                Console.WriteLine("PASS: slow slot " + slowId + " paused " + pausedMs +
                    " ms; healthy received " + recorded.Count + " updates; delayed bytes identical");

                // Re-enter the lane, observe an update, then abort the peer without graceful shutdown.
                Send(a, 8, 1, Input(4, 3));
                timer.Restart();
                var moving = BroadcastRead(healthy, timer);
                Check(moving.Type == 16 && moving.Match == 1 && moving.Payload[8] + moving.Payload[9] > 0,
                      "Reset scenario must start during active movement Broadcast");
                slowClient.Client.LingerState = new LingerOption(true, 0);
                slowClient.Close(); // Abortive TCP close (RST), not a claim about send syscall timing.
                bool canceled = false, peerLeft = false, roomSeen = false;
                timer.Restart();
                while (!roomSeen) {
                    var frame = BroadcastRead(healthy, timer);
                    if (frame.Type == 16) {
                        Check(!canceled && frame.Match == 1 && frame.Payload.Length == 18,
                              "Movement must precede match cancellation");
                    } else if (frame.Type == 14) {
                        Check(!canceled && frame.Match == 1 && frame.Payload.Length == 1 && frame.Payload[0] == 1,
                              "Expected exactly one departure cancellation");
                        canceled = true;
                    } else if (frame.Type == 5) {
                        Check(canceled && !peerLeft && frame.Match == 0 && frame.Payload.Length == 1 &&
                              frame.Payload[0] == slowId, "Expected departed slot notification");
                        peerLeft = true;
                    } else {
                        Check(frame.Type == 3 && frame.Match == 0 && frame.Payload.Length == 7 &&
                              frame.Payload[0] == 1 && frame.Payload[1] == 0 && frame.Payload[2] == 0,
                              "Expected survivor lobby state");
                        roomSeen = peerLeft;
                    }
                }
                using (TcpClient replacement = Connect()) {
                    NetworkStream fresh = replacement.GetStream();
                    Send(fresh, 1, 0, 0, 1, 0, 12);
                    byte[] welcome = Receive(fresh, 2);
                    Check(welcome.Length == 1 && welcome[0] == slowId, "new connection must reuse free slot");
                    byte[] room = Receive(fresh, 3), peerRoom = Receive(healthy, 3);
                    Check(room.Length == 7 && room[0] == 2 && Convert.ToBase64String(room) ==
                          Convert.ToBase64String(peerRoom), "survivor and replacement room mismatch");
                    // Prove the survivor is still the same connection and both can start a new match.
                    NetworkStream p1 = slowId == 1 ? fresh : healthy, p2 = slowId == 1 ? healthy : fresh;
                    byte[] restarted = BroadcastStart(p1, p2, 2);
                    Check(restarted[4] == 8 && restarted[5] == 5, "fresh match positions");
                    // The replacement itself sends gameplay input with a fresh sequence.
                    Send(fresh, 8, 2, Input(slowId == 1 ? (byte)8 : (byte)4, 1));
                    timer.Restart();
                    var next = BroadcastRead(healthy, timer);
                    BroadcastSame(next, BroadcastRead(fresh, timer));
                    int progress = slowId == 1 ? 8 : 15;
                    Check(next.Type == 16 && next.Match == 2 && next.Payload.Length == 18 &&
                          next.Payload[progress] + next.Payload[progress + 1] > 0,
                          "replacement input must move its own player in the new match");
                    server.Stop();
                }
                Console.WriteLine("PASS: active-match TCP reset of slot " + slowId +
                    "; survivor retained; replacement handshake, match 2 and movement succeed");
            }
        }
    }
    public static void RunBroadcast(string directory, int repetitions = 3) {
        Check(repetitions > 0 && repetitions <= 20, "repetitions must be 1..20");
        for (int i = 0; i < repetitions; ++i) BroadcastScenario(directory, i % 2 + 1);
        Console.WriteLine("Broadcast TCP scenarios passed: " + repetitions +
            ". OS send saturation/partial-send/WSAEWOULDBLOCK occurrence is not instrumented or asserted.");
    }
}
