#include "ui/GameDiagnostics.h"
#include "ManualLibrary.h"
#include <fstream>
#include <iostream>
#include <winioctl.h>
#include <cstring>
bool Junction(const std::filesystem::path& path,const std::filesystem::path& target){
 CreateDirectoryW(path.c_str(),nullptr);auto handle=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);if(handle==INVALID_HANDLE_VALUE)return false;
 auto substitute=L"\\??\\"+target.wstring(),print=target.wstring();auto pathBytes=(substitute.size()+1+print.size()+1)*2;std::vector<unsigned char> bytes(16+pathBytes);auto put=[&](size_t at,USHORT value){memcpy(bytes.data()+at,&value,2);};DWORD tag=IO_REPARSE_TAG_MOUNT_POINT;memcpy(bytes.data(),&tag,4);put(4,(USHORT)(8+pathBytes));put(10,(USHORT)(substitute.size()*2));put(12,(USHORT)((substitute.size()+1)*2));put(14,(USHORT)(print.size()*2));memcpy(bytes.data()+16,substitute.c_str(),(substitute.size()+1)*2);memcpy(bytes.data()+16+(substitute.size()+1)*2,print.c_str(),(print.size()+1)*2);DWORD returned=0;bool ok=DeviceIoControl(handle,FSCTL_SET_REPARSE_POINT,bytes.data(),(DWORD)bytes.size(),nullptr,0,&returned,nullptr)!=FALSE;CloseHandle(handle);return ok;
}
int main(){
 auto root=std::filesystem::absolute(L"builds/universal-connections/x86-ui/prepared-diagnostics-fixture");
 if(std::filesystem::exists(root))return 2;auto helper=root/L"NeuRotic/Prepared/NeuRotic.GpuHost";std::filesystem::create_directories(helper);
 auto exe=root/L"portal2.exe";wchar_t windows[MAX_PATH];GetWindowsDirectoryW(windows,MAX_PATH);std::filesystem::copy_file(std::filesystem::path(windows)/L"SysWOW64/cmd.exe",exe);
 auto target=nh::Utf8(exe.wstring());auto inspection=nh::Json{{"target",{{"executable",target},{"bitness",32},{"installationRoute","prepared-x86"}}},{"state",{{"status","installed-verified"},{"selected_proxy","dxgi.dll"}}}};
 std::ofstream(root/L"OptiScaler.log")<<"STALE ROOT NATIVE LOG";std::ofstream(helper/L"OptiScaler.log")<<"ACTIVE PREPARED RENDERER LOG";
 int failed=0;auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';failed+=!ok;};
 auto read=nh::ReadGameDiagnostics(target,inspection);check(read.text.find("ACTIVE PREPARED RENDERER LOG")!=std::string::npos&&read.text.find("STALE ROOT NATIVE LOG")==std::string::npos,"PE32 quick preview reads prepared renderer log, excluding stale native root log");
 std::filesystem::remove(helper/L"OptiScaler.log");std::ofstream(helper/L"OptiScaler_123456.log")<<"NUMBERED PREPARED RENDERER LOG";
 read=nh::ReadGameDiagnostics(target,inspection);check(read.text.find("NUMBERED PREPARED RENDERER LOG")!=std::string::npos,"PE32 quick preview finds runtime numbered renderer log");
 std::filesystem::remove(helper/L"OptiScaler_123456.log");read=nh::ReadGameDiagnostics(target,inspection);check(read.text.find("prepared")!=std::string::npos&&read.text.find("STALE ROOT NATIVE LOG")==std::string::npos,"missing prepared log gives route-specific empty state");
 wchar_t system[MAX_PATH];GetSystemDirectoryW(system,MAX_PATH);std::filesystem::copy_file(std::filesystem::path(system)/L"cmd.exe",exe,std::filesystem::copy_options::overwrite_existing);
 read=nh::ReadGameDiagnostics(target,inspection);check(read.text.find("STALE ROOT NATIVE LOG")!=std::string::npos,"actual PE64 retains root log even with stale x86 receipt");
 std::filesystem::copy_file(std::filesystem::path(windows)/L"SysWOW64/cmd.exe",exe,std::filesystem::copy_options::overwrite_existing);
 std::filesystem::remove(helper);auto outside=root/L"outside";std::filesystem::create_directory(outside);std::ofstream(outside/L"OptiScaler.log")<<"OUTSIDE PRIVATE LOG";
 bool linked=Junction(helper,outside);check(linked,"prepared helper junction fixture created");if(linked){read=nh::ReadGameDiagnostics(target,inspection);check(read.status.find("Linked")!=std::string::npos&&read.text.find("OUTSIDE PRIVATE LOG")==std::string::npos,"prepared helper linked directory refuses without reading outside log");RemoveDirectoryW(helper.c_str());}
 for(auto& entry:std::filesystem::recursive_directory_iterator(root))if(GetFileAttributesW(entry.path().c_str())&FILE_ATTRIBUTE_REPARSE_POINT)return 2;
 std::filesystem::remove_all(root);return failed?1:0;
}
