#include <menu/Localization.h>
#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <string>
#include <vector>
#include <stdexcept>
#pragma comment(lib,"bcrypt.lib")
namespace nh {
// Shares directory identity and admission with CMD/installer mutations. The UI
// thread owns this lease from preflight through launch; no game file is created.
class LaunchAdmission {
 std::vector<HANDLE> pins;HANDLE mutex=nullptr,process=nullptr;bool owned=false;
 ULONGLONG until=0;
 HANDLE Pin(const std::filesystem::path& path,bool directory){
  auto h=CreateFileW(path.c_str(),directory?FILE_READ_ATTRIBUTES:GENERIC_READ,directory?FILE_SHARE_READ|FILE_SHARE_WRITE:FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:0),nullptr);
  if(h==INVALID_HANDLE_VALUE)throw std::runtime_error(Neurotic::UiMessage("desktop.launchadmission.launch_files_are_unavailable_or_in_use_refresh_a_572ce3a3", "Launch files are unavailable or in use. Refresh and retry."));
  pins.push_back(h);BY_HANDLE_FILE_INFORMATION info{};
  if(!GetFileInformationByHandle(h,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||bool(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=directory||(!directory&&info.nNumberOfLinks!=1))throw std::runtime_error(Neurotic::UiMessage("desktop.launchadmission.launch_requires_ordinary_unlinked_files_74948a94", "Launch requires ordinary, unlinked files."));
  return h;
 }
 void Parents(const std::filesystem::path& path){
  auto name=path.wstring();if(!path.is_absolute()||name.starts_with(L"\\\\")||name.find(L':',2)!=std::wstring::npos||path.lexically_normal()!=path)throw std::runtime_error(Neurotic::UiMessage("desktop.launchadmission.launch_requires_a_local_absolute_path_46fd1dd6", "Launch requires a local absolute path."));
  auto walk=path.root_path();Pin(walk,true);for(auto& part:path.relative_path()){walk/=part;Pin(walk,true);}
 }
 public:
 std::wstring name;
 LaunchAdmission()=default;LaunchAdmission(const LaunchAdmission&)=delete;
 ~LaunchAdmission(){if(process)CloseHandle(process);for(auto h:pins)CloseHandle(h);if(owned)ReleaseMutex(mutex);if(mutex)CloseHandle(mutex);}
 void Enter(const std::filesystem::path& root){
  Parents(root);BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(pins.back(),&info))throw std::runtime_error(Neurotic::UiMessage("desktop.launchadmission.game_folder_identity_unavailable_759dcaba", "Game folder identity unavailable."));
  wchar_t id[32];swprintf_s(id,L"%08X%08X%08X",info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow);name=L"Global\\NeuRotic-Installer-"+std::wstring(id);
  mutex=CreateMutexW(nullptr,FALSE,name.c_str());if(!mutex)throw std::runtime_error(Neurotic::UiMessage("desktop.launchadmission.game_launch_admission_unavailable_caf73824", "Game launch admission unavailable."));auto result=WaitForSingleObject(mutex,0);owned=result==WAIT_OBJECT_0||result==WAIT_ABANDONED;
  if(!owned)throw std::runtime_error(Neurotic::UiMessage("desktop.launchadmission.an_installation_or_launch_is_already_using_this__ab5252a1", "An installation or launch is already using this game."));
 }
 void Verify(const std::filesystem::path& path,const std::string& expected){
  if(expected.size()!=64)throw std::runtime_error(Neurotic::UiMessage("desktop.launchadmission.launch_receipt_lacks_a_file_identity_6c1310f7", "Launch receipt lacks a file identity."));Parents(path.parent_path());auto file=Pin(path,false);
  BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
  struct Cleanup {BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~Cleanup(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}} cleanup{algorithm,hash};
  if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0||BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0)throw std::runtime_error(Neurotic::UiMessage("desktop.launchadmission.launch_verification_unavailable_c7550214", "Launch verification unavailable."));
  unsigned char buffer[65536],digest[32];DWORD count=0;for(;;){if(!ReadFile(file,buffer,sizeof(buffer),&count,nullptr))throw std::runtime_error(Neurotic::UiMessage("desktop.launchadmission.launch_file_read_failed_b105d282", "Launch file read failed."));if(!count)break;if(BCryptHashData(hash,buffer,count,0)<0)throw std::runtime_error(Neurotic::UiMessage("desktop.launchadmission.launch_file_hash_failed_3a0e4466", "Launch file hash failed."));}
  if(BCryptFinishHash(hash,digest,32,0)<0)throw std::runtime_error(Neurotic::UiMessage("desktop.launchadmission.launch_file_hash_failed_3a0e4466", "Launch file hash failed."));std::string actual;constexpr char hex[]="0123456789abcdef";for(auto value:digest){actual+=hex[value>>4];actual+=hex[value&15];}
  auto normalized=expected;for(auto& c:normalized){if(c>='A'&&c<='F')c=char(c-'A'+'a');if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))throw std::runtime_error(Neurotic::UiMessage("desktop.launchadmission.invalid_launch_file_identity_32dc16ac", "Invalid launch file identity."));}
  if(actual!=normalized)throw std::runtime_error(Neurotic::UiMessage("desktop.launchadmission.launch_files_changed_after_checking_refresh_and__6112ee0e", "Launch files changed after checking. Refresh and repair this installation."));
 }
 void VerifyStat(const std::filesystem::path& path,const std::string& expected){
  Parents(path.parent_path());auto file=Pin(path,false);BY_HANDLE_FILE_INFORMATION i{};
  if(!GetFileInformationByHandle(file,&i))throw std::runtime_error(Neurotic::UiMessage("desktop.launchadmission.game_executable_identity_is_unavailable_0166a271", "Game executable identity is unavailable."));
  char id[160];sprintf_s(id,"%08X%08X%08X:%u:%u:%d:%d",i.dwVolumeSerialNumber,i.nFileIndexHigh,i.nFileIndexLow,i.nFileSizeHigh,i.nFileSizeLow,(int)i.ftLastWriteTime.dwHighDateTime,(int)i.ftLastWriteTime.dwLowDateTime);
  if(expected!=id)throw std::runtime_error(Neurotic::UiMessage("desktop.launchadmission.game_executable_changed_refresh_before_launching_603adf3c", "Game executable changed. Refresh before launching."));
 }
 void Hold(HANDLE child,ULONGLONG handoffMs=30000){process=child;until=GetTickCount64()+handoffMs;}
 void Handoff(){if(owned){ReleaseMutex(mutex);owned=false;}}
 bool Finished()const{return until&&(process?WaitForSingleObject(process,0)==WAIT_OBJECT_0:GetTickCount64()>=until);}
};
}
