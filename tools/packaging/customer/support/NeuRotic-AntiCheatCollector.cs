// Read-only bounded collector. Directory handles deny delete sharing for the whole
// walk: enumeration names cannot redirect through a renamed/replaced ancestor.
using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Web.Script.Serialization;
using Microsoft.Win32.SafeHandles;
public static class NeuRoticAntiCheatCollector {
 [StructLayout(LayoutKind.Sequential)] struct Info {public uint Attributes;public System.Runtime.InteropServices.ComTypes.FILETIME Created,Accessed,Written;public uint Volume,SizeHigh,SizeLow,Links,IndexHigh,IndexLow;}
 [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct FindData {public uint Attributes;public System.Runtime.InteropServices.ComTypes.FILETIME Created,Accessed,Written;public uint SizeHigh,SizeLow,Reserved0,Reserved1;[MarshalAs(UnmanagedType.ByValTStr,SizeConst=260)]public string Name;[MarshalAs(UnmanagedType.ByValTStr,SizeConst=14)]public string Alternate;}
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern SafeFileHandle CreateFile(string name,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetFileInformationByHandle(SafeFileHandle h,out Info info);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern uint GetFinalPathNameByHandle(SafeFileHandle h,StringBuilder path,uint capacity,uint flags);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetFileInformationByHandleEx(SafeFileHandle h,int kind,out uint info,uint bytes);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr FindFirstFileEx(string name,int kind,out FindData data,int search,IntPtr filter,uint flags);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool FindNextFile(IntPtr h,out FindData data);
 [DllImport("kernel32.dll")] static extern bool FindClose(IntPtr h);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern uint GetDriveType(string root);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool GetVolumeInformation(string root,StringBuilder label,uint labelSize,out uint serial,out uint maxComponent,out uint flags,StringBuilder fileSystem,uint size);
 const uint Reparse=0x400,Directory=0x10,OpenReparse=0x200000,Backup=0x2000000;
 [DllImport("kernel32.dll")] static extern IntPtr GetStdHandle(int value);
 [DllImport("kernel32.dll")] static extern uint GetFileType(IntPtr handle);
 public static string ReadApproval(){var raw=GetStdHandle(-10);if(GetFileType(raw)!=3)return null;using(var h=new SafeFileHandle(raw,false))using(var stream=new FileStream(h,FileAccess.Read)){var b=new byte[1025];int count=0;while(count<b.Length){int n=stream.Read(b,count,b.Length-count);if(n==0)break;count+=n;}if(count>1024)return null;return Encoding.ASCII.GetString(b,0,count);}}
 public static Action<string,string> TestBoundary; // fault injection, never supplied by protocol/profile
 static void Boundary(string phase,string path){var hook=TestBoundary;if(hook!=null){try{hook(phase,path);}catch(Exception e){while(e.InnerException!=null)e=e.InnerException;throw e;}}}
 static long Size(Info i){return ((long)i.SizeHigh<<32)|i.SizeLow;}
 static string Identity(Info i){return i.Volume.ToString("x8")+":"+i.IndexHigh.ToString("x8")+i.IndexLow.ToString("x8")+":"+Size(i)+":"+i.Written.dwHighDateTime.ToString("x8")+i.Written.dwLowDateTime.ToString("x8");}
 static string FileId(Info i){return i.Volume.ToString("x8")+":"+i.IndexHigh.ToString("x8")+i.IndexLow.ToString("x8");}
 static string Final(SafeFileHandle h){var b=new StringBuilder(32768);uint n=GetFinalPathNameByHandle(h,b,(uint)b.Capacity,0);if(n==0||n>=b.Capacity)throw new IOException("final_path_unavailable");string p=b.ToString();if(p.StartsWith(@"\\?\UNC\",StringComparison.OrdinalIgnoreCase))throw new IOException("nonlocal_root");return p.StartsWith(@"\\?\",StringComparison.Ordinal)?p.Substring(4):p;}
 static string Extended(string p){return @"\\?\"+p;}
 static bool Same(string a,string b){return String.Equals(a.TrimEnd('\\'),b.TrimEnd('\\'),StringComparison.OrdinalIgnoreCase);}
 static bool Within(string root,string path){return path.StartsWith(root.TrimEnd('\\')+"\\",StringComparison.OrdinalIgnoreCase);}
 static void ValidPath(string p){if(String.IsNullOrEmpty(p)||p.StartsWith(@"\\")||p.Length>32700||p.Length<3||p[1]!=':')throw new IOException("unsupported_path");foreach(string s in p.Substring(3).Split('\\')){if(s.Length==0||s=="."||s==".."||s.EndsWith(".")||s.EndsWith(" ")||s.IndexOfAny(new[]{':','*','?','"','<','>','|'})>=0)throw new IOException("ambiguous_path");}}
 static SafeFileHandle Open(string p,bool directory,out Info info){
  Boundary("before_open",p);var h=CreateFile(Extended(p),directory?0x81u:0x80000000u,directory?3u:1u,IntPtr.Zero,3,OpenReparse|(directory?Backup:0),IntPtr.Zero);
  if(h.IsInvalid){h.Dispose();throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());}
  try{if(!GetFileInformationByHandle(h,out info))throw new IOException("identity_unavailable");if((info.Attributes&Reparse)!=0)throw new IOException("descendant_reparse");if(((info.Attributes&Directory)!=0)!=directory)throw new IOException("type_changed");if(!Same(Final(h),p))throw new IOException("scope_changed");if(directory){uint flags;if(GetFileInformationByHandleEx(h,23,out flags,4)&&(flags&1)!=0)throw new IOException("case_sensitive_directory");}Boundary("after_open",p);return h;}catch{h.Dispose();throw;}
 }
 public static string ReadBounded(string path,int maximum){
  string p=Path.GetFullPath(path);ValidPath(p);var locks=new List<SafeFileHandle>();try{LockAncestors(Path.GetDirectoryName(p),locks);Info before;using(var h=Open(p,false,out before)){if(Size(before)>maximum)throw new IOException("byte_limit");byte[] bytes=ReadStable(h,(int)Size(before));Info after;if(!GetFileInformationByHandle(h,out after)||Identity(after)!=Identity(before))throw new IOException("changed");return Convert.ToBase64String(bytes);}}finally{foreach(var h in locks)h.Dispose();}
 }
 static void LockAncestors(string dir,List<SafeFileHandle> locks){var stack=new Stack<string>();string current=dir;while(current!=null){stack.Push(current);var parent=Path.GetDirectoryName(current.TrimEnd('\\'));current=parent;}while(stack.Count!=0){Info ignored;locks.Add(Open(stack.Pop(),true,out ignored));}}
 static byte[] Read(SafeFileHandle h,int count){byte[] b=new byte[count];using(var s=new FileStream(h,FileAccess.Read,8192,false)){int pos=0;while(pos<count){int n=s.Read(b,pos,count-pos);if(n==0)throw new IOException("truncated");pos+=n;}}return b;}
 // FileStream must not own the original handle used for the post-read identity check.
 [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool DuplicateHandle(IntPtr process,SafeFileHandle source,IntPtr target,out SafeFileHandle duplicate,uint access,bool inherit,uint options);
 static byte[] ReadStable(SafeFileHandle h,int count){SafeFileHandle duplicate;if(!DuplicateHandle(GetCurrentProcess(),h,GetCurrentProcess(),out duplicate,0,false,2))throw new IOException("duplicate_failed");using(duplicate)return Read(duplicate,count);}
 static string Hash(byte[] b){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(b)).Replace("-","").ToLowerInvariant();}
 static bool Pe(byte[] b,long size){if(b.Length<64||b[0]!='M'||b[1]!='Z')return false;uint at=BitConverter.ToUInt32(b,60);if(at>1048576-24||at+24>b.Length||at+24>size)return false;int p=(int)at;if(b[p]!='P'||b[p+1]!='E'||b[p+2]!=0||b[p+3]!=0)return false;uint opt=BitConverter.ToUInt16(b,p+20),sections=BitConverter.ToUInt16(b,p+6);long extent=(long)p+24+opt+(long)sections*40;if(sections==0||sections>96||opt<2||extent>b.Length||extent>size||extent>1048576)return false;ushort magic=BitConverter.ToUInt16(b,p+24);return (magic==0x10b&&opt>=96)||(magic==0x20b&&opt>=112);}
 static Dictionary<string,object> Observation(string relative){return new Dictionary<string,object>{{"path",relative.Replace('\\','/')},{"pe",false},{"read_status","unavailable"},{"size",null},{"identity",null},{"digest",null},{"signature_status","not_checked"}};}
 static bool Candidate(string path,HashSet<string> names){string name=Path.GetFileName(path);if(!names.Contains(name))return false;string parent=Path.GetFileName(Path.GetDirectoryName(path));if(String.Equals(name,"Settings.json",StringComparison.OrdinalIgnoreCase))return String.Equals(parent,"EasyAntiCheat",StringComparison.OrdinalIgnoreCase)||String.Equals(parent,"EasyAntiCheat_EOS",StringComparison.OrdinalIgnoreCase);if(String.Equals(name,"Install_BattlEye.bat",StringComparison.OrdinalIgnoreCase))return String.Equals(parent,"BattlEye",StringComparison.OrdinalIgnoreCase);return true;}
 class Pending {public string Path;public int Depth;public Pending(string p,int d){Path=p;Depth=d;}}
 public static string Collect(string root,string executable,string[] candidates){
  var files=new List<object>();var issues=new SortedSet<string>(StringComparer.Ordinal);var locks=new List<SafeFileHandle>();var timer=Stopwatch.StartNew();int entries=0,dirs=0;long readBytes=0;string target="unavailable",selected=Path.GetFileName(executable);string status="complete";
  try{
   root=Path.GetFullPath(root).TrimEnd('\\');executable=Path.GetFullPath(executable);ValidPath(root);ValidPath(executable);if(!Within(root,executable))throw new IOException("executable_outside_root");selected=executable.Substring(root.Length+1).Replace('\\','/');string drive=Path.GetPathRoot(root);if(GetDriveType(drive)!=3)throw new IOException("nonlocal_root");uint serial,max,flags;var fs=new StringBuilder(64);if(!GetVolumeInformation(drive,null,0,out serial,out max,out flags,fs,64)||(fs.ToString()!="NTFS"&&fs.ToString()!="ReFS"))throw new IOException("unsupported_filesystem");
   LockAncestors(root,locks);var rootHandle=locks[locks.Count-1];Info ri;if(!GetFileInformationByHandle(rootHandle,out ri))throw new IOException("root_identity_unavailable");target=FileId(ri);var queue=new Queue<Pending>();queue.Enqueue(new Pending(root,0));var ids=new HashSet<string>(StringComparer.Ordinal);var names=new HashSet<string>(candidates,StringComparer.OrdinalIgnoreCase);
   var observed=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
   Action<string> inspect=delegate(string path){      if(!observed.Add(path))return;if(files.Count>=256){issues.Add("candidate_limit");return;}string relative=path.Substring(root.Length+1);var observation=Observation(relative);files.Add(observation);try{ValidPath(path);Info before;using(var file=Open(path,false,out before)){if(!Within(root,Final(file))||before.Volume!=ri.Volume)throw new IOException("candidate_scope_changed");long size=Size(before);observation["size"]=size;observation["identity"]=Identity(before);bool json=String.Equals(Path.GetFileName(path),"Settings.json",StringComparison.OrdinalIgnoreCase);int maximum=json?65536:String.Equals(Path.GetExtension(path),".exe",StringComparison.OrdinalIgnoreCase)?1048576:65536;if(json&&size>maximum)throw new IOException("config_byte_limit");Boundary("before_read",path);byte[] bytes=ReadStable(file,(int)Math.Min(size,maximum));readBytes+=bytes.Length;Boundary("after_read",path);Info after;if(!GetFileInformationByHandle(file,out after)||Identity(before)!=Identity(after)){observation["read_status"]="changed";issues.Add("candidate_read_changed");}else{observation["digest"]=Hash(bytes);observation["read_status"]="ok";observation["pe"]=Pe(bytes,size);if(json){string text=new UTF8Encoding(false,true).GetString(bytes);if(text.Length>0&&text[0]=='\uFEFF')text=text.Substring(1);observation["text"]=text;}}}}catch(Exception e){if(e is OperationCanceledException)throw;if(e is System.ComponentModel.Win32Exception&&((System.ComponentModel.Win32Exception)e).NativeErrorCode==5)observation["read_status"]="denied";issues.Add(e is IOException?e.Message:"candidate_read_unavailable");}
   };
   // Exact near-root probes precede the general tree walk. Hold every intervening
   // ancestor handle before a probe; a multi-component lexical open is insufficient.
   var priorityDirs=new HashSet<string>(StringComparer.OrdinalIgnoreCase);priorityDirs.Add(root);
   string exeParent=Path.GetDirectoryName(executable);while(Within(root,exeParent)){priorityDirs.Add(exeParent);exeParent=Path.GetDirectoryName(exeParent);}
   foreach(string near in new List<string>(priorityDirs))foreach(string folder in new[]{"EasyAntiCheat","EasyAntiCheat_EOS","BattlEye"})priorityDirs.Add(near+"\\"+folder);
   foreach(string priority in priorityDirs){if(timer.ElapsedMilliseconds>5000){issues.Add("deadline");break;}if(!System.IO.Directory.Exists(Extended(priority)))continue;
    var components=priority.Substring(root.Length).TrimStart('\\').Split(new[]{'\\'},StringSplitOptions.RemoveEmptyEntries);string current=root;bool valid=true;
    if(components.Length>12){issues.Add("depth_limit");continue;}
    foreach(string part in components){current+="\\"+part;try{Info pi;var ph=Open(current,true,out pi);if(pi.Volume!=ri.Volume||!Within(root,Final(ph))){ph.Dispose();throw new IOException("priority_scope_changed");}locks.Add(ph);}catch(Exception e){issues.Add(e is IOException?e.Message:"priority_directory_unavailable");valid=false;break;}}
    if(!valid)continue;
    foreach(string name in candidates){string candidate=priority+"\\"+name;if(!Candidate(candidate,names)||!File.Exists(Extended(candidate)))continue;inspect(candidate);}
   }
   while(queue.Count>0){if(timer.ElapsedMilliseconds>5000){issues.Add("deadline");break;}var dir=queue.Dequeue();SafeFileHandle handle;if(dir.Depth==0)handle=rootHandle;else{try{Info di;handle=Open(dir.Path,true,out di);if(!Within(root,Final(handle))||di.Volume!=ri.Volume){handle.Dispose();throw new IOException("directory_scope_changed");}locks.Add(handle);}catch(Exception e){issues.Add(e is IOException?e.Message:"directory_unavailable");continue;}}
    Info meta;if(!GetFileInformationByHandle(handle,out meta)){issues.Add("directory_identity_unavailable");continue;}if(!ids.Add(FileId(meta)))continue;if(++dirs>4096){issues.Add("directory_limit");break;}
    Boundary("before_enumerate",dir.Path);FindData data;IntPtr find=FindFirstFileEx(Extended(dir.Path)+"\\*",1,out data,0,IntPtr.Zero,2);if(find==new IntPtr(-1)){if(Marshal.GetLastWin32Error()!=2)issues.Add("enumeration_unavailable");continue;}var childNames=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
    try{bool next=true;while(next){if(data.Name!="."&&data.Name!=".."){if(++entries>50000||timer.ElapsedMilliseconds>5000){issues.Add(entries>50000?"entry_limit":"deadline");queue.Clear();break;}string path=dir.Path+"\\"+data.Name;Boundary("enumerated",path);if(!childNames.Add(data.Name)){issues.Add("case_ambiguity");next=FindNextFile(find,out data);continue;}if((data.Attributes&Reparse)!=0){issues.Add("descendant_reparse");}else if((data.Attributes&Directory)!=0){if(dir.Depth>=12)issues.Add("depth_limit");else if(queue.Count+dirs>=4096)issues.Add("directory_limit");else queue.Enqueue(new Pending(path,dir.Depth+1));}else if(Candidate(path,names)){
      inspect(path);if(files.Count>=256){issues.Add("candidate_limit");queue.Clear();break;}
     }}next=FindNextFile(find,out data);}if(Marshal.GetLastWin32Error()!=18&&Marshal.GetLastWin32Error()!=0)issues.Add("enumeration_interrupted");}finally{FindClose(find);}
   }
  }catch(OperationCanceledException){status="cancelled";issues.Add("cancelled");}catch(Exception e){issues.Add(e is IOException?e.Message:"root_unavailable");}finally{foreach(var h in locks)h.Dispose();}
  if(issues.Count>0&&status!="cancelled")status="incomplete";var result=new Dictionary<string,object>{{"schema_version",1},{"target_id",target},{"selected_exe",selected},{"scan_status",status},{"issues",new List<string>(issues)},{"files",files},{"system_status","not_requested"},{"system",new object[0]},{"catalog",new object[0]}};
  var envelope=new Dictionary<string,object>{{"snapshot",result},{"counters",new Dictionary<string,object>{{"entries",entries},{"directories",dirs},{"candidates",files.Count},{"inspected_bytes",readBytes},{"duration_ms",timer.ElapsedMilliseconds}}}};return new JavaScriptSerializer{MaxJsonLength=4194304,RecursionLimit=32}.Serialize(envelope);
 }
}
