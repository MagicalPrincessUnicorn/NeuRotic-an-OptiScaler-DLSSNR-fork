using System.Diagnostics;
using System.Reflection;
namespace NeuRotic.Discovery;

internal static class DiscoveryPerformanceTests
{
    public static async Task Run(string fixture)
    {
        Directory.CreateDirectory(fixture);
        var common=Path.Combine(fixture,"Steam","steamapps","common");
        Directory.CreateDirectory(common);
        var image=new byte[512];
        using(var writer=new BinaryWriter(new MemoryStream(image))){
            writer.Write((ushort)0x5a4d);writer.BaseStream.Position=0x3c;writer.Write(64);
            writer.BaseStream.Position=64;writer.Write(0x00004550);writer.Write((ushort)0x8664);writer.Write((ushort)1);
            writer.BaseStream.Position=84;writer.Write((ushort)240);writer.Write((ushort)2);writer.Write((ushort)0x20b);
        }
        for(var i=0;i<64;i++){
            var root=Path.Combine(common,$"Game{i:000}");Directory.CreateDirectory(root);
            File.WriteAllBytes(Path.Combine(root,"Game.exe"),image);
            for(var d=0;d<30;d++)Directory.CreateDirectory(Path.Combine(root,"Content",$"Data{d:00}"));
            File.WriteAllText(Path.Combine(fixture,"Steam","steamapps",$"appmanifest_{1000+i}.acf"),$"\"AppState\" {{ \"appid\" \"{1000+i}\" \"name\" \"Game{i:000}\" \"installdir\" \"Game{i:000}\" }}");
        }
        var watch=Stopwatch.StartNew();
        var result=await DiscoveryCoordinator.Discover([common],CancellationToken.None,fixture);
        Console.WriteLine($"PROFILE NH-SCAN-MANY: games=64 overlap=Steam/custom dataDirectories=1920 elapsedMs={watch.Elapsed.TotalMilliseconds:F1}");
        if(result.Games.Length!=64||result.Games.Any(g=>g.Store!="Steam"||g.Executable.Length==0))throw new Exception("Many-game scan lost Steam identity or executable");
        var policy=new ScanPolicy(CancellationToken.None);watch.Restart();
        foreach(var root in Directory.EnumerateDirectories(common)){
            var first=ExecutableResolver.Resolve(root,null,policy);
            var repeated=ExecutableResolver.Resolve(root,"Game.exe",policy);
            if(first.Executable!=repeated.Executable)throw new Exception("Overlapping resolution changed executable");
        }
        var visits=(int)typeof(ScanPolicy).GetField("directories",BindingFlags.Instance|BindingFlags.NonPublic)!.GetValue(policy)!;
        Console.WriteLine($"PROFILE NH-SCAN-CACHE: resolutions=128 directoriesVisited={visits} elapsedMs={watch.Elapsed.TotalMilliseconds:F1}");
        // Every install contains its root, Content and thirty data folders: 64 * 32.
        // A second caller must reuse the inventory instead of traversing 2048 folders again.
        if(visits!=2048)throw new Exception($"Overlapping launcher/custom resolution repeated directory traversal: expected 2048, got {visits}");
        Console.WriteLine("PASS NH-SCAN-CACHE: overlapping resolutions traverse each install once per scan");
        var ambiguous=Path.Combine(common,"Game000");File.WriteAllBytes(Path.Combine(ambiguous,"Alternative.exe"),image);
        var fresh=ExecutableResolver.Resolve(ambiguous,null,new(CancellationToken.None));
        if(fresh.Executable!=""||fresh.Candidates.Length!=2)throw new Exception("New scan reused a stale executable inventory or lost ambiguity");
        var metadata=ExecutableResolver.Resolve(ambiguous,"Alternative.exe",new(CancellationToken.None));
        if(metadata.Executable!=Path.Combine(ambiguous,"Alternative.exe")||metadata.Candidates.Length!=2)throw new Exception("Preferred executable bypassed legitimate alternatives");
        Console.WriteLine("PASS NH-SCAN-FRESH: fresh scans see added alternatives and metadata preserves both choices");
        var records=Enumerable.Range(0,12).Select(i=>new InstallRecord("Custom",i.ToString(),"Queue fixture",common)).ToArray();
        using var gate=new ManualResetEventSlim();var active=0;var peak=0;var completed=0;
        InstallResolutionQueue.Run(records,_=>{
            var now=Interlocked.Increment(ref active);
            int prior;do{prior=Volatile.Read(ref peak);if(prior>=now)break;}while(Interlocked.CompareExchange(ref peak,now,prior)!=prior);
            if(now==4)gate.Set();
            try{if(!gate.Wait(TimeSpan.FromSeconds(10)))throw new Exception("Serial installation resolution prevents independent game progress");Interlocked.Increment(ref completed);}
            finally{Interlocked.Decrement(ref active);}
        },CancellationToken.None);
        if(peak!=4||completed!=12)throw new Exception($"Unbounded or incomplete installation workers: peak={peak}, completed={completed}");
        Console.WriteLine("PASS NH-SCAN-THROUGHPUT: four independent installs progress concurrently, all twelve finish");
        var shared=new ScanPolicy(CancellationToken.None);
        InstallResolutionQueue.Run(records,_=>{
            var selection=ExecutableResolver.Resolve(ambiguous,null,shared);
            if(selection.Executable!=""||selection.Candidates.Length!=2)throw new Exception("Concurrent inventory reuse lost executable alternatives");
        },CancellationToken.None);
        visits=(int)typeof(ScanPolicy).GetField("directories",BindingFlags.Instance|BindingFlags.NonPublic)!.GetValue(shared)!;
        if(visits!=32)throw new Exception($"Concurrent callers duplicated executable inventory: expected 32, got {visits}");
        Console.WriteLine("PASS NH-SCAN-SINGLE-FLIGHT: concurrent callers share one exhaustive inventory");
        using var stop=new CancellationTokenSource();var started=0;
        try{
            InstallResolutionQueue.Run(records,_=>{Interlocked.Increment(ref started);stop.Cancel();},stop.Token);
            throw new Exception("Installation queue ignored cancellation");
        }catch(OperationCanceledException)when(stop.IsCancellationRequested){}
        if(started<1||started>4)throw new Exception($"Cancelled queue continued dispatching installations: started={started}");
        Console.WriteLine("PASS NH-SCAN-CANCEL-QUEUE: cancellation stops dispatch while retaining completed work");
    }
}
