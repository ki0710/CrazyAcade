// Live TCP checks against the production server. Run after IntegrationTests.Run.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net.Sockets;
using System.Threading;
using System.Text;

public static partial class BombIntegrationTests
{
    sealed class ReceivedWorld {
        readonly int[] obstacles = new int[400], items = new int[400];
        readonly int[,] players = new int[2,7];
        readonly SortedDictionary<uint,string> bombs = new SortedDictionary<uint,string>();
        string lastPlayers, lastBombs, lastHearts;
        uint tick, sequence;
        int heart1, heart2, result, ends;
        static string Slice(byte[] bytes,int offset) {
            return Convert.ToBase64String(bytes,offset,bytes.Length-offset);
        }
        public void Apply(int type,byte[] data) {
            if(type==6) {
                Check(data.Length==1614 && data[0]==3,"receiver expects Ice snapshot");
                for(int i=0;i<400;++i) { obstacles[i]=data[1+4*i];items[i]=data[4+4*i]; }
                heart1=data[1611];heart2=data[1612];result=data[1613];
                Check(heart1==3 && heart2==3 && result==0,"fresh Ice state");
                Array.Clear(players,0,players.Length);bombs.Clear();
                lastPlayers=lastBombs=lastHearts=null;tick=sequence=0;ends=0;
            } else if(type==16) {
                Check(data.Length==18 && Read32(data,0)>tick,"receiver movement tick");
                string body=Slice(data,4);
                Check(body!=lastPlayers,"duplicate unchanged Player Broadcast body");
                tick=Read32(data,0);lastPlayers=body;
                for(int p=0;p<2;++p) for(int n=0;n<7;++n) players[p,n]=data[4+7*p+n];
            } else if(type==17) {
                Check(data.Length>=10 && Read32(data,4)>sequence,"receiver Bomb sequence");
                int count=(data[8]<<8)|data[9];
                Check(data.Length==10+17*count,"receiver Bomb length");
                string body=Slice(data,8);
                Check(body!=lastBombs,"duplicate unchanged Bomb Broadcast body");
                lastBombs=body;sequence=Read32(data,4);bombs.Clear();
                for(int i=0;i<400;++i) if(obstacles[i]==9)obstacles[i]=0;
                for(int i=0;i<count;++i) {
                    int offset=10+17*i;
                    uint id=Read32(data,offset);
                    Check(!bombs.ContainsKey(id),"receiver duplicate Bomb ID");
                    bombs.Add(id,Convert.ToBase64String(data,offset,17));
                    int cell=20*data[offset+6]+data[offset+5];
                    Check(cell<400,"receiver Bomb cell");
                    if(data[offset+7]==1)obstacles[cell]=9;
                }
            } else if(type==7) {
                Check(data.Length==4 && data[0]==1,"receiver obstacle event");
                int cell=((data[1]<<8)|data[2])-1;
                Check(cell>=0 && cell<400,"receiver event cell");
                obstacles[cell]=0;items[cell]=data[3];
            } else if(type==9 || type==10) {
                Check(data.Length==3,"receiver Ice state size");
                if(type==9) {
                    string body=Slice(data,0);
                    Check(body!=lastHearts,"duplicate unchanged Ice state Broadcast");
                    lastHearts=body;heart1=data[0];heart2=data[1];result=data[2];
                } else {
                    Check(data[0]==heart1 && data[1]==heart2 && data[2]==result && result!=0,
                          "receiver result must follow final state");
                    Check(++ends==1,"receiver duplicate match result");
                }
            } else Check(false,"unexpected receiver state packet");
        }
        public string Key() {
            var key=new StringBuilder();
            for(int i=0;i<400;++i)key.Append(obstacles[i]).Append(',').Append(items[i]).Append(';');
            foreach(int value in players)key.Append(value).Append(',');
            foreach(var bomb in bombs)key.Append(bomb.Key).Append(':').Append(bomb.Value).Append(';');
            return key.Append('|').Append(heart1).Append(',').Append(heart2).Append(',')
                .Append(result).Append(',').Append(ends).ToString();
        }
    }
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
        readonly ReceivedWorld worldA = new ReceivedWorld(), worldB = new ReceivedWorld();
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
                Send(a, 1, 0, new byte[]{0,1,0,12}); Receive(a,2,0); Receive(a,3,0);
                second = Connect(); b = second.GetStream();
                Send(b,1,0,new byte[]{0,1,0,12}); Receive(b,2,0); Receive(b,3,0); Receive(a,3,0);
                Send(a,15,0,new byte[]{1,3}); Receive(a,3,0); Receive(b,3,0);
                for (int attempt=0; attempt<20; ++attempt) {
                    Send(a,11,0,new byte[]{1}); Receive(a,3,0); Receive(b,3,0);
                    if (attempt == 0) {
                        Send(b,11,0,new byte[]{1}); Receive(a,3,0); Receive(b,3,0);
                    }
                    ++match;
                    byte[] map=Receive(a,6,match), other=Receive(b,6,match);
                    Check(Same(map,other),"initial maps differ");
                    worldA.Apply(6,map); worldB.Apply(6,other);
                    Receive(a,9,match); Receive(b,9,match);
                    Route = FindRoute(map);
                    if (!needRoute || Route != null) break;
                    Send(a,11,0,new byte[]{0}); Receive(a,14,match); Receive(b,14,match);
                    byte[] room=Receive(a,3,0), otherRoom=Receive(b,3,0);
                    Check(Same(room,otherRoom) && room[1]==0 && room[2]==1,
                          "Ice map retry must preserve the peer's ready flag");
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
            var other=Receive(b,type,match);
            Check(Same(data,other),"broadcast mismatch, type="+type);
            CompareWorlds(type,data,other);
            return data;
        }
        void CompareWorlds(int type, byte[] data, byte[] other) {
            worldA.Apply(type,data); worldB.Apply(type,other);
            Check(worldA.Key()==worldB.Key(),"independently reconstructed receiver worlds differ");
        }
        public void Input1(byte direction, bool place=false) { Send(a,8,match,Input(direction,++Seq1,place)); }
        public void Input2(byte direction, bool place=false) { Send(b,8,match,Input(direction,++Seq2,place)); }
        public void Replay1(uint seq) { Send(a,8,match,Input(0,seq,true)); }
        public void ReadPair() {
            int type;
            var data=Read(a,out type,match);
            var other=Receive(b,type,match);
            Check(Same(data,other),"broadcast mismatch type="+type);
            CompareWorlds(type,data,other);
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
