// Run with PowerShell Add-Type; no external test framework is required.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net.Sockets;
using System.Text;
using System.Threading;

public static class NetworkIntegrationTests
{
    sealed class App : IDisposable
    {
        public readonly Process Process;
        readonly List<string> lines = new List<string>();
        readonly AutoResetEvent changed = new AutoResetEvent(false);
        public App(string executable, string arguments = "")
        {
            Process = new Process();
            Process.StartInfo = new ProcessStartInfo(executable, arguments) {
                UseShellExecute = false, CreateNoWindow = true,
                RedirectStandardInput = true, RedirectStandardOutput = true,
                RedirectStandardError = true
            };
            Process.OutputDataReceived += delegate(object sender, DataReceivedEventArgs e) {
                if (e.Data != null) { lock (lines) lines.Add(e.Data); changed.Set(); }
            };
            Process.Start();
            Process.BeginOutputReadLine();
        }
        public int Mark { get { lock (lines) return lines.Count; } }
        public void Wait(string text, int from = 0)
        {
            Stopwatch timer = Stopwatch.StartNew();
            while (timer.ElapsedMilliseconds < 13000) {
                lock (lines) {
                    for (int i = from; i < lines.Count; ++i)
                        if (lines[i].Contains(text)) return;
                }
                changed.WaitOne(100);
            }
            lock (lines) throw new Exception("Missing '" + text + "':\n" + string.Join("\n", lines));
        }
        public void Exited(int expected)
        {
            Check(Process.WaitForExit(7000), "Process exit timeout");
            Process.WaitForExit(); // Flush asynchronous output callbacks.
            Check(Process.ExitCode == expected, "Unexpected exit code " + Process.ExitCode);
        }
        public void Stop() { Process.StandardInput.WriteLine(); Process.StandardInput.Flush(); Exited(0); }
        public void Keys(string keys) { Process.StandardInput.Write(keys); Process.StandardInput.Flush(); }
        public void Count(string text, int expected, int from = 0)
        {
            // Observe a bounded quiet interval, rather than merely seeing the first packet.
            Thread.Sleep(500);
            int count = 0;
            lock (lines) for (int i = from; i < lines.Count; ++i)
                if (lines[i].Contains(text)) ++count;
            Check(count == expected, "Expected " + expected + " occurrences of " + text + ", got " + count);
        }
        public void Dispose()
        {
            if (!Process.HasExited) { Process.Kill(); Process.WaitForExit(); }
            Process.Dispose();
            changed.Dispose();
        }
    }
    static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }
    static byte[] Frame(ushort type, uint match, byte[] payload)
    {
        int size = 10 + payload.Length;
        byte[] bytes = new byte[size];
        bytes[0] = (byte)(size >> 24); bytes[1] = (byte)(size >> 16);
        bytes[2] = (byte)(size >> 8); bytes[3] = (byte)size;
        bytes[4] = (byte)(type >> 8); bytes[5] = (byte)type;
        bytes[6] = (byte)(match >> 24); bytes[7] = (byte)(match >> 16);
        bytes[8] = (byte)(match >> 8); bytes[9] = (byte)match;
        Buffer.BlockCopy(payload, 0, bytes, 10, payload.Length);
        return bytes;
    }
    static byte[] ReadExactly(NetworkStream stream, int size)
    {
        byte[] result = new byte[size];
        int offset = 0;
        while (offset < size) {
            int count = stream.Read(result, offset, size - offset);
            Check(count > 0, "Unexpected EOF"); offset += count;
        }
        return result;
    }
    static byte[] Receive(NetworkStream stream, int type, uint match = 0)
    {
        byte[] header = ReadExactly(stream, 10);
        int size = (header[0] << 24) | (header[1] << 16) | (header[2] << 8) | header[3];
        Check(size >= 10 && size <= 1048576, "Invalid length");
        Check(((header[4] << 8) | header[5]) == type, "Unexpected packet type");
        uint id = ((uint)header[6] << 24) | ((uint)header[7] << 16) | ((uint)header[8] << 8) | header[9];
        Check(id == match, "Unexpected matchId " + id);
        return ReadExactly(stream, size - 10);
    }
    static TcpClient Connect()
    {
        TcpClient client = new TcpClient();
        client.Connect("127.0.0.1", 9000);
        client.GetStream().ReadTimeout = 7000;
        client.GetStream().WriteTimeout = 7000;
        return client;
    }
    static void ErrorCase(byte[] bytes, int code)
    {
        using (TcpClient client = Connect()) {
            NetworkStream stream = client.GetStream();
            stream.Write(bytes, 0, bytes.Length);
            byte[] error = Receive(stream, 4);
            Check(error.Length == 2 && error[0] == 0 && error[1] == code, "Wrong SC_ERROR");
            Check(stream.ReadByte() == -1, "Rejected connection not closed");
        }
    }
    static byte[] Input(byte direction, uint sequence, bool place = false)
    {
        return new byte[] {direction, (byte)(sequence >> 24), (byte)(sequence >> 16),
                           (byte)(sequence >> 8), (byte)sequence, (byte)(place ? 1 : 0)};
    }
    static void CheckRoomState(byte[] payload, byte count, byte first, byte second,
                               byte map = 1, byte color1 = 0, byte color2 = 0, byte host = 1)
    {
        Check(payload.Length == 7 && payload[0] == count && payload[1] == first &&
              payload[2] == second && payload[3] == map && payload[4] == color1 &&
              payload[5] == color2 && payload[6] == host, "Invalid room state payload");
    }
    static void Ready(App sender, App first, App second, string key, string state)
    {
        int firstMark = first.Mark, secondMark = second.Mark;
        sender.Keys(key);
        first.Wait("SC_ROOM_STATE: count=2 ready=" + state, firstMark);
        second.Wait("SC_ROOM_STATE: count=2 ready=" + state, secondMark);
    }
    static void RequestErrorCase(App server, byte[] payload, uint match = 0, bool approved = true, ushort type = 8)
    {
        int mark = server.Mark;
        using (TcpClient raw = Connect()) {
            NetworkStream stream = raw.GetStream();
            if (approved) {
                byte[] hello = Frame(1, 0, new byte[] {0, 1, 0, 11});
                stream.Write(hello, 0, hello.Length);
                Check(Receive(stream, 2)[0] == 1, "Input test player not approved");
                CheckRoomState(Receive(stream, 3), 1, 0, 0);
            }
            byte[] bytes = Frame(type, match, payload);
            stream.Write(bytes, 0, bytes.Length);
            Check(Receive(stream, 4)[1] == 2, "Invalid request not rejected");
            Check(stream.ReadByte() == -1, "Invalid request session not closed");
        }
        if (approved) server.Wait("SC_ROOM_STATE: count=0", mark);
    }
    public static void Run(string buildDirectory)
    {
        List<App> apps = new List<App>();
        try {
            App server = new App(Path.Combine(buildDirectory, "Server.exe")); apps.Add(server);
            server.Wait("Server listening");
            App first = new App(Path.Combine(buildDirectory, "Client.exe")); apps.Add(first);
            first.Wait("SC_WELCOME: Player ID=1"); first.Wait("SC_ROOM_STATE: count=1");
            App second = new App(Path.Combine(buildDirectory, "Client.exe")); apps.Add(second);
            second.Wait("SC_WELCOME: Player ID=2");
            first.Wait("SC_ROOM_STATE: count=2"); second.Wait("SC_ROOM_STATE: count=2");

            first.Wait("SC_ROOM_STATE: count=2 ready=0,0");
            second.Wait("SC_ROOM_STATE: count=2 ready=0,0");
            Ready(first, first, second, "r", "1,0");
            Ready(first, first, second, "C", "0,0");
            Ready(second, first, second, "R", "0,1");
            Ready(first, first, second, "R", "1,1");
            first.Wait("SC_SNAPSHOT:"); second.Wait("SC_SNAPSHOT:");
            int cancelMarkA = first.Mark, cancelMarkB = second.Mark;
            Ready(first, first, second, "C", "0,1");
            first.Wait("SC_MATCH_CANCEL: match=1 reason=3", cancelMarkA);
            second.Wait("SC_MATCH_CANCEL: match=1 reason=3", cancelMarkB);
            Ready(first, first, second, "r", "1,1");
            first.Wait("map=20x20 match=2"); second.Wait("map=20x20 match=2");
            cancelMarkA = first.Mark; cancelMarkB = second.Mark;
            Ready(second, first, second, "c", "1,0");
            first.Wait("SC_MATCH_CANCEL: match=2 reason=3", cancelMarkA);
            second.Wait("SC_MATCH_CANCEL: match=2 reason=3", cancelMarkB);
            Console.WriteLine("PASS: ready/cancel synchronized at both real clients for both player IDs");
            App third = new App(Path.Combine(buildDirectory, "Client.exe")); apps.Add(third);
            third.Wait("SC_ERROR: Room full"); third.Exited(2);
            Console.WriteLine("PASS: two real clients, Player IDs, room broadcasts, third-client rejection");

            int inputMark = server.Mark;
            Thread firstInput = new Thread(delegate() { first.Keys("wasdx"); });
            Thread secondInput = new Thread(delegate() { second.Keys("DWsAX"); });
            firstInput.Start(); secondInput.Start(); firstInput.Join(); secondInput.Join();
            string[] firstDirections = {"UP", "LEFT", "DOWN", "RIGHT", "NONE"};
            string[] secondDirections = {"RIGHT", "UP", "DOWN", "LEFT", "NONE"};
            for (int i = 0; i < 5; ++i) {
                server.Wait("Player 1 -> " + firstDirections[i] + ", sequence=" + (i + 1), inputMark);
                server.Wait("Player 2 -> " + secondDirections[i] + ", sequence=" + (i + 1), inputMark);
            }
            first.Wait("Queued CS_INPUT: NONE, sequence=5");
            second.Wait("Queued CS_INPUT: NONE, sequence=5");
            first.Keys("q?W");
            server.Wait("Player 1 -> UP, sequence=6", inputMark);
            Console.WriteLine("PASS: two real clients send all directions/NONE with independent sequences and Session identities");

            first.Stop();
            second.Wait("SC_PEER_LEFT: Player ID=1"); second.Wait("SC_ROOM_STATE: count=1");
            int secondMark = second.Mark;
            App replacement = new App(Path.Combine(buildDirectory, "Client.exe")); apps.Add(replacement);
            replacement.Wait("SC_WELCOME: Player ID=1"); replacement.Wait("SC_ROOM_STATE: count=2");
            second.Wait("SC_ROOM_STATE: count=2", secondMark);
            replacement.Wait("SC_ROOM_STATE: count=2 ready=0,0");
            second.Wait("SC_ROOM_STATE: count=2 ready=0,0", secondMark);
            Console.WriteLine("PASS: ready player departs and reused slot starts unready at both clients");
            inputMark = server.Mark;
            replacement.Keys("x"); second.Keys("w");
            server.Wait("Player 1 -> NONE, sequence=1", inputMark);
            server.Wait("Player 2 -> UP, sequence=6", inputMark);
            Console.WriteLine("PASS: normal client exit, peer-left notification, slot reuse");
            int mark = server.Mark;
            replacement.Stop(); second.Stop();
            server.Wait("SC_ROOM_STATE: count=0", mark);

            ErrorCase(Frame(1, 0, new byte[] {0, 2, 0, 5}), 3);
            ErrorCase(Frame(1, 0, new byte[] {0, 1, 0, 1}), 3);
            ErrorCase(Frame(1, 0, new byte[] {0, 1, 0, 2}), 3);
            ErrorCase(Frame(1, 0, new byte[] {0, 1, 0, 3}), 3);
            ErrorCase(Frame(1, 0, new byte[] {0, 1, 0, 4}), 3);
            ErrorCase(Frame(1, 1, new byte[] {0, 1, 0, 11}), 2);
            ErrorCase(Frame(2, 0, new byte[] {1}), 2);
            ErrorCase(Frame(1, 0, new byte[] {0}), 2);
            byte[] badLength = Frame(1, 0, new byte[0]); badLength[3] = 9;
            ErrorCase(badLength, 2);
            badLength[0] = 1;
            ErrorCase(badLength, 2);
            ErrorCase(Frame(99, 0, new byte[0]), 2);
            Console.WriteLine("PASS: version, matchId, direction, payload, length and type validation");

            RequestErrorCase(server, Input(3, 1)); // Diagonal combination.
            RequestErrorCase(server, Input(0x10, 1));
            RequestErrorCase(server, Input(0xFF, 1));
            RequestErrorCase(server, new byte[0]);
            RequestErrorCase(server, new byte[4]);
            RequestErrorCase(server, new byte[] {1, 0, 0, 0, 1, 2}); // Invalid place flag.
            RequestErrorCase(server, Input(1, 1), uint.MaxValue);
            RequestErrorCase(server, Input(1, 1), 0, false);
            Console.WriteLine("PASS: CS_INPUT rejects invalid masks, lengths, matchId and unapproved sessions");
            RequestErrorCase(server, new byte[0], 0, true, 11);
            RequestErrorCase(server, new byte[] {1, 0}, 0, true, 11);
            RequestErrorCase(server, new byte[] {2}, 0, true, 11);
            RequestErrorCase(server, new byte[] {255}, 0, true, 11);
            RequestErrorCase(server, new byte[] {1}, 1, true, 11);
            RequestErrorCase(server, new byte[] {1}, 0, false, 11);
            ErrorCase(Frame(1, 0, new byte[] {0, 1, 0, 5}), 3);
            ErrorCase(Frame(1, 0, new byte[] {0, 1, 0, 6}), 3);
            ErrorCase(Frame(1, 0, new byte[] {0, 1, 0, 7}), 3);
            ErrorCase(Frame(1, 0, new byte[] {0, 1, 0, 8}), 3);
            ErrorCase(Frame(1, 0, new byte[] {0, 1, 0, 9}), 3);
            ErrorCase(Frame(1, 0, new byte[] {0, 1, 0, 10}), 3);
            RequestErrorCase(server, new byte[0], 0, true, 15);
            RequestErrorCase(server, new byte[] {1}, 0, true, 15);
            RequestErrorCase(server, new byte[] {2, 0, 1}, 0, true, 15);
            RequestErrorCase(server, new byte[] {0, 1}, 0, true, 15);
            RequestErrorCase(server, new byte[] {1, 0}, 0, true, 15);
            RequestErrorCase(server, new byte[] {1, 4}, 0, true, 15);
            RequestErrorCase(server, new byte[] {2, 7}, 0, true, 15);
            RequestErrorCase(server, new byte[] {2, 255}, 0, true, 15);
            RequestErrorCase(server, new byte[] {1, 1}, 1, true, 15);
            RequestErrorCase(server, new byte[] {2, 0}, 0, false, 15);
            Console.WriteLine("PASS: CS_SELECT malformed lengths/kinds/maps/colors/matchId/unapproved requests rejected; protocol 7/8/9/10 rejected");
            RequestErrorCase(server, new byte[0], 0, true, 12);
            RequestErrorCase(server, new byte[] {1}, 1, true, 12);
            RequestErrorCase(server, new byte[0], uint.MaxValue, true, 12);
            RequestErrorCase(server, new byte[0], 1, false, 12);
            Console.WriteLine("PASS: CS_READY rejects invalid lengths/values/matchId/unapproved sessions; old version rejected");

            mark = server.Mark;
            using (TcpClient raw = Connect()) {
                NetworkStream stream = raw.GetStream();
                byte[] hello = Frame(1, 0, new byte[] {0, 1, 0, 11});
                // Multiple writes exercise the live accumulated receive path.
                for (int i = 0; i < hello.Length; ++i) stream.Write(hello, i, 1);
                Check(Receive(stream, 2)[0] == 1, "Fragmented HELLO not accepted");
                CheckRoomState(Receive(stream, 3), 1, 0, 0);
                // Two complete requests in one write: repeated HELLO must fail.
                byte[] merged = new byte[hello.Length * 2];
                Buffer.BlockCopy(hello, 0, merged, 0, hello.Length);
                Buffer.BlockCopy(hello, 0, merged, hello.Length, hello.Length);
                stream.Write(merged, 0, merged.Length);
                Check(Receive(stream, 4)[1] == 2, "Repeated HELLO accepted");
                Check(stream.ReadByte() == -1, "Invalid session not closed");
            }
            server.Wait("SC_ROOM_STATE: count=0", mark);
            using (TcpClient raw = Connect()) {
                NetworkStream stream = raw.GetStream();
                byte[] hello = Frame(1, 0, new byte[] {0, 1, 0, 11});
                stream.Write(hello, 0, 3);
                raw.Client.Shutdown(SocketShutdown.Send);
                Check(Receive(stream, 4)[1] == 2, "Truncated EOF not rejected");
            }
            Console.WriteLine("PASS: segmented writes, coalesced requests, truncated EOF");

            using (TcpClient raw = Connect()) {
                Check(Receive(raw.GetStream(), 4)[1] == 4, "HELLO timeout not enforced");
                Check(raw.GetStream().ReadByte() == -1, "Timed-out session not closed");
            }
            Console.WriteLine("PASS: handshake timeout");

            App final = new App(Path.Combine(buildDirectory, "Client.exe")); apps.Add(final);
            final.Wait("SC_WELCOME: Player ID=1");
            server.Stop(); final.Wait("Server disconnected."); final.Exited(0);
            Console.WriteLine("PASS: normal server shutdown with a connected client");
            ConcurrentReadyAndShutdown(buildDirectory);
            MatchFlow(buildDirectory);
            SelectionFlow(buildDirectory);
            StaleLoading(buildDirectory);
            MovementFlow(buildDirectory);
            using (App iceServer = new App(Path.Combine(buildDirectory, "Server.exe"), "3")) {
                iceServer.Wait("Server listening");
                using (App iceFirst = new App(Path.Combine(buildDirectory, "Client.exe")))
                using (App iceSecond = new App(Path.Combine(buildDirectory, "Client.exe"))) {
                    iceFirst.Wait("SC_WELCOME: Player ID="); iceSecond.Wait("SC_WELCOME: Player ID=");
                    iceFirst.Keys("r"); iceSecond.Keys("r");
                    iceFirst.Wait("SC_SNAPSHOT: stage=3 map=20x20");
                    iceSecond.Wait("SC_SNAPSHOT: stage=3 map=20x20");
                    iceFirst.Wait("SC_STATE: hearts=3,3 result=0");
                    iceSecond.Wait("SC_STATE: hearts=3,3 result=0");
                    iceFirst.Stop(); iceSecond.Stop();
                }
                iceServer.Stop();
            }
            Console.WriteLine("PASS: ice server sends the same initial hearts to both real clients");
            using (App forestServer = new App(Path.Combine(buildDirectory, "Server.exe"))) {
                forestServer.Wait("Server listening");
                using (App forestFirst = new App(Path.Combine(buildDirectory, "Client.exe")))
                using (App forestSecond = new App(Path.Combine(buildDirectory, "Client.exe"))) {
                    forestFirst.Wait("SC_WELCOME: Player ID="); forestSecond.Wait("SC_WELCOME: Player ID=");
                    forestFirst.Keys("r"); forestSecond.Keys("r");
                    forestFirst.Wait("SC_SNAPSHOT: apples=0,0"); forestSecond.Wait("SC_SNAPSHOT: apples=0,0");
                    forestFirst.Stop(); forestSecond.Stop();
                }
                forestServer.Stop();
            }
            Console.WriteLine("PASS: forest server sends zero carried apples to both real clients");
        } finally {
            for (int i = apps.Count - 1; i >= 0; --i) apps[i].Dispose();
        }
    }
    static void ConcurrentReadyAndShutdown(string directory)
    {
        using (App server = new App(Path.Combine(directory, "Server.exe"))) {
            server.Wait("Server listening");
            using (App first = new App(Path.Combine(directory, "Client.exe")))
            using (App second = new App(Path.Combine(directory, "Client.exe"))) {
                first.Wait("SC_WELCOME: Player ID="); second.Wait("SC_WELCOME: Player ID=");
                using (ManualResetEvent go = new ManualResetEvent(false)) {
                    Thread a = new Thread(delegate() { go.WaitOne(); first.Keys("r"); });
                    Thread b = new Thread(delegate() { go.WaitOne(); second.Keys("r"); });
                    a.Start(); b.Start(); go.Set(); a.Join(); b.Join();
                }
                first.Wait("map=20x20 match=1"); second.Wait("map=20x20 match=1");
                first.Keys("lll"); second.Keys("lll");
                first.Wait("SC_MATCH_START: match=1"); second.Wait("SC_MATCH_START: match=1");
                server.Count("Loading: match=1", 1);
                server.Count("SC_MATCH_START: match=1", 1);
                first.Count("SC_MATCH_START: match=1", 1); second.Count("SC_MATCH_START: match=1", 1);
                server.Stop();
                first.Wait("Server disconnected."); second.Wait("Server disconnected.");
                first.Exited(0); second.Exited(0);
            }
        }
        Console.WriteLine("PASS: concurrent READY starts once; shutdown joins receivers with two clients");
    }
    static void MatchFlow(string directory)
    {
        using (App server = new App(Path.Combine(directory, "Server.exe"))) {
            server.Wait("Server listening");
            using (App second = new App(Path.Combine(directory, "Client.exe"))) {
                second.Wait("SC_WELCOME: Player ID=1");
                using (App first = new App(Path.Combine(directory, "Client.exe"))) {
                    first.Wait("SC_WELCOME: Player ID=2");
                    Ready(first, first, second, "r", "0,1");
                    first.Count("SC_SNAPSHOT", 0); second.Count("SC_SNAPSHOT", 0);
                    Ready(second, first, second, "r", "1,1");
                    first.Wait("map=20x20 match=1"); second.Wait("map=20x20 match=1");
                    first.Keys("lLL"); first.Wait("Queued CS_LOADED: match=1");
                    first.Count("SC_MATCH_START", 0); second.Count("SC_MATCH_START", 0);
                    second.Keys("l");
                    first.Wait("SC_MATCH_START: match=1"); second.Wait("SC_MATCH_START: match=1");
                    first.Count("SC_MATCH_START: match=1", 1); second.Count("SC_MATCH_START: match=1", 1);
                    first.Stop();
                }
                second.Wait("SC_MATCH_CANCEL: match=1 reason=1");
                using (App replacement = new App(Path.Combine(directory, "Client.exe"))) {
                    replacement.Wait("SC_WELCOME: Player ID=2");
                    second.Keys("r"); replacement.Keys("r");
                    second.Wait("map=20x20 match=2"); replacement.Wait("map=20x20 match=2");
                    second.Keys("l"); replacement.Stop();
                }
                second.Wait("SC_MATCH_CANCEL: match=2 reason=1");
                second.Count("SC_MATCH_START: match=2", 0);
                using (App replacement = new App(Path.Combine(directory, "Client.exe"))) {
                    replacement.Wait("SC_WELCOME: Player ID=2");
                    second.Keys("r"); replacement.Keys("r");
                    second.Wait("map=20x20 match=3"); replacement.Wait("map=20x20 match=3");
                    second.Keys("lll");
                    second.Wait("SC_MATCH_CANCEL: match=3 reason=2");
                    replacement.Wait("SC_MATCH_CANCEL: match=3 reason=2");
                    second.Count("SC_MATCH_START: match=3", 0);
                    second.Keys("r"); replacement.Keys("r");
                    second.Wait("map=20x20 match=4"); replacement.Wait("map=20x20 match=4");
                    second.Keys("l"); replacement.Keys("l");
                    second.Wait("SC_MATCH_START: match=4"); replacement.Wait("SC_MATCH_START: match=4");
                    second.Count("SC_MATCH_START: match=4", 1); replacement.Count("SC_MATCH_START: match=4", 1);
                    replacement.Stop();
                }
                second.Stop();
            }
            server.Stop();
        }
        Console.WriteLine("PASS: one ready waits; both snapshot; duplicate/single LOADED waits; start once; loading departure/timeout and retry");
    }
    static void Selection(App sender, App first, App second, string key, string state)
    {
        int a = first.Mark, b = second.Mark;
        sender.Keys(key);
        first.Wait("SC_ROOM_STATE: count=2 " + state, a);
        second.Wait("SC_ROOM_STATE: count=2 " + state, b);
    }
    static void SelectionFlow(string directory)
    {
        using (App server = new App(Path.Combine(directory, "Server.exe"))) {
            server.Wait("Server listening");
            using (App first = new App(Path.Combine(directory, "Client.exe"))) {
                first.Wait("SC_WELCOME: Player ID=1");
                using (App second = new App(Path.Combine(directory, "Client.exe"))) {
                    second.Wait("SC_WELCOME: Player ID=2");
                    first.Wait("map=1 colors=0,0 host=1"); second.Wait("map=1 colors=0,0 host=1");
                    Ready(first, first, second, "r", "1,0");
                    int a = first.Mark, b = second.Mark;
                    first.Keys("z1"); // Same color/map must not reset ready or broadcast.
                    first.Count("SC_ROOM_STATE", 0, a); second.Count("SC_ROOM_STATE", 0, b);
                    Selection(second, first, second, "K", "ready=1,0 map=1 colors=0,6 host=1");
                    Selection(second, first, second, "z", "ready=1,0 map=1 colors=0,0 host=1");
                    Selection(second, first, second, "V", "ready=1,0 map=1 colors=0,1 host=1");
                    Selection(first, first, second, "b", "ready=0,0 map=1 colors=2,1 host=1");
                    Ready(second, first, second, "r", "0,1");
                    Selection(first, first, second, "2", "ready=0,0 map=2 colors=2,1 host=1");
                    a = first.Mark; b = second.Mark;
                    second.Keys("3"); second.Wait("SC_ERROR: Selection denied", b);
                    first.Count("SC_ROOM_STATE", 0, a); second.Count("SC_ROOM_STATE", 0, b);
                    Selection(first, first, second, "3", "ready=0,0 map=3 colors=2,1 host=1");
                    Selection(first, first, second, "1", "ready=0,0 map=1 colors=2,1 host=1");
                    Selection(first, first, second, "2", "ready=0,0 map=2 colors=2,1 host=1");
                    first.Keys("r"); second.Keys("r");
                    first.Wait("SC_SNAPSHOT: stage=2 map=20x20 match=1");
                    second.Wait("SC_SNAPSHOT: stage=2 map=20x20 match=1");
                    a = first.Mark;
                    first.Keys("1"); first.Wait("SC_ERROR: Selection denied", a);
                    first.Keys("l"); second.Keys("l");
                    first.Wait("SC_MATCH_START: match=1"); second.Wait("SC_MATCH_START: match=1");
                    first.Stop();
                    second.Wait("SC_MATCH_CANCEL: match=1 reason=1");
                    using (App replacement = new App(Path.Combine(directory, "Client.exe"))) {
                        replacement.Wait("SC_WELCOME: Player ID=1");
                        replacement.Wait("map=2 colors=0,1 host=2");
                        Selection(second, replacement, second, "3", "ready=0,0 map=3 colors=0,1 host=2");
                        a = replacement.Mark;
                        replacement.Keys("1"); replacement.Wait("SC_ERROR: Selection denied", a);
                        replacement.Keys("r"); second.Keys("r");
                        replacement.Wait("SC_SNAPSHOT: stage=3 map=20x20 match=2");
                        second.Wait("SC_SNAPSHOT: stage=3 map=20x20 match=2");
                        replacement.Keys("l"); second.Keys("l");
                        replacement.Wait("SC_MATCH_START: match=2"); second.Wait("SC_MATCH_START: match=2");
                        replacement.Stop();
                    }
                    second.Stop();
                }
            }
            server.Stop();
        }
        Console.WriteLine("PASS: real clients synchronize selections; map/color readiness reset; no-op preserved; guest denied without disconnect; host transfer and City/Ice start");
    }
    static void Send(NetworkStream stream, ushort type, uint match, params byte[] payload)
    {
        byte[] bytes = Frame(type, match, payload);
        stream.Write(bytes, 0, bytes.Length);
    }
    static byte[] ReadSamePlayers(NetworkStream a, NetworkStream b, uint match)
    {
        byte[] frame = Receive(a, 16, match);
        Check(frame.Length == 18 && Convert.ToBase64String(frame) ==
              Convert.ToBase64String(Receive(b, 16, match)), "Two clients received different player states");
        if (frame[0] == 0 && frame[1] == 0 && frame[2] == 0 && frame[3] == 1) {
            byte[] bombs = Receive(a, 17, match);
            Check(bombs.Length == 10 && Convert.ToBase64String(bombs) ==
                  Convert.ToBase64String(Receive(b, 17, match)), "Initial Bomb lists differ");
        }
        return frame;
    }
    static void MovementFlow(string directory)
    {
        using (App server = new App(Path.Combine(directory, "Server.exe"))) {
            server.Wait("Server listening");
            using (TcpClient first = Connect()) {
                NetworkStream a = first.GetStream();
                Send(a, 1, 0, 0, 1, 0, 11); Receive(a, 2); Receive(a, 3);
                using (TcpClient second = Connect()) {
                    NetworkStream b = second.GetStream();
                    Send(b, 1, 0, 0, 1, 0, 11); Receive(b, 2); Receive(b, 3); Receive(a, 3);
                    Send(a, 15, 0, 1, 2); Receive(a, 3); Receive(b, 3);
                    Send(a, 11, 0, 1); Receive(a, 3); Receive(b, 3);
                    Send(b, 11, 0, 1); Receive(a, 3); Receive(b, 3);
                    Receive(a, 6, 1); Receive(b, 6, 1);
                    // A structurally valid input for this match cannot move a loading player.
                    Send(a, 8, 1, Input(8, 900));
                    Send(a, 12, 1); Send(b, 12, 1);
                    Receive(a, 13, 1); Receive(b, 13, 1);
                    byte[] initial = ReadSamePlayers(a, b, 1);
                    Check(initial[4] == 8 && initial[5] == 5 && initial[11] == 11 &&
                          initial[12] == 14, "Wrong City spawn positions");
                    Thread.Sleep(150);
                    Check(first.Available == 0 && second.Available == 0, "Loading input moved players");

                    // P1 moves across the City lane to the map edge. An old or duplicate
                    // NONE must not stop it, including one sent before the first tick.
                    Send(a, 8, 1, Input(8, 2));
                    Send(a, 8, 1, Input(0, 2));
                    Send(a, 8, 1, Input(0, 1));
                    byte[] frame;
                    do {
                        frame = ReadSamePlayers(a, b, 1);
                        Check(frame[5] == 5 && frame[11] == 11 && frame[12] == 14,
                              "P1 input changed P2");
                    } while (frame[4] < 19);
                    Thread.Sleep(120);
                    Check(first.Available == 0 && second.Available == 0, "Player advanced past map edge");

                    Send(a, 8, 1, Input(0, 3));
                    Send(b, 8, 1, Input(4, 1));
                    frame = ReadSamePlayers(a, b, 1);
                    Check(frame[4] == 19 && frame[13] == 10 && frame[17] == 4,
                          "P2 did not move independently");
                    Send(b, 8, 1, Input(0, 2));
                    do { frame = ReadSamePlayers(a, b, 1); } while (frame[15] != 0 || frame[16] != 0);
                    Check(frame[11] < 11 && frame[12] == 14, "P2 did not finish its active tile");
                    // Consume frames already queued in transit, then verify the stop settled.
                    Thread.Sleep(160);
                    while (first.Available != 0) frame = ReadSamePlayers(a, b, 1);
                    Thread.Sleep(160);
                    Check(first.Available == 0 && second.Available == 0, "P2 kept moving after NONE");
                }
            }
            server.Stop();
        }
        Console.WriteLine("PASS: match movement, identical player frames, launch positions, edge, stale/duplicate sequence, NONE, P1/P2 independence");
    }
    static void StaleLoading(string directory)
    {
        using (App server = new App(Path.Combine(directory, "Server.exe"))) {
            server.Wait("Server listening");
            using (TcpClient first = Connect()) {
                NetworkStream a = first.GetStream();
                Send(a, 1, 0, 0, 1, 0, 11); Receive(a, 2); Receive(a, 3);
                using (TcpClient second = Connect()) {
                    NetworkStream b = second.GetStream();
                    Send(b, 1, 0, 0, 1, 0, 11); Receive(b, 2); Receive(b, 3); Receive(a, 3);
                    Send(b, 11, 0, 1); Receive(a, 3); Receive(b, 3);
                    // Both-ready then SELECT in one accumulated read: no loading tick in between.
                    byte[] ready = Frame(11, 0, new byte[] {1});
                    byte[] map = Frame(15, 0, new byte[] {1, 2});
                    byte[] merged = new byte[ready.Length + map.Length];
                    Buffer.BlockCopy(ready, 0, merged, 0, ready.Length);
                    Buffer.BlockCopy(map, 0, merged, ready.Length, map.Length);
                    a.Write(merged, 0, merged.Length);
                    CheckRoomState(Receive(a, 3), 2, 1, 1); CheckRoomState(Receive(b, 3), 2, 1, 1);
                    CheckRoomState(Receive(a, 3), 2, 0, 0, 2); CheckRoomState(Receive(b, 3), 2, 0, 0, 2);
                    server.Count("Loading:", 0);
                    Send(a, 15, 0, 1, 1); Receive(a, 3); Receive(b, 3);
                    Console.WriteLine("PASS: actual map change clears both ready flags before loading");

                    Send(b, 11, 0, 1); Receive(a, 3); Receive(b, 3);
                    byte[] color = Frame(15, 0, new byte[] {2, 6});
                    Buffer.BlockCopy(color, 0, merged, ready.Length, color.Length);
                    a.Write(merged, 0, merged.Length);
                    CheckRoomState(Receive(a, 3), 2, 1, 1); CheckRoomState(Receive(b, 3), 2, 1, 1);
                    CheckRoomState(Receive(a, 3), 2, 0, 1, 1, 6); CheckRoomState(Receive(b, 3), 2, 0, 1, 1, 6);
                    server.Count("Loading:", 0);
                    Send(b, 11, 0, 0); Receive(a, 3); Receive(b, 3);
                    Console.WriteLine("PASS: actual color change clears only the sender's ready flag before loading");
                    for (uint id = 1; id <= 2; ++id) {
                        if (id == 1) { Send(a, 11, 0, 1); Receive(a, 3); Receive(b, 3); }
                        Send(b, 11, 0, 1); Receive(a, 3); Receive(b, 3);
                        byte[] snapshotA = Receive(a, 6, id), snapshotB = Receive(b, 6, id);
                        Check(Convert.ToBase64String(snapshotA) == Convert.ToBase64String(snapshotB),
                            "Initial snapshots differ");
                        if (id == 1) {
                            Send(a, 12, id);
                            Send(b, 11, 0, 0);
                            Check(Receive(a, 14, id)[0] == 3, "Expected unready cancellation");
                            Receive(b, 14, id);
                            CheckRoomState(Receive(a, 3), 2, 1, 0, 1, 6);
                            CheckRoomState(Receive(b, 3), 2, 1, 0, 1, 6);
                            Send(b, 12, id); // Late completion while back in lobby.
                        } else {
                            Send(a, 12, 1); Send(a, 12, 1); // Previous attempt cannot load P1.
                            Send(b, 12, id);
                            server.Count("SC_MATCH_START", 0);
                            Check(first.Available == 0 && second.Available == 0, "Stale LOADED triggered a packet");
                            Send(a, 12, id);
                            Check(Receive(a, 13, id).Length == 0, "Start payload not empty");
                            Check(Receive(b, 13, id).Length == 0, "Start payload not empty");
                            ReadSamePlayers(a, b, id);
                            Send(a, 12, id); Send(b, 12, id);
                            Send(a, 8, 1, Input(8, 1)); // A valid direction for an earlier match is ignored.
                            server.Count("SC_MATCH_START: match=2", 1);
                            Check(first.Available == 0 && second.Available == 0, "Duplicate start");
                        }
                    }
                }
            }
            server.Stop();
        }
        Console.WriteLine("PASS: identical snapshot bytes; canceled/stale LOADED ignored in lobby and next match; start IDs/payload/duplicates");
    }
}
