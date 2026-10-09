using System.Text;
namespace NeuRotic.Discovery;
internal sealed class ScanLimitException(string message):IOException(message);

internal sealed class ScanPolicy(CancellationToken cancel, string[]? excludedRoots = null)
{
    readonly string[] excluded = (excludedRoots??[]).Select(p=>Path.TrimEndingDirectorySeparator(Path.GetFullPath(p))).ToArray();
    readonly Dictionary<string,DiscoveryDirectory> reported = new(StringComparer.OrdinalIgnoreCase);
    int directoryBytes;
    readonly SemaphoreSlim io = new(4);
    readonly HashSet<string> roots = new(StringComparer.OrdinalIgnoreCase);
    readonly List<string> errors = [];
    readonly System.Collections.Concurrent.ConcurrentDictionary<string,Lazy<ExecutableSelection>> inventories = new(StringComparer.OrdinalIgnoreCase);
    int directories, candidates;
    public CancellationToken Cancel => cancel;
    public string[] Errors { get { lock(errors) return [..errors]; } }
    public DiscoveryDirectory[] Directories { get { lock(reported) return reported.Values.OrderBy(d=>d.Path,StringComparer.OrdinalIgnoreCase).ToArray(); } }
    public static bool LocalRoot(string? path) => !string.IsNullOrWhiteSpace(path) && path.Length<=32768 && Path.IsPathFullyQualified(path)
        && !path.StartsWith("\\\\",StringComparison.Ordinal) && !path.StartsWith("//",StringComparison.Ordinal)
        && Path.TrimEndingDirectorySeparator(Path.GetFullPath(path))!=Path.GetPathRoot(Path.GetFullPath(path));
    public bool Excluded(string path) => excluded.Any(root=>Path.TrimEndingDirectorySeparator(Path.GetFullPath(path)).Equals(root,StringComparison.OrdinalIgnoreCase)||Within(path,root));
    public bool ReportDirectory(string path,string source,bool automatic=true)
    {
        if(!AccessibleDirectory(path))return false;
        path=Path.TrimEndingDirectorySeparator(Path.GetFullPath(path));
        var value=new DiscoveryDirectory(path,source,automatic);
        lock(reported){
            if(reported.TryGetValue(path,out var prior)){if(automatic&&!prior.Automatic)reported[path]=value;return true;}
            var cost=Encoding.UTF8.GetByteCount(System.Text.Json.JsonSerializer.Serialize(value))+1;
            if(reported.Count>=128 || directoryBytes+cost>65536){Warn("Directory metadata limit reached; partial directories retained");return true;}
            reported[path]=value;directoryBytes+=cost;return true;
        }
    }
    // Metadata access and the library-root catalogue are intentionally separate.
    public bool AccessibleDirectory(string path)=>LocalRoot(path)&&!Excluded(path)&&System.IO.Directory.Exists(path)&&!Linked(path);
    public void Warn(string value) { lock(errors) if(errors.Count < 64) errors.Add(value.Length > 512 ? value[..512] : value); }
    public void Check() => cancel.ThrowIfCancellationRequested();
    public ExecutableSelection Inventory(string root,Func<ExecutableSelection> scan)
    {
        Check();
        // Only this request owns the snapshot. A later scan gets a fresh policy.
        return inventories.GetOrAdd(root,_=>new(scan,LazyThreadSafetyMode.ExecutionAndPublication)).Value;
    }
    public bool Root(string path) { Check(); if(Excluded(path))return false;lock(roots) return roots.Count < 64 && roots.Add(Path.GetFullPath(path)); }
    public void DirectoryVisited() { Check(); if(Interlocked.Increment(ref directories)>20000) throw new ScanLimitException("Directory scan limit reached; partial results retained"); }
    public void CandidateVisited() { Check(); if(Interlocked.Increment(ref candidates)>10000) throw new ScanLimitException("Executable scan limit reached; partial results retained"); }
    public T Read<T>(Func<T> read)
    {
        // A timed-out OS operation keeps its slot until the actual operation ends.
        if(!io.WaitAsync(TimeSpan.FromSeconds(5),cancel).GetAwaiter().GetResult())throw new TimeoutException("Metadata I/O slots unavailable");
        return RunLeased(read);
    }
    T RunLeased<T>(Func<T> read)
    {
        var worker = Task.Run(()=> { try { return read(); } finally { io.Release(); } });
        return worker.WaitAsync(TimeSpan.FromSeconds(5),cancel).GetAwaiter().GetResult();
    }
    public string Text(string path) => Read(()=> {
        if(Linked(path))throw new IOException("Linked metadata refused");
        using var stream=File.OpenRead(path);
        if(stream.Length>1048576)throw new IOException("Metadata exceeds 1 MiB");
        var bytes=new byte[checked((int)stream.Length)];stream.ReadExactly(bytes);
        return new UTF8Encoding(false,true).GetString(bytes).TrimStart('\uFEFF');
    });
    public byte[] Bytes(string path,int limit) => Read(()=>{if(Linked(path))throw new IOException("Linked metadata refused");using var file=File.OpenRead(path);if(file.Length>limit)throw new IOException("Metadata too large");var bytes=new byte[checked((int)file.Length)];file.ReadExactly(bytes);return bytes;});
    public static bool Linked(string path)
    {
        for(var current=Path.GetFullPath(path);;){
            if((File.Exists(current)||System.IO.Directory.Exists(current)) && File.GetAttributes(current).HasFlag(FileAttributes.ReparsePoint))return true;
            var parent=Path.GetDirectoryName(current);if(string.IsNullOrEmpty(parent)||parent==current)break;current=parent;
        }
        return false;
    }
    public static bool Within(string path,string root) => Path.GetFullPath(path).StartsWith(Path.GetFullPath(root).TrimEnd('\\','/')+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase);
    public static bool Skipped(string name) => name.StartsWith('.') || name.StartsWith('$') || new[]{"_CommonRedist","__Installer","Installer","Redist","Support","Cache","node_modules","Windows"}.Contains(name,StringComparer.OrdinalIgnoreCase);
}
