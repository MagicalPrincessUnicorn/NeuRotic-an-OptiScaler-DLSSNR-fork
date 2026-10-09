#pragma once
#include <windows.h>
#include <filesystem>
#include <mutex>
#include <string>
#include <cstdint>
#include <memory>
#include "ConfigFileLockDiagnostics.h"
#define CONFIG_PERSISTENCE_ASYNC 1

namespace ConfigPersistence {
struct Snapshot {
 bool attempted=false,succeeded=false;
 std::string path,stage,message;
 DWORD errorCode=ERROR_SUCCESS;
 uint64_t sequence=0;
 ConfigFileLockDiagnostics::Result fileUsers;
 bool fileUsersPending=false;
};
class Owner {
 using QueryFunction=ConfigFileLockDiagnostics::Result(*)(const std::filesystem::path&);
 using DiagnosticSink=void(*)(const Snapshot&);
 struct Shared {
  std::mutex mutex;
  Snapshot state;
  std::filesystem::path nativePath;
  bool workerRunning=false;
  QueryFunction query;
  DiagnosticSink sink=nullptr;
  explicit Shared(QueryFunction function):query(function){}
 };
 std::shared_ptr<Shared> shared;
 struct Work { std::shared_ptr<Shared> shared; HMODULE module=nullptr; };
 static DWORD WINAPI Worker(void* parameter) {
  HMODULE module=nullptr;
  {
   // Destroy all C++ owners before atomically releasing the module and exiting.
   std::unique_ptr<Work> work(static_cast<Work*>(parameter));module=work->module;
   auto& data=*work->shared;
   try {
    for(;;){
     std::filesystem::path path;uint64_t sequence=0;
     {
      std::lock_guard lock(data.mutex);
      if(!data.state.fileUsersPending){data.workerRunning=false;break;}
      path=data.nativePath;sequence=data.state.sequence;
     }
     ConfigFileLockDiagnostics::Result result;
     try{result=data.query(path);}catch(...){result.errorCode=ERROR_GEN_FAILURE;}
     DiagnosticSink sink=nullptr;Snapshot completed;
     {
      std::lock_guard lock(data.mutex);
      if(data.state.sequence==sequence&&data.state.fileUsersPending){
       data.state.fileUsers=std::move(result);data.state.fileUsersPending=false;
       sink=data.sink;if(sink)completed=data.state;
      }
      // A newer failure stays pending for the same worker. A successful save
      // clears it, so this completion cannot overwrite success or stale users.
     }
     if(sink){try{sink(completed);}catch(...) {}}
    }
   }catch(...){
    std::lock_guard lock(data.mutex);data.workerRunning=false;
    data.state.fileUsersPending=false;data.state.fileUsers.errorCode=ERROR_GEN_FAILURE;
   }
  }
  FreeLibraryAndExitThread(module,0);
 }
 void StartQueryLocked(){
  auto& data=*shared;if(data.workerRunning)return;
  HMODULE module=nullptr;
  try{
   auto work=std::make_unique<Work>();work->shared=shared;
   if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&Worker),&module))throw 1;
   work->module=module;data.workerRunning=true;
   const auto thread=CreateThread(nullptr,1024*1024,&Worker,work.get(),0,nullptr);
   if(!thread)throw 1;
   work.release();module=nullptr;CloseHandle(thread);
  }catch(...){
   data.workerRunning=false;data.state.fileUsersPending=false;
   data.state.fileUsers.errorCode=GetLastError();
   if(data.state.fileUsers.errorCode==ERROR_SUCCESS)data.state.fileUsers.errorCode=ERROR_GEN_FAILURE;
   if(module)FreeLibrary(module);
  }
 }
public:
 explicit Owner(QueryFunction query=&ConfigFileLockDiagnostics::Query):shared(std::make_shared<Shared>(query)){}
 Owner(const Owner&)=delete;Owner& operator=(const Owner&)=delete;
 void Publish(const std::filesystem::path& path,bool succeeded,DWORD error,const std::string& stage,DiagnosticSink sink=nullptr){
  std::lock_guard lock(shared->mutex);auto& state=shared->state;
  const auto utf8=path.u8string();
  state.attempted=true;state.succeeded=succeeded;
  state.path.assign(reinterpret_cast<const char*>(utf8.data()),utf8.size());state.stage=stage;
  state.errorCode=error;if(state.sequence!=UINT64_MAX)++state.sequence;
  state.fileUsers={};state.fileUsersPending=!succeeded&&error==ERROR_SHARING_VIOLATION;
  shared->nativePath=path;shared->sink=sink;
  state.message.clear();
  if(!succeeded){wchar_t text[512]{};const auto count=FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,error,0,text,512,nullptr);
   if(count){const auto bytes=WideCharToMultiByte(CP_UTF8,0,text,count,nullptr,0,nullptr,nullptr);
    if(bytes>0){state.message.resize(bytes);WideCharToMultiByte(CP_UTF8,0,text,count,state.message.data(),bytes,nullptr,nullptr);}
    while(!state.message.empty()&&(state.message.back()=='\r'||state.message.back()=='\n'))state.message.pop_back();}}
  if(state.fileUsersPending)StartQueryLocked();
 }
 Snapshot Read(){std::lock_guard lock(shared->mutex);return shared->state;}
};
inline Owner& Current(){static Owner owner;return owner;}
}
