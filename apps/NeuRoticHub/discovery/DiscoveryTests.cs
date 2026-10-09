using System.Security.Cryptography;
namespace NeuRotic.Discovery;
internal static class DiscoveryTests
{
 public static async Task<int> Run(string root)
 {
  root=Path.GetFullPath(root);if(Directory.Exists(root))throw new IOException("Fixture root must be absent");Directory.CreateDirectory(root);
  try {
   string[] roots=[Path.Combine(root,"Library Ω & one"),Path.Combine(root,"Library two")];
   foreach(var library in roots){var apps=Path.Combine(library,"steamapps");var game=Path.Combine(apps,"common","Game");Directory.CreateDirectory(game);File.WriteAllText(Path.Combine(apps,"appmanifest_123.acf"),"\"AppState\" { \"appid\" \"123\" \"name\" \"Example without DLSS\" \"installdir\" \"Game\" }");foreach(var name in new[]{"user.ini","backup.dlsss","private.dll"})File.WriteAllText(Path.Combine(game,name),"untouched");}
   var before=Directory.GetFiles(root,"*",SearchOption.AllDirectories).ToDictionary(p=>p,p=> (Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(p))),File.GetLastWriteTimeUtc(p)));
   var result=await SteamProvider.Discover([..roots,Path.Combine(root,"disconnected")],CancellationToken.None);
   if(result.Games.Length!=2||result.Games[0].Id==result.Games[1].Id||result.Errors.Length==0)throw new Exception("Multi-library identity/partial error failed");
   Console.WriteLine("PASS NH-02-STEAM-MULTI, NH-02-NO-DLSS");
   if(before.Any(f=>f.Value!=(Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(f.Key))),File.GetLastWriteTimeUtc(f.Key))))throw new Exception("Discovery changed fixtures");Console.WriteLine("PASS NH-02-READONLY: hashes and timestamps unchanged");
   try{SteamManifestReader.Read("\"AppState\" { \"appid\"");throw new Exception("Accepted truncation");}catch(FormatException){Console.WriteLine("PASS NH-02-BAD-MANIFEST");}
   var appsBudget=Path.Combine(roots[0],"steamapps");
   for(int i=0;i<30;i++)File.WriteAllText(Path.Combine(appsBudget,$"appmanifest_{1000+i}.acf"),$"\"AppState\" {{ \"appid\" \"{1000+i}\" \"name\" \"{new string('Ω',8000)}\" \"installdir\" \"Game\" }}");
   for(int i=0;i<80;i++)File.WriteAllText(Path.Combine(appsBudget,$"appmanifest_bad{i}.acf"),"malformed");
   var bounded=await SteamProvider.Discover(roots,CancellationToken.None);
   var encoded=System.Text.Json.JsonSerializer.Serialize(bounded,new System.Text.Json.JsonSerializerOptions{PropertyNamingPolicy=System.Text.Json.JsonNamingPolicy.CamelCase});
   if(System.Text.Encoding.UTF8.GetByteCount(encoded)>1048576||bounded.Errors.Length>64||bounded.Games.Length>=32)throw new Exception("IPC result or diagnostics exceeded bounded budget");
   Console.WriteLine("PASS NH-02-OUTPUT-BOUND: partial results and diagnostics stay within 1 MiB");
   using var cancelled=new CancellationTokenSource();cancelled.Cancel();try{await SteamProvider.Discover(roots,cancelled.Token);throw new Exception("Ignored cancellation");}catch(OperationCanceledException){Console.WriteLine("PASS NH-02-BOUNDING: cancellation");}
   await LibraryRootTests.Run(Path.Combine(root,"LibraryRoots"));
   await UnifiedDiscoveryTests.Run(Path.Combine(root,"Unified"));
   await FolderDiscoveryTests.Run(Path.Combine(root,"Folders"));
   await DiscoveryPerformanceTests.Run(Path.Combine(root,"Performance"));
   return 0;
  }finally{Directory.Delete(root,true);}
 }
}
