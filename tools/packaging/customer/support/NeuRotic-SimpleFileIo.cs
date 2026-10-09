// Bounded file I/O: parent/leaf handle identity, no hashes, preimages or rollback.
using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class NeuRoticSimpleFileIo {
 [StructLayout(LayoutKind.Sequential)] struct Info {
  public uint Attributes; public System.Runtime.InteropServices.ComTypes.FILETIME Creation,Access,Write;
  public uint Volume,SizeHigh,SizeLow,Links,IndexHigh,IndexLow;
 }
 [StructLayout(LayoutKind.Sequential)] struct Basic { public long Creation,Access,Write,Change; public uint Attributes; }
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern SafeFileHandle CreateFileW(string p,uint a,uint share,IntPtr security,uint create,uint flags,IntPtr template);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetFileInformationByHandle(SafeFileHandle h,out Info i);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern uint GetFinalPathNameByHandleW(SafeFileHandle h,StringBuilder p,uint len,uint flags);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool SetFileInformationByHandle(SafeFileHandle h,int kind,ref byte value,uint size);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool SetFileInformationByHandle(SafeFileHandle h,int kind,ref Basic value,uint size);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool SetFileInformationByHandle(SafeFileHandle h,int kind,IntPtr value,uint size);
 static IOException Error(string p){return new IOException("File operation failed: "+p,new Win32Exception(Marshal.GetLastWin32Error()));}
 static string Final(SafeFileHandle h){var b=new StringBuilder(32768);uint n=GetFinalPathNameByHandleW(h,b,32768,1);if(n==0||n>=32768)throw Error("canonical path");return b.ToString().TrimEnd('\\');}
 static Info Check(SafeFileHandle h,string p,bool directory){Info i;if(h.IsInvalid||!GetFileInformationByHandle(h,out i))throw Error(p);if((i.Attributes&0x400)!=0||((i.Attributes&0x10)!=0)!=directory||(!directory&&i.Links!=1))throw new IOException("Linked or unexpected destination: "+p);return i;}
 sealed class Lease:IDisposable {public List<SafeFileHandle> Handles=new List<SafeFileHandle>();public string Parent;public void Dispose(){for(int i=Handles.Count-1;i>=0;i--)Handles[i].Dispose();}}
 static Lease Parents(string path,bool create){var lease=new Lease();try{
  string full=Path.GetFullPath(path),directory=Path.GetDirectoryName(full);var stack=new Stack<string>();
  while(!String.IsNullOrEmpty(directory)){stack.Push(directory);var next=Path.GetDirectoryName(directory);if(next==directory)break;directory=next;}
  string previous=null;
  while(stack.Count>0){string item=stack.Pop();if(!Directory.Exists(item)){if(!create)throw new IOException("Parent is missing: "+item);Directory.CreateDirectory(item);}
   var h=CreateFileW(item,0x80000000,3,IntPtr.Zero,3,0x02200000,IntPtr.Zero);try{Check(h,item,true);string actual=Final(h);if(previous!=null&&!actual.Equals(previous+"\\"+Path.GetFileName(item),StringComparison.OrdinalIgnoreCase))throw new IOException("Parent changed: "+item);lease.Parent=previous=actual;lease.Handles.Add(h);h=null;}finally{if(h!=null)h.Dispose();}
  }return lease;
 }catch{lease.Dispose();throw;}}
 static SafeFileHandle Open(string path,uint access,uint mode,Lease parents,uint share){var h=CreateFileW(Path.GetFullPath(path),access,share,IntPtr.Zero,mode,0x00200000,IntPtr.Zero);try{Check(h,path,false);if(!Final(h).Equals(parents.Parent+"\\"+Path.GetFileName(path),StringComparison.OrdinalIgnoreCase))throw new IOException("File changed: "+path);return h;}catch{h.Dispose();throw;}}
 static string Identity(Info i){return i.Volume.ToString("X8")+i.IndexHigh.ToString("X8")+i.IndexLow.ToString("X8")+":"+i.SizeHigh+":"+i.SizeLow+":"+i.Write.dwHighDateTime+":"+i.Write.dwLowDateTime;}
 public static string Stat(string path){if(!File.Exists(path)){if(Directory.Exists(path))throw new IOException("Directory occupies file: "+path);return "Absent";}using(var p=Parents(path,false))using(var h=Open(path,0,3,p,7))return Identity(Check(h,path,false));}
 public static string DirectoryIdentity(string directory){using(var p=Parents(Path.Combine(directory,"identity-placeholder"),false)){var i=Check(p.Handles[p.Handles.Count-1],directory,true);return i.Volume.ToString("X8")+i.IndexHigh.ToString("X8")+i.IndexLow.ToString("X8");}}
 // Explicit INI editing retains its revision guard. Payload copy/removal never
 // calls this bounded configuration-only content check.
 public static bool WriteSettings(byte[] bytes,string destination,string revision){
  if(bytes.Length>6291456)throw new IOException("Configuration is too large");
  using(var p=Parents(destination,false))using(var h=Open(destination,0xC0010100,3,p,0))using(var stream=new FileStream(h,FileAccess.ReadWrite)){
   if(stream.Length>6291456)throw new IOException("Configuration is too large");
   using(var sha=System.Security.Cryptography.SHA256.Create()){string actual=BitConverter.ToString(sha.ComputeHash(stream)).Replace("-","");if(!actual.Equals(revision,StringComparison.OrdinalIgnoreCase))return false;}
   ClearReadOnly(h,destination);stream.Position=0;stream.Write(bytes,0,bytes.Length);stream.SetLength(bytes.Length);stream.Flush(true);return true;
  }
 }
 static void ClearReadOnly(SafeFileHandle h,string path){if((Check(h,path,false).Attributes&1)!=0){var b=new Basic{Attributes=0x80};if(!SetFileInformationByHandle(h,0,ref b,(uint)Marshal.SizeOf(typeof(Basic))))throw Error(path);}}
 public static void Copy(string source,string destination,string expected,string sourceExpected){
  bool destinationOwned=false;try{
  using(var s=Parents(source,false))using(var sh=Open(source,0x80000000,3,s,1))using(var input=new FileStream(sh,FileAccess.Read))
  using(var p=Parents(destination,true)){
   if(!String.IsNullOrEmpty(sourceExpected)&&Identity(Check(sh,source,false))!=sourceExpected)throw new IOException("Source changed; refresh before installing: "+source);
   // CREATE_NEW prevents an appearing foreign file being truncated under Absent consent.
   uint mode=expected=="Absent"?1u:3u;
   using(var h=Open(destination,0xC0010100,mode,p,0)){
    var actual=Check(h,destination,false);if(!String.IsNullOrEmpty(expected)&&expected!="Absent"&&Identity(actual)!=expected)throw new IOException("Foreign file changed; choose again: "+destination);
    destinationOwned=true;ClearReadOnly(h,destination);using(var output=new FileStream(h,FileAccess.ReadWrite)){output.Position=0;input.CopyTo(output);output.SetLength(output.Position);output.Flush(true);}
   }
  }}catch(Exception e){e.Data["NeuRoticDestinationOwned"]=destinationOwned;throw;}
 }
 public static void Write(byte[] bytes,string destination){using(var p=Parents(destination,true))using(var h=Open(destination,0xC0010100,4,p,0)){ClearReadOnly(h,destination);using(var output=new FileStream(h,FileAccess.ReadWrite)){output.Write(bytes,0,bytes.Length);output.SetLength(bytes.Length);output.Flush(true);}}}
 public static void Remove(string path){if(!File.Exists(path)){if(Directory.Exists(path))throw new IOException("Directory occupies owned file: "+path);return;}using(var p=Parents(path,false))using(var h=Open(path,0x00010100,3,p,0)){ClearReadOnly(h,path);byte remove=1;if(!SetFileInformationByHandle(h,4,ref remove,1))throw Error(path);}}
 // The small retry tool is a dependency group. Open every member for deletion
 // before removing any, so a sharing/access failure cannot dismantle the tool.
 public static void RemoveTogether(string[] paths){
  var parents=new List<Lease>();var files=new List<SafeFileHandle>();var names=new List<string>();string current="";
  try{
   foreach(string path in paths){current=path;if(!File.Exists(path)){if(Directory.Exists(path))throw new IOException("Directory occupies owned file: "+path);continue;}
    var p=Parents(path,false);parents.Add(p);files.Add(Open(path,0x00010100,3,p,0));names.Add(path);
   }
   for(int i=0;i<files.Count;i++){current=names[i];ClearReadOnly(files[i],current);}
   for(int i=0;i<files.Count;i++){current=names[i];byte remove=1;if(!SetFileInformationByHandle(files[i],4,ref remove,1))throw Error(current);}
  }catch(Exception error){error.Data["NeuRoticPath"]=current;throw;}
  finally{for(int i=files.Count-1;i>=0;i--)files[i].Dispose();for(int i=parents.Count-1;i>=0;i--)parents[i].Dispose();}
 }
 public static void Rename(string source,string destination,string expected){
  using(var p=Parents(destination,false))using(var s=Parents(source,false))using(var h=Open(source,0x10000,3,s,0)){
   if(!String.IsNullOrEmpty(expected)&&Identity(Check(h,source,false))!=expected)throw new IOException("Rename source changed: "+source);
   RenameHeld(h,destination);
  }
 }
 public static void RenameDirectory(string source,string destination){
  using(var p=Parents(destination,false))using(var s=Parents(source,false))using(var h=CreateFileW(source,0x10000,0,IntPtr.Zero,3,0x02200000,IntPtr.Zero)){
   Check(h,source,true);if(!Final(h).Equals(s.Parent+"\\"+Path.GetFileName(source),StringComparison.OrdinalIgnoreCase))throw new IOException("Staging directory changed: "+source);
   RenameHeld(h,destination);
  }
 }
 static void RenameHeld(SafeFileHandle h,string destination){
   byte[] name=Encoding.Unicode.GetBytes(Path.GetFullPath(destination));int rootOffset=IntPtr.Size==8?8:4,lengthOffset=rootOffset+IntPtr.Size,nameOffset=lengthOffset+4;
   IntPtr info=Marshal.AllocHGlobal(nameOffset+name.Length+2);
   try{for(int i=0;i<nameOffset;i++)Marshal.WriteByte(info,i,0);Marshal.WriteInt32(info,lengthOffset,name.Length);Marshal.Copy(name,0,IntPtr.Add(info,nameOffset),name.Length);Marshal.WriteInt16(info,nameOffset+name.Length,0);
    // Flags=0: atomically refuse an occupied destination, including one that
    // appeared after checking. Rename the held source inode, never old bytes.
    if(!SetFileInformationByHandle(h,22,info,(uint)(nameOffset+name.Length+2)))throw Error(destination);
   }finally{Marshal.FreeHGlobal(info);}
 }
}
