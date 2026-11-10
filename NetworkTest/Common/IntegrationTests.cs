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
        public App(string executable)
        {
            Process = new Process();
            Process.StartInfo = new ProcessStartInfo(executable) {
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
            while (timer.ElapsedMilliseconds < 7000) {
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
    static byte[] Receive(NetworkStream stream, int type)
    {
        byte[] header = ReadExactly(stream, 10);
        int size = (header[0] << 24) | (header[1] << 16) | (header[2] << 8) | header[3];
        Check(size >= 10 && size <= 1048576, "Invalid length");
        Check(((header[4] << 8) | header[5]) == type, "Unexpected packet type");
        Check(header[6] == 0 && header[7] == 0 && header[8] == 0 && header[9] == 0, "matchId != 0");
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
            first.Wait("SC_SNAPSHOT: stage=1 map=20x20");
            second.Wait("SC_SNAPSHOT: stage=1 map=20x20");
            App third = new App(Path.Combine(buildDirectory, "Client.exe")); apps.Add(third);
            third.Wait("SC_ERROR: Room full"); third.Exited(2);
            Console.WriteLine("PASS: two real clients, Player IDs, room broadcasts, third-client rejection");

            first.Stop();
            second.Wait("SC_PEER_LEFT: Player ID=1"); second.Wait("SC_ROOM_STATE: count=1");
            int secondMark = second.Mark;
            App replacement = new App(Path.Combine(buildDirectory, "Client.exe")); apps.Add(replacement);
            replacement.Wait("SC_WELCOME: Player ID=1"); replacement.Wait("SC_ROOM_STATE: count=2");
            second.Wait("SC_ROOM_STATE: count=2", secondMark);
            Console.WriteLine("PASS: normal client exit, peer-left notification, slot reuse");
            int mark = server.Mark;
            replacement.Stop(); second.Stop();
            server.Wait("SC_ROOM_STATE: count=0", mark);

            ErrorCase(Frame(1, 0, new byte[] {0, 2, 0, 2}), 3);
            ErrorCase(Frame(1, 0, new byte[] {0, 1, 0, 1}), 3);
            ErrorCase(Frame(1, 1, new byte[] {0, 1, 0, 2}), 2);
            ErrorCase(Frame(2, 0, new byte[] {1}), 2);
            ErrorCase(Frame(1, 0, new byte[] {0}), 2);
            byte[] badLength = Frame(1, 0, new byte[0]); badLength[3] = 9;
            ErrorCase(badLength, 2);
            badLength[0] = 1;
            ErrorCase(badLength, 2);
            ErrorCase(Frame(99, 0, new byte[0]), 2);
            Console.WriteLine("PASS: version, matchId, direction, payload, length and type validation");

            mark = server.Mark;
            using (TcpClient raw = Connect()) {
                NetworkStream stream = raw.GetStream();
                byte[] hello = Frame(1, 0, new byte[] {0, 1, 0, 2});
                // Multiple writes exercise the live accumulated receive path.
                for (int i = 0; i < hello.Length; ++i) stream.Write(hello, i, 1);
                Check(Receive(stream, 2)[0] == 1, "Fragmented HELLO not accepted");
                Check(Receive(stream, 3)[0] == 1, "Missing room state");
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
                byte[] hello = Frame(1, 0, new byte[] {0, 1, 0, 2});
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
        } finally {
            for (int i = apps.Count - 1; i >= 0; --i) apps[i].Dispose();
        }
    }
}
