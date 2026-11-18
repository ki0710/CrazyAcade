// Live TCP checks against the production server. Run after IntegrationTests.Run.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net.Sockets;
using System.Threading;

public static class BombIntegrationTests
{
    sealed class Pair : IDisposable {
        readonly Process server;
        readonly TcpClient first, second;
        readonly NetworkStream a, b;
        uint match;
        public byte[] Positions;
        public byte[] Bombs;
        public int Heart1 = 3, Heart2 = 3, Result, EndCount;
        public uint Seq1, Seq2;
        uint lastBombSequence;
        public List<int> Route;
        public Pair(string directory, bool needRoute) {
            server = Process.Start(new ProcessStartInfo(Path.Combine(directory, "Server.exe")) {
                UseShellExecute=false, CreateNoWindow=true,
                RedirectStandardInput=true, RedirectStandardOutput=true
            });
            try {
                var listening = server.StandardOutput.ReadLineAsync();
                Check(listening.Wait(5000) && listening.Result != null &&
                      listening.Result.Contains("Server listening"), "server startup");
                first = Connect(); a = first.GetStream();
                Send(a, 1, 0, new byte[]{0,1,0,11}); Receive(a,2,0); Receive(a,3,0);
                second = Connect(); b = second.GetStream();
                Send(b,1,0,new byte[]{0,1,0,11}); Receive(b,2,0); Receive(b,3,0); Receive(a,3,0);
                Send(a,15,0,new byte[]{1,3}); Receive(a,3,0); Receive(b,3,0);
                for (int attempt=0; attempt<20; ++attempt) {
                    Send(a,11,0,new byte[]{1}); Receive(a,3,0); Receive(b,3,0);
                    Send(b,11,0,new byte[]{1}); Receive(a,3,0); Receive(b,3,0);
                    ++match;
                    byte[] map=Receive(a,6,match), other=Receive(b,6,match);
                    Check(Same(map,other),"initial maps differ");
                    Receive(a,9,match); Receive(b,9,match);
                    Route = FindRoute(map);
                    if (!needRoute || Route != null) break;
                    Send(a,11,0,new byte[]{0}); Receive(a,14,match); Receive(b,14,match);
                    Receive(a,3,0); Receive(b,3,0);
                }
                Check(!needRoute || Route != null,"no walkable Ice route in 20 randomized maps");
                // Loading input must never place a Bomb or consume the in-match sequence.
                Send(a,8,match,Input(0,900,true));
                Send(a,12,match,new byte[0]); Send(b,12,match,new byte[0]);
                Receive(a,13,match); Receive(b,13,match);
                Positions=SameFrame(16);
                Bombs=SameFrame(17);
                Check(Count == 0,"loading input created a Bomb");
                lastBombSequence=Read32(Bombs,4);
            } catch { Dispose(); throw; }
        }
        static TcpClient Connect() {
            var client=new TcpClient();
            client.Connect("127.0.0.1",9000);
            client.GetStream().ReadTimeout=7000;
            return client;
        }
        public int Count { get { return (Bombs[8]<<8)|Bombs[9]; } }
        public int BombState { get { return Count==0 ? 0 : Bombs[17]; } }
        public uint BombId { get { return Count==0 ? 0 : Read32(Bombs,10); } }
        byte[] SameFrame(int type) {
            var data=Receive(a,type,match);
            Check(Same(data,Receive(b,type,match)),"broadcast mismatch, type="+type);
            return data;
        }
        public void Input1(byte direction, bool place=false) { Send(a,8,match,Input(direction,++Seq1,place)); }
        public void Input2(byte direction, bool place=false) { Send(b,8,match,Input(direction,++Seq2,place)); }
        public void Replay1(uint seq) { Send(a,8,match,Input(0,seq,true)); }
        public void ReadPair() {
            int type;
            var data=Read(a,out type,match);
            Check(Same(data,Receive(b,type,match)),"broadcast mismatch type="+type);
            if(type==16) Positions=data;
            else if(type==17) {
                Check(data.Length==10+17*((data[8]<<8)|data[9]) && Read32(data,4)>lastBombSequence,
                      "Bomb length/sequence");
                lastBombSequence=Read32(data,4); Bombs=data;
            } else if(type==9) { Heart1=data[0]; Heart2=data[1]; Result=data[2]; }
            else if(type==10) {
                Check(data[0]==Heart1 && data[1]==Heart2 && data[2]==Result,"end before final hearts");
                ++EndCount;
            } else Check(type==7,"unexpected broadcast "+type);
        }
        public void Until(Func<bool> predicate) {
            var timer=Stopwatch.StartNew();
            while(!predicate()) {
                Check(timer.ElapsedMilliseconds<7000,"state wait timeout");
                ReadPair();
            }
        }
        public void NoMore() {
            Thread.Sleep(150);
            Check(first.Available==0 && second.Available==0,"unexpected update after settling");
        }
        public void WalkRoute() {
            foreach(int encoded in Route) {
                int col=encoded%20,row=encoded/20;
                int x=Positions[11], y=Positions[12];
                byte dir = col>x ? (byte)8 : col<x ? (byte)4 : row>y ? (byte)2 : (byte)1;
                Input2(dir);
                Until(()=>Positions[13]==col && Positions[14]==row);
                Input2(0);
                Until(()=>Positions[11]==col && Positions[12]==row &&
                          Positions[15]==0 && Positions[16]==0);
            }
            Check(Positions[11]==6 && Positions[12]==5,"P2 did not arrive at P1");
        }
        public void Dispose() {
            if(first!=null) first.Close();
            if(second!=null) second.Close();
            if(server!=null) {
                if(!server.HasExited) {
                    server.StandardInput.WriteLine(); server.StandardInput.Flush();
                    if(!server.WaitForExit(5000)){server.Kill();server.WaitForExit();}
                }
                server.Dispose();
            }
        }
    }
    static int checks;
    static void Check(bool value,string message) { ++checks; if(!value)throw new Exception(message); }
    static uint Read32(byte[] b,int n) { return ((uint)b[n]<<24)|((uint)b[n+1]<<16)|((uint)b[n+2]<<8)|b[n+3]; }
    static bool Same(byte[] a,byte[] b) { return Convert.ToBase64String(a)==Convert.ToBase64String(b); }
    static byte[] Exactly(NetworkStream s,int length) {
        var b=new byte[length];
        for(int n=0;n<length;){int got=s.Read(b,n,length-n);Check(got>0,"unexpected EOF");n+=got;}
        return b;
    }
    static byte[] Read(NetworkStream s,out int type,uint match) {
        var head=Exactly(s,10);
        int size=(int)Read32(head,0);
        Check(size>=10 && size<=1048576 && Read32(head,6)==match,"bad live header");
        type=(head[4]<<8)|head[5];
        return Exactly(s,size-10);
    }
    static byte[] Receive(NetworkStream s,int expected,uint match) {
        int type;var b=Read(s,out type,match);
        Check(type==expected,"expected type "+expected+", got "+type);
        return b;
    }
    static void Send(NetworkStream s,int type,uint match,byte[] payload) {
        int length=10+payload.Length;
        var b=new byte[length];
        b[0]=(byte)(length>>24);b[1]=(byte)(length>>16);b[2]=(byte)(length>>8);b[3]=(byte)length;
        b[4]=(byte)(type>>8);b[5]=(byte)type;
        b[6]=(byte)(match>>24);b[7]=(byte)(match>>16);b[8]=(byte)(match>>8);b[9]=(byte)match;
        Buffer.BlockCopy(payload,0,b,10,payload.Length);s.Write(b,0,length);
    }
    static byte[] Input(byte direction,uint sequence,bool place) {
        return new byte[]{direction,(byte)(sequence>>24),(byte)(sequence>>16),(byte)(sequence>>8),
                          (byte)sequence,(byte)(place?1:0)};
    }
    static List<int> FindRoute(byte[] snapshot) {
        int start=5*20+13, goal=5*20+6;
        var prev=new int[400];for(int i=0;i<400;++i)prev[i]=-1;
        var q=new Queue<int>();q.Enqueue(start);prev[start]=start;
        while(q.Count>0 && prev[goal]<0) {
            int p=q.Dequeue(),x=p%20,y=p/20;
            foreach(int next in new int[]{x>0?p-1:-1,x<19?p+1:-1,y>0?p-20:-1,y<19?p+20:-1}) {
                if(next<0 || prev[next]>=0 || snapshot[1+4*next]!=0)continue;
                prev[next]=p;q.Enqueue(next);
            }
        }
        if(prev[goal]<0)return null;
        var route=new List<int>();for(int p=goal;p!=start;p=prev[p])route.Add(p);
        route.Reverse();return route;
    }
    public static void Run(string directory) {
        using(var pair=new Pair(directory,false)) {
            uint oldId=0;
            for(int expected=2;expected>=0;--expected) {
                pair.Input1(0,true);
                uint accepted=pair.Seq1;
                pair.Input1(0); // A later direction state cannot overwrite the earlier placement event.
                pair.Until(()=>pair.Count==1 && pair.BombState==1 && pair.BombId>oldId);
                oldId=pair.BombId;
                Check(pair.Bombs[14]==1 && pair.Bombs[15]==6 && pair.Bombs[16]==5,"wrong owner/row/column");
                pair.Replay1(accepted);pair.Replay1(accepted-1);
                pair.Input1(0,true); // Failed same-cell request is consumed.
                uint failed=pair.Seq1;
                pair.Until(()=>pair.BombState==2);
                Check(pair.Count==1,"duplicate request placed extra Bomb");
                pair.Replay1(failed);
                pair.Until(()=>pair.Heart1==expected);
                Check(pair.Heart2==3,"far opponent was hit");
                if(expected==0) {
                    pair.Until(()=>pair.EndCount==1);
                    Check(pair.Result==2,"wrong winner");
                    pair.Input1(8,true);pair.Input2(4,true);
                }
                pair.Until(()=>pair.Count==0);
                Check(pair.EndCount==(expected==0?1:0),"match end sent more than once");
                pair.NoMore();
            }
        }
        Console.WriteLine("PASS: TCP Bomb IDs, identical views, deadlines, consumed failed/duplicate requests, self hits 3/2/1/0, one result and finished cleanup.");
        using(var pair=new Pair(directory,true)) {
            pair.WalkRoute(); // Actual server movement, no test-only world mutation.
            for(int expected=2;expected>=0;--expected) {
                pair.Input1(0,true);
                pair.Until(()=>pair.Count==1 && pair.BombState==1);
                pair.Until(()=>pair.Heart1==expected && pair.Heart2==expected);
                if(expected==0) {
                    pair.Until(()=>pair.EndCount==1);
                    Check(pair.Result==3,"same-tick hits did not produce a draw");
                }
                pair.Until(()=>pair.Count==0);
                pair.NoMore();
            }
        }
        Console.WriteLine("PASS: server-moved P2, self/opponent hits in the same tick, equal final hearts and a single TCP draw.");
        Console.WriteLine("Bomb TCP checks passed: "+checks);
    }
}
