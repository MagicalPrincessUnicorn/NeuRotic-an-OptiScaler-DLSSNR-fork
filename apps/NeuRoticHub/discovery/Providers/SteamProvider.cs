using Microsoft.Win32;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
namespace NeuRotic.Discovery;
internal static class SteamProvider
{
    sealed class BoundedErrors
    {
        readonly System.Collections.Concurrent.ConcurrentBag<string> entries = new();
        int count;
        public void Add(string value) { if (Interlocked.Increment(ref count) <= 64) entries.Add(value.Length > 512 ? value[..512] : value); }
        public string[] ToArray() => entries.ToArray();
    }
    static string Text(string path) { using var file = File.OpenRead(path); if (file.Length > 1048576) throw new IOException("Manifest too large"); var bytes = new byte[checked((int)file.Length)]; file.ReadExactly(bytes); return new UTF8Encoding(false, true).GetString(bytes).TrimStart('\uFEFF'); }
    static bool Linked(string path) { for (var d = new DirectoryInfo(Path.GetFullPath(path)); d != null; d = d.Parent) if (d.Exists && d.Attributes.HasFlag(FileAttributes.ReparsePoint)) return true; return false; }
    public static async Task<DiscoveryResult> Discover(string[]? supplied, CancellationToken cancel, ScanPolicy? policy=null)
    {
        var roots = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var errors = new BoundedErrors();
        if (supplied is { Length: > 32 }) throw new ArgumentException("Too many roots");
        if (supplied is { Length: > 0 }) foreach (var root in supplied) roots.Add(Path.GetFullPath(root));
        else
        {
            try { using var key = Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam"); if (key?.GetValue("SteamPath") is string root) roots.Add(Path.GetFullPath(root)); }
            catch (Exception e) { errors.Add("Steam registry: " + e.Message); }
            var standard=LauncherProviders.SteamRoot();if(Directory.Exists(standard))roots.Add(standard);
        }
        cancel.ThrowIfCancellationRequested();
        if(policy!=null)roots.RemoveWhere(root=>!ScanPolicy.LocalRoot(root)||policy.Excluded(root));
        foreach (var root in roots.ToArray())
        {
            cancel.ThrowIfCancellationRequested();
            try
            {
                var file = Path.Combine(root, "steamapps", "libraryfolders.vdf");
                if (!File.Exists(file)) continue;
                if (Linked(root)||ScanPolicy.Linked(file)||policy?.Excluded(Path.GetDirectoryName(file)!)==true) { errors.Add(root + ": linked or excluded metadata root refused"); continue; }
                var folders = await Task.Run(() => SteamManifestReader.Section(SteamManifestReader.Read(Text(file)), "libraryfolders"), cancel).WaitAsync(TimeSpan.FromSeconds(5), cancel);
                foreach (var entry in folders.Values)
                    if (entry is Dictionary<string, object> obj && obj.TryGetValue("path", out var path) && path is string value && roots.Count < 32 && (policy==null||(ScanPolicy.LocalRoot(value)&&!policy.Excluded(value)))) roots.Add(Path.GetFullPath(value));
            }
            catch (OperationCanceledException) when (cancel.IsCancellationRequested) { throw; }
            catch (Exception e) { errors.Add(root + ": " + e.Message); }
        }
        var games = new System.Collections.Concurrent.ConcurrentBag<GameCandidate>();
        int manifestCount = 0;
        long resultBytes = 0;
        var json = new JsonSerializerOptions { PropertyNamingPolicy = JsonNamingPolicy.CamelCase };
        // The lease belongs to the actual worker, including blocked file I/O.
        // A timeout must not release it while that worker is still running.
        var semaphore = new SemaphoreSlim(4);
        var tasks = roots.Select(async root =>
        {
            await semaphore.WaitAsync(cancel);
            try
            {
                await Task.Run(() =>
                {
                    if (Linked(root)) throw new IOException("Linked root refused");
                    var apps = Path.Combine(root, "steamapps");
                    if(policy!=null){
                        if(!policy.AccessibleDirectory(apps)||!policy.ReportDirectory(root,"Steam"))return;
                    }
                    int count = 0;
                    foreach (var manifest in Directory.EnumerateFiles(apps, "appmanifest_*.acf", SearchOption.TopDirectoryOnly))
                    {
                        cancel.ThrowIfCancellationRequested();
                        if (++count > 10000 || Interlocked.Increment(ref manifestCount) > 10000) throw new IOException("Manifest enumeration limit reached");
                        try
                        {
                            if (File.GetAttributes(manifest).HasFlag(FileAttributes.ReparsePoint)) throw new IOException("Linked manifest refused");
                            var doc = SteamManifestReader.Section(SteamManifestReader.Read(Text(manifest)), "AppState");
                            var id = SteamManifestReader.Value(doc, "appid");
                            var title = SteamManifestReader.Value(doc, "name");
                            var relative = SteamManifestReader.Value(doc, "installdir");
                            if (title.Length > 32768 || !id.All(char.IsAsciiDigit) || Path.IsPathRooted(relative) || relative.Split('/', '\\').Any(p => p is ".." or "." or "")) throw new FormatException("Invalid installation path or identity");
                            var common = Path.GetFullPath(Path.Combine(apps, "common"));
                            var target = Path.GetFullPath(Path.Combine(common, relative));
                            if (!target.StartsWith(common + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) throw new FormatException("Path escapes common");
                            if(policy?.Excluded(target)==true)continue;
                            var available = Directory.Exists(target) && !Linked(target);
                            var identity = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(target.ToUpperInvariant() + "|" + id)));
                            var candidate = new GameCandidate(identity, "Steam", id, title, target, available ? "Available" : "Unavailable", available ? "Select the actual game executable" : "Installation missing or linked");
                            long cost = Encoding.UTF8.GetByteCount(JsonSerializer.Serialize(candidate, json)) + 1;
                            if (Interlocked.Add(ref resultBytes, cost) > 786432) errors.Add("Discovery result budget reached; partial accessible games retained");
                            else games.Add(candidate);
                        }
                        catch (Exception e) { errors.Add(Path.GetFileName(manifest) + ": " + e.Message); }
                    }
                }, cancel);
            }
            catch (OperationCanceledException) when (cancel.IsCancellationRequested) { throw; }
            catch (Exception e) { errors.Add(root + ": " + e.Message); }
            finally { semaphore.Release(); }
        }).ToArray();
        try { await Task.WhenAll(tasks).WaitAsync(cancel); }
        catch (OperationCanceledException) when (cancel.IsCancellationRequested) { errors.Add("Scan deadline reached; accessible results retained"); }
        // A stalled OS read can outlive the deadline, so do not dispose its
        // semaphore. The short-lived helper exits after returning partial data.
        if (tasks.All(t => t.IsCompleted)) semaphore.Dispose();
        return new(1, "DiscoveryResult", games.OrderBy(g => g.Title).ThenBy(g => g.InstallRoot).ToArray(), errors.ToArray());
    }
}
