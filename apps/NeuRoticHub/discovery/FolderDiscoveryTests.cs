using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
namespace NeuRotic.Discovery;
internal static class FolderDiscoveryTests
{
    public static async Task Run(string fixture)
    {
        Directory.CreateDirectory(fixture);
        var cmd=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),"cmd.exe");
        string Dir(string relative){var path=Path.GetFullPath(Path.Combine(fixture,relative));Directory.CreateDirectory(path);return path;}
        string Exe(string relative){var path=Path.GetFullPath(Path.Combine(fixture,relative));Directory.CreateDirectory(Path.GetDirectoryName(path)!);File.Copy(cmd,path,true);return path;}
        void Text(string relative,string text){var path=Path.Combine(fixture,relative);Directory.CreateDirectory(Path.GetDirectoryName(path)!);File.WriteAllText(path,text);}
        void Check(bool ok,string label){if(!ok)throw new Exception(label);Console.WriteLine("PASS "+label);}
        var added=Dir("Added");var sibling=Dir("AddedSibling");Exe("Added/Local/Local.exe");Exe("AddedSibling/Sibling/Sibling.exe");
        Exe("Steam/steamapps/common/SteamGame/SteamGame.exe");Text("Steam/steamapps/appmanifest_9.acf","\"AppState\" { \"appid\" \"9\" \"name\" \"Launcher Steam\" \"installdir\" \"SteamGame\" }");
        var library=Dir("SteamLibrary");Exe("SteamLibrary/steamapps/common/OtherSteam/OtherSteam.exe");Text("SteamLibrary/steamapps/appmanifest_10.acf","\"AppState\" { \"appid\" \"10\" \"name\" \"Library Steam\" \"installdir\" \"OtherSteam\" }");
        Text("Steam/steamapps/libraryfolders.vdf","\"libraryfolders\" { \"1\" { \"path\" \""+library.Replace("\\","\\\\")+"\" } }");
        var epic=Exe("Epic/Game/EpicGame.exe");Text("Epic/Manifests/game.item",JsonSerializer.Serialize(new{InstallLocation=Path.GetDirectoryName(epic),DisplayName="Launcher Epic",AppName="epic-id",LaunchExecutable="EpicGame.exe"}));
        Exe("EA Games/EAGame/EAGame.exe");Dir("Ubisoft Games");Dir("GOG");Dir("XboxGames");
        var before=Directory.EnumerateFiles(fixture,"*",SearchOption.AllDirectories).ToDictionary(p=>p,p=>(Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(p))),File.GetLastWriteTimeUtc(p)));
        var local=await DiscoveryCoordinator.DiscoverFolder([added],CancellationToken.None);
        Check(local.Games.Length==1&&local.Games[0].Store=="Custom"&&local.Games[0].Title=="Local"&&!local.Games.Any(g=>g.Store is "Steam" or "Epic"),"NH-09-FOLDER-ONLY: launcher fixtures never enter an added-folder result");
        Check(local.Directories is {Length:1}&&local.Directories[0]==new DiscoveryDirectory(added,"Custom",false),"NH-09-FOLDER-DIRECTORY: only the added root is reported");
        foreach(var roots in new string[]?[]{null,[],[added,sibling]}){
            try{await DiscoveryCoordinator.DiscoverFolder(roots,CancellationToken.None);throw new Exception("Invalid root count accepted");}catch(ArgumentException){}
        }
        Check(true,"NH-09-FOLDER-COUNT: zero/multiple roots refused");
        var full=await DiscoveryCoordinator.Discover([added,sibling],CancellationToken.None,fixture);
        Check(full.Games.Any(g=>g.Store=="Steam")&&full.Games.Any(g=>g.Store=="Epic")&&full.Games.Any(g=>g.Title=="Sibling"),"NH-09-FULL: fixture providers and custom roots remain connected");
        Check(full.Directories!.Any(d=>d.Path==Path.Combine(fixture,"Ubisoft Games")&&d.Source=="Ubisoft"&&d.Automatic)
            &&full.Directories!.Any(d=>d.Path==Path.Combine(fixture,"Epic")&&d.Source=="Epic")
            &&full.Directories!.Any(d=>d.Path==library&&d.Source=="Steam")
            &&full.Directories!.All(d=>!d.Path.EndsWith("Manifests")&&!d.Path.EndsWith("common")&&!d.Path.EndsWith("steamapps")),"NH-09-AUTOMATIC-DIRECTORIES: existing library roots reported without metadata or game folders");
        var excluded=new[]{added,Path.Combine(fixture,"EA Games"),library,Path.GetDirectoryName(epic)!};
        var filtered=await DiscoveryCoordinator.Discover([added,sibling],CancellationToken.None,fixture,excludedRoots:excluded);
        Check(filtered.Games.All(g=>!excluded.Any(p=>g.InstallRoot.Equals(p,StringComparison.OrdinalIgnoreCase)||ScanPolicy.Within(g.InstallRoot,p)))
            &&filtered.Games.Any(g=>g.Title=="Sibling")&&filtered.Games.Any(g=>g.StoreId=="9"),"NH-09-EXCLUSION: custom, automatic and manifest installs excluded with prefix boundary preserved");
        Check(filtered.Directories!.All(d=>!excluded.Any(p=>d.Path.Equals(p,StringComparison.OrdinalIgnoreCase)||ScanPolicy.Within(d.Path,p))),"NH-09-EXCLUDED-DIRECTORIES: removed roots are not reintroduced");
        var excludedPolicy=new ScanPolicy(CancellationToken.None,excluded);
        Check(!LauncherProviders.FolderGames(added,"Custom",excludedPolicy).Any()&&!LauncherProviders.FolderGames(Path.Combine(fixture,"EA Games"),"EA",excludedPolicy).Any()
            &&excludedPolicy.Directories.Length==0,"NH-09-SKIP-FOLDER-ROOTS: excluded roots bypass folder enumeration");
        Check(ExecutableResolver.Resolve(Path.Combine(added,"Local"),null,excludedPolicy).Candidates.Length==0
            &&(int)typeof(ScanPolicy).GetField("directories",BindingFlags.NonPublic|BindingFlags.Instance)!.GetValue(excludedPolicy)! == 0,"NH-09-SKIP-RESOLUTION: excluded installs bypass executable inventory");
        var metadata=new ScanPolicy(CancellationToken.None);metadata.ReportDirectory(added,"Custom",false);metadata.ReportDirectory(added.ToUpperInvariant(),"Steam");
        for(int i=0;i<150;i++)metadata.ReportDirectory(Dir("Metadata/Root"+i),"EA");
        Check(metadata.Directories.Length==128&&metadata.Directories.Count(d=>d.Path.Equals(added,StringComparison.OrdinalIgnoreCase))==1
            &&metadata.Directories.Single(d=>d.Path.Equals(added,StringComparison.OrdinalIgnoreCase)).Automatic
            &&Encoding.UTF8.GetByteCount(JsonSerializer.Serialize(metadata.Directories))<=65536&&metadata.Errors.Length>0,"NH-09-METADATA-BOUND: canonical dedup, automatic provenance and bounded directory output");
        using var cancelled=new CancellationTokenSource();cancelled.Cancel();var partial=await DiscoveryCoordinator.DiscoverFolder([added],cancelled.Token);
        Check(partial.Games.Length==0&&partial.Errors.Length>0,"NH-09-FOLDER-CANCEL: partial response retained");
        Check(before.All(f=>f.Value==(Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(f.Key))),File.GetLastWriteTimeUtc(f.Key))),"NH-09-READONLY: hashes and timestamps preserved");
    }
}
