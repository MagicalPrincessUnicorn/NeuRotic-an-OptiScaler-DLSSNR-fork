using System.Text.Json;
namespace NeuRotic.Discovery;
internal static class LibraryRootTests
{
    public static async Task Run(string fixture)
    {
        string Dir(string path){var full=Path.Combine(fixture,path);Directory.CreateDirectory(full);return full;}
        string Exe(string path){var full=Path.Combine(fixture,path);Directory.CreateDirectory(Path.GetDirectoryName(full)!);File.Copy(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),"cmd.exe"),full,true);return full;}
        void Text(string path,string text){var full=Path.Combine(fixture,path);Directory.CreateDirectory(Path.GetDirectoryName(full)!);File.WriteAllText(full,text);}
        void Check(bool value,string label){if(!value)throw new Exception(label);Console.WriteLine("PASS "+label);}
        var steam=Dir("Steam");var second=Dir("SecondSteam");
        Exe("Steam/steamapps/common/One/One.exe");Exe("SecondSteam/steamapps/common/Two/Two.exe");
        Text("Steam/steamapps/appmanifest_1.acf","\"AppState\" { \"appid\" \"1\" \"name\" \"One\" \"installdir\" \"One\" }");
        Text("SecondSteam/steamapps/appmanifest_2.acf","\"AppState\" { \"appid\" \"2\" \"name\" \"Two\" \"installdir\" \"Two\" }");
        Text("Steam/steamapps/libraryfolders.vdf","\"libraryfolders\" { \"1\" { \"path\" \""+second.Replace("\\","\\\\")+"\" } }");
        var epic=Exe("Epic/Game/Game.exe");Text("Epic/Manifests/game.item",JsonSerializer.Serialize(new{InstallLocation=Path.GetDirectoryName(epic),DisplayName="Epic game",AppName="epic",LaunchExecutable="Game.exe"}));
        Dir("GOG");Dir("EA Games");Dir("Ubisoft Games");Dir("XboxGames");
        var result=await DiscoveryCoordinator.Discover([],CancellationToken.None,fixture);
        var expected=new[]{steam,second,Path.Combine(fixture,"Epic"),Path.Combine(fixture,"GOG"),Path.Combine(fixture,"EA Games"),Path.Combine(fixture,"Ubisoft Games"),Path.Combine(fixture,"XboxGames")};
        Check(result.Directories!.Select(d=>d.Path).ToHashSet(StringComparer.OrdinalIgnoreCase).SetEquals(expected),"ROOTS-ONLY: actual libraries without game or metadata directories");
        var missing=await DiscoveryCoordinator.Discover([],CancellationToken.None,Dir("Missing"));Check(missing.Directories!.Length==0,"ROOTS-MISSING: absent providers produce no empty entries");
        var custom=Dir("Custom");Exe("Custom/Genre/Series/DeepGame/Game.exe");Exe("Custom/Launcher.exe");Text("Custom/Invalid.exe","not an executable");
        var nestedSteam=Dir("Custom/SteamLibrary");Exe("Custom/SteamLibrary/steamapps/common/Inside/Inside.exe");Text("Custom/SteamLibrary/steamapps/appmanifest_3.acf","\"AppState\" { \"appid\" \"3\" \"name\" \"Inside Steam\" \"installdir\" \"Inside\" }");
        Text("Custom/SteamLibrary/steamapps/libraryfolders.vdf","\"libraryfolders\" { \"1\" { \"path\" \""+second.Replace("\\","\\\\")+"\" } }");
        Exe("Custom/GogLibrary/GogGame/Game.exe");Text("Custom/GogLibrary/GogGame/goggame-4.info","{\"gameId\":\"4\",\"name\":\"Inside GOG\",\"playTasks\":[{\"isPrimary\":true,\"path\":\"Game.exe\"}]}");
        var local=await DiscoveryCoordinator.DiscoverFolder([custom],CancellationToken.None);
        Check(local.Games.Any(g=>g.Title=="DeepGame")&&local.Games.Any(g=>g.Store=="Steam"&&g.StoreId=="3")&&local.Games.Any(g=>g.Store=="GOG"&&g.StoreId=="4"),"ROOTS-CUSTOM: recursive folders preserve local provider identities");
        Check(local.Directories is {Length:1}&&local.Directories[0].Path==custom&&local.Games.All(g=>ScanPolicy.Within(g.InstallRoot,custom)),"ROOTS-CONTAINMENT: one added root never follows external Steam libraries");
        var filtered=await DiscoveryCoordinator.Discover([custom,custom.ToUpperInvariant()+"\\"],CancellationToken.None,fixture,excludedRoots:[Path.Combine(fixture,"Epic"),second,Path.Combine(custom,"Genre")]);
        Check(filtered.Games.All(g=>!ScanPolicy.Within(g.InstallRoot,Path.Combine(fixture,"Epic"))&&!ScanPolicy.Within(g.InstallRoot,second)&&!ScanPolicy.Within(g.InstallRoot,Path.Combine(custom,"Genre")))&&filtered.Directories!.Count(d=>d.Path.Equals(custom,StringComparison.OrdinalIgnoreCase))==1,"ROOTS-EXCLUDE-DEDUP: normalized roots and exclusions apply across providers");
    }
}
