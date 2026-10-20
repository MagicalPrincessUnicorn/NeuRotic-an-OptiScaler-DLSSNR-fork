"""Bounded verifier child ownership; failures cannot leave an extracted worker alive."""
import subprocess, ctypes, re

def product_version(path):
    api=ctypes.WinDLL('version',use_last_error=True)
    api.GetFileVersionInfoSizeW.argtypes=[ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_ulong)]
    api.GetFileVersionInfoW.argtypes=[ctypes.c_wchar_p,ctypes.c_ulong,ctypes.c_ulong,ctypes.c_void_p]
    api.VerQueryValueW.argtypes=[ctypes.c_void_p,ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_void_p),ctypes.POINTER(ctypes.c_uint)]
    ignored=ctypes.c_ulong();size=api.GetFileVersionInfoSizeW(str(path),ctypes.byref(ignored))
    if not size:raise ValueError('DLL version resource missing')
    data=ctypes.create_string_buffer(size)
    if not api.GetFileVersionInfoW(str(path),0,size,data):raise ValueError('DLL version read failed')
    pointer=ctypes.c_void_p();count=ctypes.c_uint()
    if not api.VerQueryValueW(data,'\\VarFileInfo\\Translation',ctypes.byref(pointer),ctypes.byref(count)) or count.value<4:
        raise ValueError('DLL version translation missing')
    words=ctypes.cast(pointer,ctypes.POINTER(ctypes.c_ushort))
    name=f'\\StringFileInfo\\{words[0]:04x}{words[1]:04x}\\ProductVersion'
    if not api.VerQueryValueW(data,name,ctypes.byref(pointer),ctypes.byref(count)):raise ValueError('ProductVersion missing')
    return ctypes.wstring_at(pointer,count.value-1)

def require_product_commit(path,expected):
    version=product_version(path)
    identities=re.findall(r'\(([0-9a-f]+)\)',version)
    if identities!=[expected]:raise ValueError('DLL embedded source differs: '+version)
    return version

def run_owned_child(command, data, cwd, timeout, logs):
    child=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                           cwd=cwd,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    try:
        output,errors=child.communicate(data,timeout=timeout)
        return child.pid,child.returncode,output,errors
    finally:
        if child.poll() is None:
            child.kill()
            output,errors=child.communicate(timeout=5)
        else:
            output,errors=child.communicate()
        logs.append('worker pid='+str(child.pid)+' exit='+str(child.returncode)+'\n'+errors.decode('utf-8',errors='replace'))
