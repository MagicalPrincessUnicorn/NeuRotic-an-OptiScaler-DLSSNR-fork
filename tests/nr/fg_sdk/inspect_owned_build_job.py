"""Inspect an exact SDK supervisor's job; optionally stop residual build helpers.

No process-name search/kill: duplicate the exact supervisor's job handle, query
membership, and refuse cleanup if any member is not an explicitly named helper.
The original supervisor independently observes job emptiness and parent exit.
"""
import ctypes as c
from ctypes import wintypes as w
import sys,json,os
from pathlib import Path
pid=int(sys.argv[1]); stop='--stop-residual-build-helper' in sys.argv
k=c.WinDLL('kernel32',use_last_error=True);nt=c.WinDLL('ntdll')
def sig(name,result,args):
 f=getattr(k,name);f.restype=result;f.argtypes=args;return f
openproc=sig('OpenProcess',w.HANDLE,[w.DWORD,w.BOOL,w.DWORD])
close=sig('CloseHandle',w.BOOL,[w.HANDLE])
dup=sig('DuplicateHandle',w.BOOL,[w.HANDLE,w.HANDLE,w.HANDLE,c.POINTER(w.HANDLE),w.DWORD,w.BOOL,w.DWORD])
current=sig('GetCurrentProcess',w.HANDLE,[])
query=sig('QueryInformationJobObject',w.BOOL,[w.HANDLE,c.c_int,w.LPVOID,w.DWORD,w.LPVOID])
image=sig('QueryFullProcessImageNameW',w.BOOL,[w.HANDLE,w.DWORD,w.LPWSTR,c.POINTER(w.DWORD)])
terminate=sig('TerminateJobObject',w.BOOL,[w.HANDLE,w.UINT])
class Entry(c.Structure):
 _fields_=[('object',c.c_size_t),('pid',c.c_size_t),('handle',c.c_size_t),('access',w.DWORD),('trace',w.USHORT),('type',w.USHORT),('flags',w.DWORD),('reserved',w.DWORD)]
def path_for(identity):
 p=openproc(0x1000,False,identity)
 if not p:raise OSError(c.get_last_error(),'OpenProcess query')
 try:
  buf=c.create_unicode_buffer(32768);length=w.DWORD(len(buf))
  if not image(p,0,buf,c.byref(length)):raise OSError(c.get_last_error(),'QueryFullProcessImageName')
  return buf.value
 finally:close(p)
source=openproc(0x40,False,pid)
if not source:raise OSError(c.get_last_error(),'Open exact supervisor')
report={'supervisor_pid':pid,'supervisor_image':path_for(pid),'stop_requested':stop,'jobs':[]}
assert Path(report['supervisor_image']).name.lower()=='python.exe'
nt.NtQuerySystemInformation.argtypes=[w.ULONG,w.LPVOID,w.ULONG,c.POINTER(w.ULONG)]
nt.NtQuerySystemInformation.restype=c.c_long
size=1024*1024
try:
 while True:
  buf=c.create_string_buffer(size);needed=w.ULONG()
  result=nt.NtQuerySystemInformation(64,buf,size,c.byref(needed))
  if result==0:break
  if result!=-1073741820:raise OSError(result,'NtQuerySystemInformation')
  size=max(size*2,needed.value+65536)
 count=c.c_size_t.from_buffer(buf).value
 for index in range(count):
  e=Entry.from_buffer(buf,16+index*c.sizeof(Entry))
  if e.pid!=pid:continue
  copied=w.HANDLE()
  if not dup(source,e.handle,current(),c.byref(copied),0,False,2):continue
  try:
   members=c.create_string_buffer(8+8*1024)
   if not query(copied,3,members,len(members),None):continue
   assigned=w.DWORD.from_buffer(members).value;listed=w.DWORD.from_buffer(members,4).value
   if assigned!=listed or listed>1024:raise RuntimeError('Incomplete job membership')
   ids=[c.c_size_t.from_buffer(members,8+i*8).value for i in range(listed)]
   names=[path_for(i) for i in ids]
   job={'handle':e.handle,'members':[{'pid':i,'image':name} for i,name in zip(ids,names)],'terminated':False}
   report['jobs'].append(job)
   if stop and ids:
    allowed=['C:/Windows/System32/conhost.exe','C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/vctip.exe']
    if not all(os.path.normcase(name) in {os.path.normcase(p.replace('/',os.sep)) for p in allowed} for name in names):
     raise RuntimeError('Refuse: exact job contains a non-allowlisted build helper')
    if not terminate(copied,0):raise OSError(c.get_last_error(),'Terminate exact residual helper job')
    job['terminated']=True
  finally:close(copied)
finally:close(source)
print(json.dumps(report,indent=2))
