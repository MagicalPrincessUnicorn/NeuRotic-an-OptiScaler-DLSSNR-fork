namespace NeuRotic.Discovery;
internal sealed record ExecutableSelection(string Executable,string[] Candidates,string Reason);
internal static class ExecutableResolver
{
    static bool Helper(string path)
    {
        var name=Path.GetFileNameWithoutExtension(path).ToLowerInvariant();
        return name.StartsWith("unins",StringComparison.Ordinal)||name.EndsWith("launcher",StringComparison.Ordinal)||new[]{"setup","install","installer","crashreporter","crashreportclient","reporter","updater","update","launcher","launch","helper","cef","anticheat","eac","easyanticheat","battleye","benchmark","server","redistributable"}.Any(n=>name==n||name.StartsWith(n+"-",StringComparison.Ordinal)||name.StartsWith(n+"_",StringComparison.Ordinal));
    }
    internal static bool GameEntry(string path,ScanPolicy policy)=>!Helper(path)&&Bitness(path,policy)!=0;
    public static bool X64(string path,ScanPolicy policy)=>Bitness(path,policy)==64;
    public static int Bitness(string path,ScanPolicy policy)
    {
        if(!path.EndsWith(".exe",StringComparison.OrdinalIgnoreCase)||!File.Exists(path)||ScanPolicy.Linked(path))return 0;
        try { return policy.Read(()=> {
            using var file=File.OpenRead(path);using var reader=new BinaryReader(file);
            if(file.Length<256||reader.ReadUInt16()!=0x5a4d)return 0;
            file.Position=0x3c;var offset=reader.ReadInt32();if(offset<64||offset>1048576||offset>file.Length-26)return 0;
            file.Position=offset;if(reader.ReadUInt32()!=0x00004550)return 0;var machine=reader.ReadUInt16();var sections=reader.ReadUInt16();
            if(machine!=0x14c&&machine!=0x8664)return 0;
            file.Position=offset+20;var size=reader.ReadUInt16();var flags=reader.ReadUInt16();var magic=reader.ReadUInt16();var x86=machine==0x14c;
            return sections>0&&size>=(x86?224:240)&&file.Length>=offset+24L+size+sections*40L&&(flags&0x2000)==0&&(flags&2)!=0&&magic==(x86?0x10b:0x20b)?(x86?32:64):0;
        }); } catch(OperationCanceledException){throw;}catch{return 0;}
    }
    public static ExecutableSelection Resolve(string root,string? preferred,ScanPolicy policy)
    {
        policy.Check();root=Path.TrimEndingDirectorySeparator(Path.GetFullPath(root));
        if(policy.Excluded(root)||!Directory.Exists(root)||ScanPolicy.Linked(root))return new("",[],"Installation missing, linked or excluded");
        var inventory=policy.Inventory(root,()=>Scan(root,policy));
        if(inventory.Reason=="Detected the game's Win64 runtime")return inventory;
        if(!string.IsNullOrWhiteSpace(preferred)){
            var target=Path.GetFullPath(Path.IsPathRooted(preferred)?preferred:Path.Combine(root,preferred));
            // The exhaustive inventory has already validated every candidate's PE and ancestry.
            if(ScanPolicy.Within(target,root)&&inventory.Candidates.Contains(target,StringComparer.OrdinalIgnoreCase))return new(target,inventory.Candidates,"Executable identified by launcher metadata");
        }
        return inventory;
    }
    static ExecutableSelection Scan(string root,ScanPolicy policy)
    {
        var paths=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var queue=new Queue<(string,int)>();queue.Enqueue((root,0));
        while(queue.TryDequeue(out var next)){
            policy.DirectoryVisited();
            if(policy.Excluded(next.Item1)||ScanPolicy.Linked(next.Item1))continue;
            try {
                foreach(var file in Directory.EnumerateFiles(next.Item1,"*.exe",SearchOption.TopDirectoryOnly)){
                    policy.CandidateVisited();if(paths.Count>=64){policy.Warn(root+": executable choice limit reached");break;}
                    if(!Helper(file)&&Bitness(file,policy)!=0)paths.Add(Path.GetFullPath(file));
                }
                if(next.Item2<8)foreach(var dir in Directory.EnumerateDirectories(next.Item1))if(!policy.Excluded(dir)&&!ScanPolicy.Skipped(Path.GetFileName(dir))&&!ScanPolicy.Linked(dir)){if(queue.Count>=20000){policy.Warn("Directory queue limit reached");break;}queue.Enqueue((dir,next.Item2+1));}
            }catch(OperationCanceledException){throw;}catch(ScanLimitException){throw;}catch(Exception e){policy.Warn(next.Item1+": "+e.Message);}
        }
        var all=paths.OrderBy(p=>p,StringComparer.OrdinalIgnoreCase).ToArray();
        // A conventional Unreal bootstrap plus one Win64 shipping/runtime is a known layout.
        var nested=all.Where(p=>p.Replace('/','\\').Contains("\\Binaries\\Win64\\",StringComparison.OrdinalIgnoreCase)).ToArray();
        if(nested.Length==1&&all.Length<=2&&all.All(p=>p==nested[0]||(Path.GetDirectoryName(p)==Path.GetFullPath(root)&&Path.GetFileNameWithoutExtension(p).Equals(Path.GetFileName(root),StringComparison.OrdinalIgnoreCase))))return new(nested[0],all,"Detected the game's Win64 runtime");
        return all.Length==1?new(all[0],all,"One game executable found"):new("",all,all.Length==0?"No game executable found":"Choose from the detected game executables");
    }
}
