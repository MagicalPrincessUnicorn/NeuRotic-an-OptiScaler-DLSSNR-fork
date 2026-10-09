#pragma once
#include <windows.h>
#include <RestartManager.h>
#include <filesystem>
#include <string>
#include <vector>
#pragma comment(lib, "Rstrtmgr.lib")

// Diagnostic only: never stop, restart or close another process or its handles.
namespace ConfigFileLockDiagnostics {
#define CONFIG_FILE_OWNER_PROCESS_IDENTITY 1
struct Owner { DWORD processId=0; std::string name; bool currentProcess=false; };
struct Result { DWORD errorCode=ERROR_SUCCESS; std::vector<Owner> owners; };
inline Result Query(const std::filesystem::path& path)
{
 DWORD session=0;wchar_t key[CCH_RM_SESSION_KEY+1]{};
 Result result;
 result.errorCode=RmStartSession(&session,0,key);
 if(result.errorCode!=ERROR_SUCCESS)return result;
 struct Session { DWORD handle; ~Session(){RmEndSession(handle);} } lifetime{session};
 const auto native=std::filesystem::absolute(path).wstring();const wchar_t* file=native.c_str();
 result.errorCode=RmRegisterResources(session,1,&file,0,nullptr,0,nullptr);
 if(result.errorCode!=ERROR_SUCCESS)return result;
 // A resource can change while queried. Bound both retries and allocation.
 std::vector<RM_PROCESS_INFO> processes(8);
 for(unsigned attempt=0;attempt<3;++attempt){
  UINT needed=0,count=static_cast<UINT>(processes.size());DWORD reasons=0;
  result.errorCode=RmGetList(session,&needed,&count,processes.data(),&reasons);
  if(result.errorCode==ERROR_MORE_DATA){if(needed>64)return result;processes.resize(needed);continue;}
  if(result.errorCode!=ERROR_SUCCESS)return result;
  for(UINT i=0;i<count;++i){
   const auto& process=processes[i];Owner owner;owner.processId=process.Process.dwProcessId;
   owner.currentProcess=owner.processId==GetCurrentProcessId();
   const int bytes=WideCharToMultiByte(CP_UTF8,0,process.strAppName,-1,nullptr,0,nullptr,nullptr);
   if(bytes>0){owner.name.resize(bytes);WideCharToMultiByte(CP_UTF8,0,process.strAppName,-1,owner.name.data(),bytes,nullptr,nullptr);owner.name.pop_back();}
   result.owners.push_back(std::move(owner));
  }
  return result;
 }
 return result;
}
}
