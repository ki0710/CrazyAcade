// Compile with BombIntegrationTests.cs. Receiver model is a test oracle, not GUI code.
using System;

public static partial class BombIntegrationTests
{
    public static void RunFinalState(string directory) {
        // Existing Run covers P2 win and draw. Complete the missing P1-win direction.
        using(var pair=new Pair(directory,false)) {
            for(int expected=2;expected>=0;--expected) {
                pair.Input2(0,true);
                pair.Until(()=>pair.Count==1 && pair.BombState==1);
                pair.Until(()=>pair.Heart2==expected);
                Check(pair.Heart1==3,"P2 self-hit must not damage distant P1");
                if(expected==0) {
                    pair.Until(()=>pair.EndCount==1);
                    Check(pair.Result==1,"P1 win/P2 loss result missing");
                }
                pair.Until(()=>pair.Count==0);
                pair.Input1(0);pair.Input2(0);
                pair.NoMore(); // Unchanged inputs/finished state must not generate more state.
            }
        }
        Console.WriteLine("PASS: independent receiver worlds, P1 win/P2 loss, unchanged inputs and final cleanup");
    }
}
