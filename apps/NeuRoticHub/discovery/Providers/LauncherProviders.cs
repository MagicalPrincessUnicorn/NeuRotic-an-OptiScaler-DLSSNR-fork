using Microsoft.Win32;
using System.Text.Json;
using System.Xml;
using System.Xml.Linq;
namespace NeuRotic.Discovery;
internal sealed record InstallRecord(string Store,string Id,string Title,string Root,string Preferred="",string SteamRoot="",string Icon="",bool Authoritative=true);
internal static class LauncherProviders
{
    static void ReportInstallLibrary(string install,string store,ScanPolicy policy)
    {
        // Launcher records identify an install, never a search root. Its containing
        // library may be non-default (Epic/GOG/registry installs on another drive).
        if(!policy.AccessibleDirectory(install))return;
        var library=Path.GetDirectoryName(Path.TrimEndingDirectorySeparator(Path.GetFullPath(install)));
        if(store=="Xbox"&&Path.GetFileName(install).Equals("Content",StringComparison.OrdinalIgnoreCase))library=Path.GetDirectoryName(library);
        if(library!=null)policy.ReportDirectory(library,store);
    }
    static IEnumerable<InstallRecord> Accessible(IEnumerable<InstallRecord> source,ScanPolicy policy,string name){
        using var iterator=source.GetEnumerator();
        while(true){bool next;try{next=iterator.MoveNext();}catch(OperationCanceledException){throw;}catch(ScanLimitException){throw;}catch(Exception e){policy.Warn(name+": "+e.Message);yield break;}if(!next)yield break;yield return iterator.Current;}
    }
    static string Field(JsonElement item,string field)=>item.TryGetProperty(field,out var v)&&v.ValueKind==JsonValueKind.String?v.GetString()??"":"";
    static string Value(RegistryKey key,params string[] names){foreach(var name in names)if(key.GetValue(name) is string value && value.Length<=32768)return value;return "";}
    static IEnumerable<InstallRecord> RegistryGames(string keyPath,string store,ScanPolicy policy)
    {
        var records=new List<InstallRecord>();
        foreach(var view in new[]{RegistryView.Registry64,RegistryView.Registry32})try {
            using var machine=RegistryKey.OpenBaseKey(RegistryHive.LocalMachine,view);using var key=machine.OpenSubKey(keyPath);if(key==null)continue;
            foreach(var name in key.GetSubKeyNames().Take(1000)){
                policy.Check();using var game=key.OpenSubKey(name);if(game==null)continue;
                var root=Value(game,"path","InstallDir","Install Dir","InstallLocation","InstallDirectory");
                if(!ScanPolicy.LocalRoot(root)||policy.Excluded(root))continue;
                ReportInstallLibrary(root,store,policy);
                var title=Value(game,"gameName","DisplayName","name");
                records.Add(new(store,name,string.IsNullOrWhiteSpace(title)?Path.GetFileName(root.TrimEnd('\\','/')):title,root,Value(game,"exe","Executable")));
            }
        }catch(Exception e){policy.Warn(store+" registry: "+e.Message);}
        return records;
    }
    public static string SteamRoot()
    {
        try{using var user=Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam");if(user?.GetValue("SteamPath") is string root&&Directory.Exists(root))return Path.GetFullPath(root);}catch{}
        foreach(var view in new[]{RegistryView.Registry64,RegistryView.Registry32})try{using var machine=RegistryKey.OpenBaseKey(RegistryHive.LocalMachine,view);using var key=machine.OpenSubKey(@"Software\Valve\Steam");if(key?.GetValue("InstallPath") is string root&&Directory.Exists(root))return Path.GetFullPath(root);}catch{}
        return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),"Steam");
    }
    public static IEnumerable<InstallRecord> FolderGames(string root,string store,ScanPolicy policy)
    {
        policy.Check();root=Path.TrimEndingDirectorySeparator(Path.GetFullPath(root));if(!policy.Root(root))yield break;
        if(root==Path.GetPathRoot(root)||!Directory.Exists(root)||ScanPolicy.Linked(root)){if(Directory.Exists(root))policy.Warn(store+": search root is linked or too broad");yield break;}
        if(!policy.ReportDirectory(root,store,store!="Custom"))yield break;
        var pending=new Queue<(string Path,int Depth)>();pending.Enqueue((root,0));
        while(pending.TryDequeue(out var node)){
            policy.DirectoryVisited();if(!policy.AccessibleDirectory(node.Path))continue;
            // Local provider metadata is parsed inside this root; libraryfolders
            // references are deliberately not followed during an added-root scan.
            var apps=Path.Combine(node.Path,"steamapps");
            if(policy.AccessibleDirectory(apps)){
                foreach(var record in Accessible(LocalSteam(node.Path,policy),policy,"Steam"))yield return record;
                continue;
            }
            string[] gog;
            try{gog=Directory.EnumerateFiles(node.Path,"goggame-*.info").Take(8).ToArray();}catch(Exception e){policy.Warn(node.Path+": "+e.Message);continue;}
            if(gog.Length>0){foreach(var record in Accessible(GOGGame(node.Path,gog,policy),policy,"GOG"))yield return record;continue;}
            if(File.Exists(Path.Combine(node.Path,"Content","MicrosoftGame.config"))){foreach(var record in Accessible(XboxGame(node.Path,policy),policy,"Xbox"))yield return record;continue;}
            bool gameRoot;
            try{gameRoot=Directory.EnumerateFiles(node.Path,"*.exe").Take(10000).Any(file=>ExecutableResolver.GameEntry(file,policy))||Directory.Exists(Path.Combine(node.Path,"Binaries"));}catch(OperationCanceledException){throw;}catch(Exception e){policy.Warn(node.Path+": "+e.Message);continue;}
            if(gameRoot){yield return new(store,"",Path.GetFileName(node.Path),node.Path,Authoritative:false);continue;}
            if(node.Depth>=8){policy.Warn(node.Path+": library depth limit reached");continue;}
            string[] children;
            try{children=Directory.EnumerateDirectories(node.Path).Take(1001).ToArray();}catch(Exception e){policy.Warn(node.Path+": "+e.Message);continue;}
            if(children.Length>1000)policy.Warn(node.Path+": folder entry limit reached");
            foreach(var dir in children.Take(1000))if(ScanPolicy.Within(dir,root)&&!ScanPolicy.Skipped(Path.GetFileName(dir))){if(pending.Count>=20000){policy.Warn("Library directory queue limit reached");break;}pending.Enqueue((dir,node.Depth+1));}
        }
    }
    static IEnumerable<InstallRecord> LocalSteam(string root,ScanPolicy policy)
    {
        var apps=Path.Combine(root,"steamapps");var common=Path.Combine(apps,"common");
        foreach(var file in Directory.EnumerateFiles(apps,"appmanifest_*.acf").Take(10000)){
            InstallRecord? record=null;
            try{var doc=SteamManifestReader.Section(SteamManifestReader.Read(policy.Text(file)),"AppState");var id=SteamManifestReader.Value(doc,"appid");var title=SteamManifestReader.Value(doc,"name");var relative=SteamManifestReader.Value(doc,"installdir");
                if(id.Length==0||!id.All(char.IsAsciiDigit)||id=="228980"||Path.IsPathRooted(relative)||relative.Split('/','\\').Any(p=>p is ".." or "." or ""))continue;
                var target=Path.GetFullPath(Path.Combine(common,relative));if(ScanPolicy.Within(target,common)&&!policy.Excluded(target)&&!ScanPolicy.Linked(target))record=new("Steam",id,title,target,SteamRoot:root);
            }catch(OperationCanceledException){throw;}catch(Exception e){policy.Warn("Steam manifest: "+e.Message);}
            if(record!=null)yield return record;
        }
    }
    static IEnumerable<InstallRecord> Epic(string folder,ScanPolicy policy)
    {
        if(!policy.AccessibleDirectory(folder))yield break;
        foreach(var file in Directory.EnumerateFiles(folder,"*.item").Take(1000)){
            InstallRecord? record=null;
            try{using var json=JsonDocument.Parse(policy.Text(file),new(){MaxDepth=16});var item=json.RootElement;
                if(item.TryGetProperty("bIsIncompleteInstall",out var incomplete)&&incomplete.ValueKind==JsonValueKind.True)continue;
                var root=Field(item,"InstallLocation");var title=Field(item,"DisplayName");var exe=Field(item,"LaunchExecutable");
                if(Path.IsPathFullyQualified(root)&&root!=Path.GetPathRoot(root)&&!string.IsNullOrWhiteSpace(title))record=new("Epic",Field(item,"AppName"),title,root,exe);
            }catch(OperationCanceledException){throw;}catch(Exception e){policy.Warn("Epic manifest: "+e.Message);}
            if(record!=null){ReportInstallLibrary(record.Root,"Epic",policy);yield return record;}
        }
    }
    static IEnumerable<InstallRecord> EpicInstalled(string file,ScanPolicy policy){
        var records=new List<InstallRecord>();if(!File.Exists(file)||!policy.AccessibleDirectory(Path.GetDirectoryName(file)!))return records;
        try{using var json=JsonDocument.Parse(policy.Text(file),new(){MaxDepth=16});if(json.RootElement.TryGetProperty("InstallationList",out var list)&&list.ValueKind==JsonValueKind.Array)foreach(var item in list.EnumerateArray().Take(1000)){var root=Field(item,"InstallLocation");if(Path.IsPathFullyQualified(root)&&root!=Path.GetPathRoot(root))records.Add(new("Epic",Field(item,"AppName"),Path.GetFileName(root.TrimEnd('\\','/')),root));}}
        catch(OperationCanceledException){throw;}catch(Exception e){policy.Warn("Epic installed list: "+e.Message);}foreach(var record in records)ReportInstallLibrary(record.Root,"Epic",policy);return records;
    }
    static IEnumerable<InstallRecord> GOGMetadata(string root,ScanPolicy policy)
    {
        if(!policy.ReportDirectory(root,"GOG"))yield break;
        foreach(var dir in Directory.EnumerateDirectories(root).Take(1000).Where(d=>!policy.Excluded(d)&&!ScanPolicy.Linked(d)))foreach(var record in GOGGame(dir,Directory.EnumerateFiles(dir,"goggame-*.info").Take(8),policy))yield return record;
    }
    static IEnumerable<InstallRecord> GOGGame(string dir,IEnumerable<string> files,ScanPolicy policy)
    {
        foreach(var file in files){
            InstallRecord? record=null;
            try{using var json=JsonDocument.Parse(policy.Text(file),new(){MaxDepth=16});var item=json.RootElement;var exe="";
                if(item.TryGetProperty("playTasks",out var tasks)&&tasks.ValueKind==JsonValueKind.Array)foreach(var task in tasks.EnumerateArray())if(task.TryGetProperty("isPrimary",out var primary)&&primary.ValueKind==JsonValueKind.True){exe=Field(task,"path");break;}
                var title=Field(item,"name");if(title.Length>0)record=new("GOG",Field(item,"gameId"),title,dir,exe);
            }catch(OperationCanceledException){throw;}catch(Exception e){policy.Warn("GOG metadata: "+e.Message);}
            if(record!=null)yield return record;
        }
    }
    static IEnumerable<InstallRecord> Xbox(string root,ScanPolicy policy)
    {
        if(!policy.ReportDirectory(root,"Xbox"))yield break;
        foreach(var dir in Directory.EnumerateDirectories(root).Take(1000).Where(policy.AccessibleDirectory))foreach(var record in XboxGame(dir,policy))yield return record;
    }
    static IEnumerable<InstallRecord> XboxGame(string dir,ScanPolicy policy)
    {
        {
            var content=Path.Combine(dir,"Content");var file=Path.Combine(content,"MicrosoftGame.config");if(policy.Excluded(content)||!File.Exists(file))yield break;InstallRecord? record=null;
            try{using var reader=XmlReader.Create(new StringReader(policy.Text(file)),new(){DtdProcessing=DtdProcessing.Prohibit,XmlResolver=null,MaxCharactersInDocument=1048576});var doc=XDocument.Load(reader);
                var identity=doc.Descendants().FirstOrDefault(e=>e.Name.LocalName=="Identity");var title=doc.Descendants().FirstOrDefault(e=>e.Name.LocalName=="ShellVisuals")?.Attribute("DefaultDisplayName")?.Value??Path.GetFileName(dir);
                var exe=doc.Descendants().FirstOrDefault(e=>e.Name.LocalName=="Executable"&&e.Attribute("TargetDeviceFamily")?.Value!="XboxOne")?.Attribute("Name")?.Value??"";
                record=new("Xbox",identity?.Attribute("Name")?.Value??"",title,content,exe);
            }catch(OperationCanceledException){throw;}catch(Exception e){policy.Warn("Xbox metadata: "+e.Message);}
            if(record!=null)yield return record;
        }
    }
    static IEnumerable<string> ConfigRoots(string store,ScanPolicy policy)
    {
        var local=Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);var roaming=Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
        var files=new List<string>();
        if(store=="Epic")files.Add(Path.Combine(local,"EpicGamesLauncher","Saved","Config","WindowsEditor","GameUserSettings.ini"));
        if(store=="EA"){
            var config=Path.Combine(local,"Electronic Arts","EA Desktop");if(policy.AccessibleDirectory(config))files.AddRange(Directory.EnumerateFiles(config,"user_*.ini").Take(32));
            files.Add(Path.Combine(roaming,"Origin","local.xml"));
        }
        foreach(var file in files){if(!policy.AccessibleDirectory(Path.GetDirectoryName(file)!)||!File.Exists(file))continue;string text;try{text=policy.Text(file);}catch(Exception e){policy.Warn(store+" config: "+e.Message);continue;}
            if(file.EndsWith(".xml",StringComparison.OrdinalIgnoreCase)){
                string? root=null;try{using var reader=XmlReader.Create(new StringReader(text),new(){DtdProcessing=DtdProcessing.Prohibit,XmlResolver=null});var doc=XDocument.Load(reader);root=doc.Descendants().FirstOrDefault(e=>e.Attribute("key")?.Value=="DownloadInPlaceDir")?.Attribute("value")?.Value;}catch(Exception e){policy.Warn(e.Message);}
                if(root!=null&&Path.IsPathFullyQualified(root))yield return root;
            }else foreach(var line in text.Split('\n')){var pair=line.Split('=',2);if(pair.Length==2&&(pair[0].Trim().Equals("DefaultAppInstallLocation",StringComparison.OrdinalIgnoreCase)||pair[0].Trim().Equals("user.downloadinplacedir",StringComparison.OrdinalIgnoreCase))){var root=pair[1].Trim().Trim('"');if(Path.IsPathFullyQualified(root))yield return root;}}
        }
    }
    public static IEnumerable<InstallRecord> Installs(ScanPolicy policy,string? fixture)
    {
        var program=Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles);var program86=Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86);
        var data=Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData);
        var epic=fixture==null?Path.Combine(data,"Epic","EpicGamesLauncher","Data","Manifests"):Path.Combine(fixture,"Epic","Manifests");
        foreach(var record in Accessible(Epic(epic,policy),policy,"Epic"))yield return record;
        foreach(var record in EpicInstalled(fixture==null?Path.Combine(data,"Epic","UnrealEngineLauncher","LauncherInstalled.dat"):Path.Combine(fixture,"Epic","LauncherInstalled.dat"),policy))yield return record;
        var gogRoots=fixture==null?new[]{Path.Combine(program86,"GOG Galaxy","Games"),Path.Combine(Path.GetPathRoot(program)??"C:\\","GOG Games")}:new[]{Path.Combine(fixture,"GOG")};
        foreach(var root in gogRoots)foreach(var record in Accessible(GOGMetadata(root,policy),policy,"GOG"))yield return record;
        var xboxRoots=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        if(fixture!=null)xboxRoots.Add(Path.Combine(fixture,"XboxGames"));
        else foreach(var drive in DriveInfo.GetDrives().Where(d=>d.DriveType==DriveType.Fixed)){
            xboxRoots.Add(Path.Combine(drive.Name,"XboxGames"));var gaming=Path.Combine(drive.Name,".GamingRoot");
            if(File.Exists(gaming)&&!policy.Excluded(gaming))try{var bytes=policy.Bytes(gaming,4096);if(bytes.Length>=8&&System.Text.Encoding.ASCII.GetString(bytes,0,4)=="RGBX"){var relative=System.Text.Encoding.Unicode.GetString(bytes,8,bytes.Length-8).TrimEnd('\0');var target=Path.GetFullPath(Path.Combine(drive.Name,relative));if(ScanPolicy.Within(target,drive.Name))xboxRoots.Add(target);}}catch(Exception e){policy.Warn("Xbox install root: "+e.Message);}
        }
        foreach(var root in xboxRoots)foreach(var record in Accessible(Xbox(root,policy),policy,"Xbox"))yield return record;
        if(fixture==null){
            foreach(var record in RegistryGames(@"SOFTWARE\GOG.com\Games","GOG",policy))yield return record;
            foreach(var key in new[]{@"SOFTWARE\EA Games",@"SOFTWARE\Electronic Arts\EA Games",@"SOFTWARE\Electronic Arts\EA Core\Installed Games"})foreach(var record in RegistryGames(key,"EA",policy))yield return record;
            foreach(var record in RegistryGames(@"SOFTWARE\Ubisoft\Launcher\Installs","Ubisoft",policy))yield return record;
        }
        var defaults=fixture==null?new[]{("Epic",Path.Combine(program,"Epic Games")),("EA",Path.Combine(program,"EA Games")),("EA",Path.Combine(program86,"Origin Games")),("Ubisoft",Path.Combine(program86,"Ubisoft","Ubisoft Game Launcher","games"))}:new[]{("EA",Path.Combine(fixture,"EA Games")),("Ubisoft",Path.Combine(fixture,"Ubisoft Games"))};
        foreach(var (store,root) in defaults)foreach(var record in Accessible(FolderGames(root,store,policy),policy,store))yield return record;
        if(fixture==null)foreach(var store in new[]{"Epic","EA"})foreach(var root in ConfigRoots(store,policy))foreach(var record in FolderGames(root,store,policy))yield return record;
    }
}
