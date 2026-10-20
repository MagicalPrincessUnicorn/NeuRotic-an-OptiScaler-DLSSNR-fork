#include "NrDllManager.h"
#include <chrono>

namespace nh {
bool NrDllManager::Start(Action action,std::filesystem::path source,std::string id,std::string name){
 if(Busy()||stopping_)return false;
 if(action==Action::Refresh||action==Action::SetDefault||action==Action::ClearDefault||(action==Action::Remove&&id==catalog_.defaultId))catalogCurrent_=false;
 stop_=std::stop_source{};error_.reset();systemError_=0;
 const auto root=root_;const bool writable=writable_;const auto token=stop_.get_token();
 try{worker_=std::async(std::launch::async,[root,writable,token,action,source=std::move(source),id=std::move(id),name=std::move(name)]{
  Result result;std::optional<NrDllProfiles> store;
  try{
   store.emplace(root,writable);
   if(token.stop_requested())throw NrDllError(NrDllErrorCode::Cancelled);
   switch(action){
    case Action::Refresh:break;
    case Action::Import:store->Import(source,name,token);break;
    case Action::Rename:store->Rename(id,name);break;
    case Action::Remove:store->Remove(id);break;
    case Action::SetDefault:store->SetDefault(id);break;
    case Action::ClearDefault:store->ClearDefault();break;
   }
  }catch(const NrDllError& e){result.error=e.code;result.systemError=e.systemError;}
   catch(...){result.error=NrDllErrorCode::IoError;}
  // A failed multi-file operation can leave a truthful, changed catalog. Read
  // it again instead of inventing either success or the old on-disk state.
  try{if(store)result.catalog=store->List();}catch(const NrDllError& e){if(!result.error){result.error=e.code;result.systemError=e.systemError;}}
   catch(...){if(!result.error)result.error=NrDllErrorCode::IoError;}
  return result;
 });}catch(...){error_=NrDllErrorCode::IoError;systemError_=0;return false;}
 return true;
}
bool NrDllManager::Refresh(){return Start(Action::Refresh);}
bool NrDllManager::Import(std::filesystem::path source,std::string name){return Start(Action::Import,std::move(source),{},std::move(name));}
bool NrDllManager::Rename(std::string id,std::string name){return Start(Action::Rename,{},std::move(id),std::move(name));}
bool NrDllManager::Remove(std::string id){return Start(Action::Remove,{},std::move(id));}
bool NrDllManager::SetDefault(std::string id){return Start(Action::SetDefault,{},std::move(id));}
bool NrDllManager::ClearDefault(){return Start(Action::ClearDefault);}
void NrDllManager::RequestStop() noexcept{stopping_=true;stop_.request_stop();}
void NrDllManager::Poll(){
 if(!worker_.valid()||worker_.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)return;
 try{auto result=worker_.get();catalogCurrent_=result.catalog.has_value();if(result.catalog){catalog_=std::move(*result.catalog);ready_=true;}error_=result.error;systemError_=result.systemError;}
 catch(...){catalogCurrent_=false;error_=NrDllErrorCode::IoError;systemError_=0;}
}
}
