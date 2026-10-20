using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using Microsoft.Win32.SafeHandles;
public static class NeuRoticAppUpdateFiles {
 [StructLayout(LayoutKind.Sequential)] struct Info {
  public uint Attributes; public System.Runtime.InteropServices.ComTypes.FILETIME Creation,Access,Write;
  public uint Volume,SizeHigh,SizeLow,Links,IndexHigh,IndexLow;
 }
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern SafeFileHandle CreateFileW(string p,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetFileInformationByHandle(SafeFileHandle h,out Info info);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern uint GetFinalPathNameByHandleW(SafeFileHandle h,StringBuilder p,uint length,uint flags);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool SetFileInformationByHandle(SafeFileHandle h,int kind,ref byte value,uint size);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool SetFileInformationByHandle(SafeFileHandle h,int kind,IntPtr value,uint size);
 static Exception Error(string p){return new IOException("Safe file operation refused: "+p,new Win32Exception(Marshal.GetLastWin32Error()));}
 static string Final(SafeFileHandle h){var b=new StringBuilder(32768);uint n=GetFinalPathNameByHandleW(h,b,32768,1);if(n==0||n>=32768)throw Error("canonical volume path");return b.ToString().TrimEnd('\\');}
 static Info Check(SafeFileHandle h,string path,bool directory){
  Info i;if(h.IsInvalid||!GetFileInformationByHandle(h,out i))throw Error(path);
  if((i.Attributes&0x400)!=0||((i.Attributes&0x10)!=0)!=directory||(!directory&&i.Links!=1))throw new IOException("Linked or unexpected target refused: "+path);
  return i;
 }
 public sealed class Lease:IDisposable {
  internal readonly List<SafeFileHandle> Handles=new List<SafeFileHandle>();
  internal string Parent;
  public void Dispose(){for(int i=Handles.Count-1;i>=0;i--)Handles[i].Dispose();Handles.Clear();}
 }
 public static Lease Parents(string path,bool create){
  var result=new Lease();try{
   string full=Path.GetFullPath(path);string directory=Path.GetDirectoryName(full);var pending=new Stack<string>();
   while(!String.IsNullOrEmpty(directory)){pending.Push(directory);string next=Path.GetDirectoryName(directory);if(next==directory)break;directory=next;}
   string previous=null;
   while(pending.Count>0){string item=pending.Pop();
    if(!Directory.Exists(item)){if(!create)throw new IOException("Parent missing: "+item);Directory.CreateDirectory(item);}
    var h=CreateFileW(item,0x80000000,3,IntPtr.Zero,3,0x02200000,IntPtr.Zero);
    try{Check(h,item,true);string actual=Final(h);
     if(previous!=null&&!actual.Equals(previous+"\\"+Path.GetFileName(item),StringComparison.OrdinalIgnoreCase))throw new IOException("Parent identity changed: "+item);
     previous=actual;result.Parent=actual;result.Handles.Add(h);h=null;
    }finally{if(h!=null)h.Dispose();}
   }
   return result;
  }catch{result.Dispose();throw;}
 }
 static SafeFileHandle Open(string path,uint access,uint creation,bool directory,Lease parents,uint share=1){
  var h=CreateFileW(Path.GetFullPath(path),access,share,IntPtr.Zero,creation,0x00200000|(directory?0x02000000u:0u),IntPtr.Zero);
  try{Check(h,path,directory);string expected=parents.Parent+"\\"+Path.GetFileName(Path.GetFullPath(path).TrimEnd('\\'));
   if(!Final(h).Equals(expected,StringComparison.OrdinalIgnoreCase))throw new IOException("Opened target identity differs: "+path);
   return h;
  }catch{h.Dispose();throw;}
 }
 static string Hash(Stream stream){using(var sha=SHA256.Create()){return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-","");}}
 public static string Identity(string directory){
  using(var parents=Parents(Path.Combine(directory,"identity-placeholder"),false)){
   var h=parents.Handles[parents.Handles.Count-1];Info i=Check(h,directory,true);
   return i.Volume.ToString("X8")+i.IndexHigh.ToString("X8")+i.IndexLow.ToString("X8");
  }
 }
 public static Lease PinTarget(string directory){return Parents(Path.Combine(directory,"target-placeholder"),false);}
 public static void Copy(string source,string destination,string expected,string current){
  try {
  using(var parents=Parents(source,false))using(var handle=Open(source,0x80000000,3,false,parents))using(var input=new FileStream(handle,FileAccess.Read)){
   // The bundled Character Inspector OpenCV module is about 71 MiB. Keep a
   // bounded allocation while permitting that verified offline dependency.
   if(input.Length>268435456)throw new IOException("Managed copy exceeds 256 MiB");
   if(!Hash(input).Equals(expected,StringComparison.OrdinalIgnoreCase))throw new IOException("Source changed: "+source);
   input.Position=0;byte[] bytes=new byte[(int)input.Length];int offset=0;while(offset<bytes.Length){int n=input.Read(bytes,offset,bytes.Length-offset);if(n==0)throw new EndOfStreamException();offset+=n;}
   if(!Write(bytes,destination,expected,current)){var e=new IOException("Destination changed at copy boundary: "+destination);e.Data["NeuRoticRefusedPath"]=destination;throw e;}
  }
  }catch(Exception e){if(!e.Data.Contains("NeuRoticWriteStarted")||!(bool)e.Data["NeuRoticWriteStarted"])e.Data["NeuRoticRefusedPath"]=destination;throw;}
 }
 static void PublicationFixtureBoundary(string destination,string phase){
  if(Environment.GetEnvironmentVariable("NEUROTIC_PUBLICATION_TEST_BOUNDARY")!=phase)return;
  string root=Environment.GetEnvironmentVariable("NEUROTIC_PUBLICATION_TEST_ROOT");
  if(String.IsNullOrEmpty(root)||!Path.GetDirectoryName(Path.GetFullPath(destination)).Equals(Path.GetFullPath(root).TrimEnd('\\'),StringComparison.OrdinalIgnoreCase))throw new IOException("Publication fault injection requires its exact fixture directory");
  System.Diagnostics.Process.GetCurrentProcess().Kill();
 }
 public static bool Write(byte[] bytes,string destination,string expected,string current){
  using(var input=new MemoryStream(bytes,false)){if(!Hash(input).Equals(expected,StringComparison.OrdinalIgnoreCase))throw new IOException("Staged content hash mismatch: "+destination);}
  using(var parents=Parents(destination,true)){
   SafeFileHandle existing=null;FileStream old=null;
   string stage=destination+".nr-stage-"+Guid.NewGuid().ToString("N");
   try{
    if(File.Exists(destination)){
     if(current=="Absent")return false;
     existing=Open(destination,0x80000000,3,false,parents,5);old=new FileStream(existing,FileAccess.Read);
     if(!String.IsNullOrEmpty(current)&&!Hash(old).Equals(current,StringComparison.OrdinalIgnoreCase))return false;
    }else if(!String.IsNullOrEmpty(current)&&current!="Absent")return false;
    using(var handle=Open(stage,0xC0010000,1,false,parents))using(var stream=new FileStream(handle,FileAccess.ReadWrite)){
     bool published=false;try{
      PublicationFixtureBoundary(destination,"stage-created");
      stream.Write(bytes,0,bytes.Length);stream.Flush(true);stream.Position=0;
      PublicationFixtureBoundary(destination,"stage-flushed");
      if(!Hash(stream).Equals(expected,StringComparison.OrdinalIgnoreCase))throw new IOException("Staged bytes failed verification: "+destination);
      // Keep the verified old inode open without write sharing. Windows atomic
      // replacement needs delete sharing; recheck the destination file identity
      // immediately before rename while all directory ancestors remain pinned.
      // Unsupported filesystems fail here, before touching the live destination.
      byte[] name=Encoding.Unicode.GetBytes(Path.GetFullPath(destination));int rootOffset=IntPtr.Size==8?8:4;int lengthOffset=rootOffset+IntPtr.Size;int nameOffset=lengthOffset+4;
      IntPtr info=Marshal.AllocHGlobal(nameOffset+name.Length+2);
      try{
       if(existing!=null)using(var observed=Open(destination,0x80000000,3,false,parents,5)){
        Info a=Check(existing,destination,false),b=Check(observed,destination,false);
        if(a.Volume!=b.Volume||a.IndexHigh!=b.IndexHigh||a.IndexLow!=b.IndexLow)return false;
       }
       for(int i=0;i<nameOffset;i++)Marshal.WriteByte(info,i,0);
       Marshal.WriteInt32(info,existing==null?0:3);Marshal.WriteInt32(info,lengthOffset,name.Length);Marshal.Copy(name,0,IntPtr.Add(info,nameOffset),name.Length);Marshal.WriteInt16(info,nameOffset+name.Length,0);
       PublicationFixtureBoundary(destination,"before-publish");
       if(!SetFileInformationByHandle(handle,22,info,(uint)(nameOffset+name.Length+2)))throw Error(destination);
       PublicationFixtureBoundary(destination,"after-publish");
       published=true;return true;
      }finally{Marshal.FreeHGlobal(info);}
     }finally{if(!published){byte remove=1;SetFileInformationByHandle(handle,4,ref remove,1);}}
    }
   }finally{if(old!=null)old.Dispose();else if(existing!=null)existing.Dispose();}
  }
 }
 public static void DeleteDirectory(string path,string expectedIdentity){
  using(var parents=Parents(path,false))using(var handle=Open(path,0x10000u,3,true,parents)){
   Info i=Check(handle,path,true);
   string identity=i.Volume.ToString("X8")+i.IndexHigh.ToString("X8")+i.IndexLow.ToString("X8");
   if(!identity.Equals(expectedIdentity,StringComparison.Ordinal))throw new IOException("Directory identity changed: "+path);
   // The same verified directory handle is removed nonrecursively. A file
   // replacement or a newly nonempty directory must never be deleted here.
   byte remove=1;if(!SetFileInformationByHandle(handle,4,ref remove,1))throw Error(path);
  }
 }
 public static void Delete(string path,string expected){
  bool directory=Directory.Exists(path);
  using(var parents=Parents(path,false))using(var handle=Open(path,directory?0x10000u:0x80010000u,3,directory,parents)){
   if(!directory&&!String.IsNullOrEmpty(expected))using(var stream=new FileStream(handle,FileAccess.Read)){
    if(!Hash(stream).Equals(expected,StringComparison.OrdinalIgnoreCase)){var e=new IOException("File changed at deletion boundary: "+path);e.Data["NeuRoticRefusedPath"]=path;throw e;}
    byte removeFile=1;if(!SetFileInformationByHandle(handle,4,ref removeFile,1))throw Error(path);
    return;
   }
   byte remove=1;if(!SetFileInformationByHandle(handle,4,ref remove,1))throw Error(path);
  }
 }
}
