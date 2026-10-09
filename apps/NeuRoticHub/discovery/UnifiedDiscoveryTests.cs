using System.Security.Cryptography;
using System.Text.Json;
namespace NeuRotic.Discovery;
internal static class UnifiedDiscoveryTests
{
    public static async Task Run(string fixture)
    {
        Directory.CreateDirectory(fixture);
        var cmd=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),"cmd.exe");
        string Exe(string relative){var path=Path.GetFullPath(Path.Combine(fixture,relative));Directory.CreateDirectory(Path.GetDirectoryName(path)!);File.Copy(cmd,path,true);return path;}
        void Text(string relative,string text){var path=Path.Combine(fixture,relative);Directory.CreateDirectory(Path.GetDirectoryName(path)!);File.WriteAllText(path,text);}
        void Check(bool ok,string label){if(!ok)throw new Exception(label);Console.WriteLine("PASS "+label);}
        var portal=Path.Combine(fixture,"Legacy","Portal2","portal2.exe");Directory.CreateDirectory(Path.GetDirectoryName(portal)!);
        var pe32=new byte[512];using(var writer=new BinaryWriter(new MemoryStream(pe32))){writer.Write((ushort)0x5a4d);writer.BaseStream.Position=0x3c;writer.Write(64);writer.BaseStream.Position=64;writer.Write(0x4550);writer.Write((ushort)0x14c);writer.Write((ushort)1);writer.BaseStream.Position=84;writer.Write((ushort)224);writer.Write((ushort)2);writer.Write((ushort)0x10b);}File.WriteAllBytes(portal,pe32);
        Check(ExecutableResolver.Resolve(Path.GetDirectoryName(portal)!,null,new(CancellationToken.None)).Executable==portal,"NH-X86-DISCOVERY: PE32 Portal2 executable selected");
        Check(ExecutableResolver.Bitness(portal,new(CancellationToken.None))==32,"NH-X86-BITNESS: PE32 architecture retained");
        var invalidRoot=Path.Combine(fixture,"InvalidPE");Directory.CreateDirectory(invalidRoot);
        foreach(var (name,machine,magic,flags,length) in new[]{("Mismatch",0x8664,0x10b,2,512),("ARM",0xaa64,0x20b,2,512),("DLL",0x14c,0x10b,0x2002,512),("Truncated",0x14c,0x10b,2,90),("NotExecutable",0x14c,0x10b,0,512)}){
            var bytes=(byte[])pe32.Clone();using(var writer=new BinaryWriter(new MemoryStream(bytes))){writer.BaseStream.Position=68;writer.Write((ushort)machine);writer.BaseStream.Position=86;writer.Write((ushort)flags);writer.Write((ushort)magic);}var invalid=Path.Combine(invalidRoot,name+".exe");File.WriteAllBytes(invalid,bytes[..length]);Check(ExecutableResolver.Bitness(invalid,new(CancellationToken.None))==0,"NH-PE-REFUSAL: "+name);
        }
        Check(ExecutableResolver.Resolve(invalidRoot,null,new(CancellationToken.None)).Candidates.Length==0,"NH-PE-INVENTORY: invalid architectures excluded");
        Exe("Steam/steamapps/common/NoDLSS/Game.exe");Text("Steam/steamapps/appmanifest_42.acf","\"AppState\" { \"appid\" \"42\" \"name\" \"Steam without DLSS\" \"installdir\" \"NoDLSS\" }");
        var epic=Exe("Epic/Game/EpicGame.exe");Text("Epic/Manifests/game.item",JsonSerializer.Serialize(new{InstallLocation=Path.GetDirectoryName(epic),DisplayName="Epic fixture",AppName="epic-id",LaunchExecutable="EpicGame.exe"}));
        Text("Epic/Manifests/bad.item","not json");
        Exe("GOG/GogGame/GogGame.exe");Text("GOG/GogGame/goggame-7.info","{\"gameId\":\"7\",\"name\":\"GOG fixture\",\"playTasks\":[{\"isPrimary\":true,\"path\":\"GogGame.exe\"}]}");
        Exe("XboxGames/XboxGame/Content/XboxGame.exe");Text("XboxGames/XboxGame/Content/MicrosoftGame.config","<Game><Identity Name=\"Xbox-id\"/><ShellVisuals DefaultDisplayName=\"Xbox fixture\"/><Executable Name=\"XboxGame.exe\"/></Game>");
        Exe("EA Games/EA fixture/EAGame.exe");Exe("Ubisoft Games/Ubisoft fixture/UbiGame.exe");
        Exe("Custom/Nested/Nested.exe");var runtime=Exe("Custom/Nested/Binaries/Win64/Nested-Win64-Shipping.exe");
        Exe("Custom/Ambiguous/A.exe");Exe("Custom/Ambiguous/B.exe");Exe("Custom/Helpers/CrashReporter.exe");
        Text("Custom/Invalid/Invalid.exe","not an executable");
        var before=Directory.EnumerateFiles(fixture,"*",SearchOption.AllDirectories).ToDictionary(p=>p,p=>Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(p))));
        var result=await DiscoveryCoordinator.Discover([Path.Combine(fixture,"Custom"),Path.GetDirectoryName(epic)!],CancellationToken.None,fixture);
        Check(new[]{"Steam","Epic","GOG","Xbox","EA","Ubisoft","Custom"}.All(s=>result.Games.Any(g=>g.Store==s)),"NH-07-STORES: all six launchers and custom folder fixtures");
        Check(result.Games.Count(g=>g.InstallRoot==Path.GetDirectoryName(epic))==1,"NH-07-DEDUP: overlapping custom and launcher root");
        var steamGameRoot=Path.GetFullPath(Path.Combine(fixture,"Steam/steamapps/common/NoDLSS"));var steamOverlap=await DiscoveryCoordinator.Discover([steamGameRoot],CancellationToken.None,fixture);Check(steamOverlap.Games.Count(g=>g.InstallRoot==steamGameRoot)==1&&steamOverlap.Games.Any(g=>g.Store=="Steam"&&g.StoreId=="42"&&!string.IsNullOrEmpty(g.SteamRoot)),"NH-07-STEAM-IDENTITY: custom overlap retains Steam metadata");
        var nestedGame=result.Games.First(g=>g.Title=="Nested");if(nestedGame.Executable!=runtime)Console.WriteLine(JsonSerializer.Serialize(new { expected=runtime,actual=nestedGame }));
        Check(nestedGame.Executable==runtime,"NH-07-RUNTIME: nested Win64 selected over bootstrap");
        var overlap=await DiscoveryCoordinator.Discover([Path.Combine(fixture,"Custom/Nested"),Path.GetDirectoryName(runtime)!],CancellationToken.None,fixture);Check(overlap.Games.Count(g=>g.Executable==runtime)==1,"NH-07-DEDUP-NESTED: identical runtime under nested roots appears once");
        Exe("Titles/HaloReach/HaloReach.exe");Exe("Titles/CrashBandicoot/CrashBandicoot.exe");var titles=await DiscoveryCoordinator.Discover([Path.Combine(fixture,"Titles")],CancellationToken.None,fixture);Check(titles.Games.Count(g=>g.Store=="Custom")==2,"NH-07-TITLES: helper-like substrings do not exclude games");
        var ambiguous=result.Games.First(g=>g.Title=="Ambiguous");Check(ambiguous.Executable==""&&ambiguous.ExecutableCandidates?.Length==2,"NH-07-AMBIGUITY: plausible candidates require a choice");
        Check(!result.Games.Any(g=>g.Title is "Helpers" or "Invalid"),"NH-07-HELPERS: support and malformed binaries excluded");
        Check(result.Errors.Length>0&&result.Games.Any(g=>g.Title=="Steam without DLSS"),"NH-07-PARTIAL: malformed source preserves non-DLSS games");
        Check(before.All(f=>f.Value==Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(f.Key)))),"NH-07-READONLY: source file hashes preserved");
        var root=Path.Combine(fixture,"Depth");Exe("Depth/a/b/c/d/e/f/g/h/i/TooDeep.exe");Check(ExecutableResolver.Resolve(root,null,new(CancellationToken.None)).Candidates.Length==0,"NH-07-DEPTH: ninth-level executable excluded");
        var outside=Exe("Outside/Outside.exe");var chosen=ExecutableResolver.Resolve(Path.GetDirectoryName(epic)!,outside,new(CancellationToken.None));Check(chosen.Executable==epic,"NH-07-CONTAINMENT: metadata cannot select an outside executable");
        var linked=Path.Combine(fixture,"Linked");try{Directory.CreateSymbolicLink(linked,Path.GetDirectoryName(epic)!);Check(ExecutableResolver.Resolve(linked,null,new(CancellationToken.None)).Candidates.Length==0,"NH-07-LINKS: linked roots refused");Directory.Delete(linked);}catch(Exception e)when(e is UnauthorizedAccessException || (e is IOException && (e.HResult&65535)==1314)){Console.WriteLine("LIMIT NH-07-LINKS: symbolic-link creation unavailable; ancestor checks remain enabled");}
        using var cancelled=new CancellationTokenSource();cancelled.Cancel();var partial=await DiscoveryCoordinator.Discover([fixture],cancelled.Token,fixture);Check(partial.Games.Length==0&&partial.Errors.Length>0,"NH-07-CANCEL: bounded partial response");
        using var midway=new CancellationTokenSource(150);var slowSteam=Task.Run(async()=>{await Task.Delay(200);return new DiscoveryResult(1,"DiscoveryResult",[new("partial-steam","Steam","42","Partial Steam",Path.Combine(fixture,"Steam/steamapps/common/NoDLSS"),"Available","Pending")],[]);});var retained=await DiscoveryCoordinator.Discover([],midway.Token,fixture,slowSteam);Check(retained.Games.Any(g=>g.Title=="Partial Steam")&&retained.Games.Any(g=>g.Store=="Epic"),"NH-07-DEADLINE: delayed Steam partial and healthy other stores retained");
        var policy=new ScanPolicy(CancellationToken.None);for(int i=0;i<10000;i++)policy.CandidateVisited();try{policy.CandidateVisited();throw new Exception("Candidate budget not enforced");}catch(IOException){Console.WriteLine("PASS NH-07-BUDGET: executable cap enforced");}
    }
}
