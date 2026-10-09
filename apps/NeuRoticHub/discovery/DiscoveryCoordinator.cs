using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
namespace NeuRotic.Discovery;
internal static class DiscoveryCoordinator
{
    public static Task<DiscoveryResult> DiscoverFolder(string[]? roots,CancellationToken cancel,string[]? excludedRoots=null)
    {
        if(roots is not {Length:1} || !ScanPolicy.LocalRoot(roots[0]))throw new ArgumentException("DiscoverFolder requires exactly one absolute local folder");
        return Discover(roots,cancel,excludedRoots:excludedRoots,folderOnly:true);
    }
    public static async Task<DiscoveryResult> Discover(string[]? customRoots,CancellationToken cancel,string? fixtureRoot=null,Task<DiscoveryResult>? steamFixture=null,string[]? excludedRoots=null,bool folderOnly=false)
    {
        if(customRoots is {Length:>64} || customRoots?.Any(p=>!ScanPolicy.LocalRoot(p))==true)throw new ArgumentException("Invalid custom search roots");
        if(excludedRoots is {Length:>128} || excludedRoots?.Any(p=>!ScanPolicy.LocalRoot(p))==true)throw new ArgumentException("Invalid excluded roots");
        var policy=new ScanPolicy(cancel,excludedRoots);var games=new Dictionary<string,GameCandidate>(StringComparer.OrdinalIgnoreCase);var installs=new HashSet<string>(StringComparer.OrdinalIgnoreCase);var sync=new object();long bytes=0;
        void Add(GameCandidate game){
            var root=Path.GetFullPath(game.InstallRoot).TrimEnd('\\','/');
            if(game.Title.Length>4096||root.Length>32768)return;
            lock(sync){
                var prior=games.FirstOrDefault(p=>p.Key.Equals(root,StringComparison.OrdinalIgnoreCase)||(game.Executable.Length>0&&p.Value.Executable.Equals(game.Executable,StringComparison.OrdinalIgnoreCase)));
                if(prior.Value!=null&&!(prior.Value.Store=="Custom"&&game.Store!="Custom"))return;
                var cost=Encoding.UTF8.GetByteCount(JsonSerializer.Serialize(game))+1;
                var previousCost=prior.Value==null?0:Encoding.UTF8.GetByteCount(JsonSerializer.Serialize(prior.Value))+1;
                if(bytes-previousCost+cost>786432){policy.Warn("Result budget reached; partial games retained");return;}
                if(prior.Value!=null){games.Remove(prior.Key);bytes-=previousCost;}
                games[root]=game;bytes+=cost;
            }
        }
        bool Admit(InstallRecord record){
            var key=Path.GetFullPath(record.Root).TrimEnd('\\','/');
            if(!ScanPolicy.LocalRoot(key)||policy.Excluded(key))return false;
            lock(sync){
                if(installs.Count>=10000&&!installs.Contains(key))return false;
                return installs.Add(key)||record.Store=="Steam";
            }
        }
        IEnumerable<InstallRecord> Unique(IEnumerable<InstallRecord> records){
            foreach(var record in records){
                policy.Check();bool admitted;
                try{admitted=Admit(record);}catch(Exception e){policy.Warn(record.Store+": "+e.Message);continue;}
                if(!admitted)continue;
                yield return record;
            }
        }
        void Resolve(InstallRecord record){
            policy.Check();
            var result=ExecutableResolver.Resolve(record.Root,record.Preferred,policy);
            if(!record.Authoritative&&result.Candidates.Length==0)return;
            var identity=Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(Path.GetFullPath(record.Root).ToUpperInvariant())));
            Add(new(identity,record.Store,record.Id,record.Title,Path.GetFullPath(record.Root),Directory.Exists(record.Root)&&!ScanPolicy.Linked(record.Root)?"Available":"Unavailable",result.Reason,result.Executable,result.Candidates,record.SteamRoot,record.Icon));
        }
        var steamTask=folderOnly?Task.FromResult(new DiscoveryResult(1,"DiscoveryResult",[],[])):steamFixture??ProvidersSteam(fixtureRoot,cancel,policy);
        var task=Task.Run(()=> {
            try {
                if(!folderOnly)InstallResolutionQueue.Run(Unique(LauncherProviders.Installs(policy,fixtureRoot)),record=>{
                    try{Resolve(record);}catch(OperationCanceledException){throw;}catch(ScanLimitException){throw;}catch(Exception e){policy.Warn(record.Store+": "+e.Message);}
                },cancel);
                foreach(var root in customRoots??[])InstallResolutionQueue.Run(Unique(LauncherProviders.FolderGames(root,"Custom",policy)),Resolve,cancel);
            }catch(OperationCanceledException){policy.Warn("Scan cancelled or deadline reached; partial games retained");}catch(Exception e){policy.Warn(e.Message);}
        },CancellationToken.None);
        try{await Task.WhenAll(task,steamTask).WaitAsync(cancel);}catch(OperationCanceledException){policy.Warn("Scan cancelled or deadline reached; partial games retained");}
        if(!steamTask.IsCompleted)try{await steamTask.WaitAsync(TimeSpan.FromMilliseconds(250));}catch{}
        if(!folderOnly&&steamTask.IsCompletedSuccessfully){
            var steam=steamTask.Result;foreach(var error in steam.Errors)policy.Warn(error);
            var steamRoot=fixtureRoot==null?LauncherProviders.SteamRoot():Path.Combine(fixtureRoot,"Steam");
            void ResolveSteam(GameCandidate game){
                if(policy.Excluded(game.InstallRoot))return;
                if(cancel.IsCancellationRequested){Add(game with {SteamRoot=steamRoot,Reason="Scan deadline reached; scan again to identify the executable"});return;}
                try{var record=new InstallRecord("Steam",game.StoreId,game.Title,game.InstallRoot,"",steamRoot,"");if(Admit(record))Resolve(record);}catch(OperationCanceledException){Add(game with {SteamRoot=steamRoot});}catch(Exception e){policy.Warn("Steam: "+e.Message);Add(game with {SteamRoot=steamRoot});}
            }
            // Continue visiting already discovered manifests after a deadline so their identities survive.
            InstallResolutionQueue.Run(steam.Games.Where(g=>g.StoreId!="228980"),ResolveSteam,CancellationToken.None);
        }
        lock(sync)return new(2,"DiscoveryResult",games.Values.OrderBy(g=>g.Title,StringComparer.OrdinalIgnoreCase).ToArray(),policy.Errors,policy.Directories);
    }
    static Task<DiscoveryResult> ProvidersSteam(string? fixture,CancellationToken cancel,ScanPolicy policy)=>SteamProvider.Discover(fixture==null?null:[Path.Combine(fixture,"Steam")],cancel,policy);
}
