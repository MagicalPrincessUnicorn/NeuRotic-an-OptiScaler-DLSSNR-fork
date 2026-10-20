// Controlled legal Windows metadata, applied only to this TU's real HubModel.
// This is API-contract coverage, not native ReFS/case-sensitive qualification.
#include "ui/HubViewModel.h"
#include <Windows.h>
#include <cstring>
#include <map>
#include <iostream>

namespace IdentityContract {
enum class Mode { Native, UpperId, UpperVolume, ZeroId, Unsupported, CaseName, NoIdentity };
Mode mode=Mode::Native;
std::wstring fixtureRoot;
std::map<HANDLE,std::wstring> opened;
HANDLE WINAPI Open(LPCWSTR path,DWORD access,DWORD sharing,LPSECURITY_ATTRIBUTES security,DWORD disposition,DWORD flags,HANDLE templateFile){
 auto handle=::CreateFileW(path,access,sharing,security,disposition,flags,templateFile);
 if(handle!=INVALID_HANDLE_VALUE)opened[handle]=path;
 return handle;
}
bool Controlled(HANDLE file){auto it=opened.find(file);return mode!=Mode::Native&&it!=opened.end()&&it->second.find(fixtureRoot)==0;}
bool Second(HANDLE file){const auto& path=opened.at(file);return path.find(L"identity-b")!=std::wstring::npos||path.find(L"casescope")!=std::wstring::npos;}
BOOL WINAPI Basic(HANDLE file,LPBY_HANDLE_FILE_INFORMATION info){
 const auto ok=::GetFileInformationByHandle(file,info);
 if(ok&&Controlled(file)){
  // Preserve actual directory attributes while holding old identity/stat fields equal.
  info->dwVolumeSerialNumber=7;info->nFileIndexHigh=0;info->nFileIndexLow=mode==Mode::CaseName?0:9;
  info->nFileSizeHigh=0;info->nFileSizeLow=123;info->ftLastWriteTime={456,0};
 }
 return ok;
}
BOOL WINAPI Extended(HANDLE file,FILE_INFO_BY_HANDLE_CLASS kind,LPVOID output,DWORD bytes){
 if(!Controlled(file)||kind!=FileIdInfo)return ::GetFileInformationByHandleEx(file,kind,output,bytes);
 if(mode==Mode::Unsupported||mode==Mode::CaseName||mode==Mode::NoIdentity){SetLastError(ERROR_NOT_SUPPORTED);return FALSE;}
 if(bytes<sizeof(FILE_ID_INFO)){SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
 FILE_ID_INFO info{};info.VolumeSerialNumber=7;info.FileId.Identifier[0]=9;
 if(mode==Mode::UpperId)info.FileId.Identifier[15]=Second(file)?2:1;
 if(mode==Mode::UpperVolume)info.VolumeSerialNumber|=static_cast<ULONGLONG>(Second(file)?2:1)<<32;
 if(mode==Mode::ZeroId){info.FileId.Identifier[0]=Second(file)?1:0;info.VolumeSerialNumber=0;}
 std::memcpy(output,&info,sizeof(info));return TRUE;
}
DWORD WINAPI FinalName(HANDLE file,LPWSTR output,DWORD capacity,DWORD flags){
 if(Controlled(file)&&(mode==Mode::ZeroId||mode==Mode::NoIdentity)){SetLastError(ERROR_NOT_SUPPORTED);return 0;}
 if(Controlled(file)&&mode==Mode::CaseName){
  const auto name=std::wstring(L"\\\\?\\C:\\controlled-api-case\\")+(Second(file)?L"scope":L"Scope");
  if(!output||capacity<=name.size())return static_cast<DWORD>(name.size()+1);
  std::copy(name.begin(),name.end(),output);output[name.size()]=0;return static_cast<DWORD>(name.size());
 }
 return ::GetFinalPathNameByHandleW(file,output,capacity,flags);
}
}

#define CreateFileW IdentityContract::Open
#define GetFileInformationByHandle IdentityContract::Basic
#define GetFileInformationByHandleEx IdentityContract::Extended
#define GetFinalPathNameByHandleW IdentityContract::FinalName
#include "ui/HubViewModel.cpp"
#undef CreateFileW
#undef GetFileInformationByHandle
#undef GetFileInformationByHandleEx
#undef GetFinalPathNameByHandleW
#include "InstallQueueTests.cpp"

int RunInstallQueueIdentityContractTests(){
 int checks=0,failed=0;auto check=[&](bool pass,const char* label){++checks;failed+=!pass;std::cout<<(pass?"PASS ":"FAIL ")<<label<<std::endl;};
 const auto root=nh::UserRoot()/L"queue-identity-contract";std::filesystem::create_directories(root);IdentityContract::fixtureRoot=root.wstring();
 const auto a=Game(root,"identity-a"),b=Game(root,"identity-b");
 auto distinct=[&](){return nh::QueueDirectoryIdentity(a.target.path)!=nh::QueueDirectoryIdentity(b.target.path)&&nh::QueueExecutableStamp(a.target.path)!=nh::QueueExecutableStamp(b.target.path);};
 IdentityContract::mode=IdentityContract::Mode::UpperId;
 check(distinct(),"different upper 64 file-ID bits distinguish folders and executable stamps despite equal basic indexes");
 IdentityContract::mode=IdentityContract::Mode::UpperVolume;
 check(distinct(),"different upper 32 volume bits distinguish folders and executable stamps");
 IdentityContract::mode=IdentityContract::Mode::ZeroId;
 check(distinct(),"successful opaque zero ID remains valid without requiring final-name support");
 IdentityContract::mode=IdentityContract::Mode::UpperId;
 auto alias=a;alias.target.path=nh::Utf8((std::filesystem::path(nh::Wide(a.target.path)).parent_path()/L"."/L"Game.exe").wstring());
 check(nh::QueueDirectoryIdentity(a.target.path)==nh::QueueDirectoryIdentity(alias.target.path)&&nh::QueueExecutableStamp(a.target.path)==nh::QueueExecutableStamp(alias.target.path),"same full physical ID preserves an ordinary dot alias");
 IdentityContract::mode=IdentityContract::Mode::Unsupported;
 check(distinct(),"unsupported FileIdInfo falls back to distinct authentic normalized names");
 check(nh::QueueDirectoryIdentity(a.target.path)==nh::QueueDirectoryIdentity(alias.target.path)&&nh::QueueExecutableStamp(a.target.path)==nh::QueueExecutableStamp(alias.target.path),"unsupported FileIdInfo normalized-name fallback preserves an ordinary alias");
 IdentityContract::mode=IdentityContract::Mode::CaseName;
 check(distinct(),"fallback preserves case-distinct normalized names returned by the API");
 IdentityContract::mode=IdentityContract::Mode::NoIdentity;bool rejected=false;
 try{(void)nh::QueueDirectoryIdentity(a.target.path);}catch(const std::exception&){rejected=true;}
 check(rejected,"unavailable full ID and final-name APIs cannot fabricate folder identity from a truncated index");
 IdentityContract::mode=IdentityContract::Mode::Native;
 check(distinct()&&nh::QueueDirectoryIdentity(a.target.path)==nh::QueueDirectoryIdentity(alias.target.path),"unmodified native metadata distinguishes real folders and preserves a real alias");
 const auto c=Game(root,"CaseScope");auto lower=c;lower.id="case-distinct-model";lower.root=nh::Utf8((root/L"casescope").wstring());lower.target.path=nh::Utf8((root/L"casescope"/L"Game.exe").wstring());
 if(std::filesystem::exists(nh::Wide(lower.target.path))){
  // Underlying local case-insensitive access provides real valid PE files;
  // legal distinct case-sensitive identities are supplied at the API boundary.
  IdentityContract::mode=IdentityContract::Mode::UpperId;nh::HubModel model;model.games={c,lower};model.selected=0;model.ChooseProxy(0);model.Plan("Install");model.Select(1);model.Plan("Install");
  check(model.InstallQueueItems().size()==2,"public enqueue trusts distinct full physical identities over equal case-folded lexical roots");
  ResetQueueForNextCase(model);
 }else std::cout<<"UNAVAILABLE case-spelling controlled-model adapter requires locally reachable alternate spelling; no filesystem setting changed\n";
 IdentityContract::mode=IdentityContract::Mode::Native;
 std::cout<<"RESULT checks="<<checks<<" failed="<<failed<<" ACTUAL_HUBMODEL=true CONTROLLED_LEGAL_METADATA=true NATIVE_REFS_QUALIFIED=false NO_GAME_LAUNCH=true\n";return failed;
}
#ifdef NEUROTIC_INSTALL_QUEUE_IDENTITY_CONTRACT_STANDALONE
int main()try{return RunInstallQueueIdentityContractTests()?1:0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
#endif
