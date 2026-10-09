#include "Dav2Module.h"
namespace nrw::depth {
Dav2Module::~Dav2Module(){if(module_)FreeLibrary(module_);if(file_!=INVALID_HANDLE_VALUE)CloseHandle(file_);}
bool Dav2Module::Open(const std::filesystem::path& root,std::string& reason){
 if(module_||directories_){reason="Depth backend already opened";return false;}
 directories_=std::make_unique<guidance::DirectoryLeases>();
 if(!directories_->Open(root,reason))return false;
 const auto path=directories_->Root()/L"NeuRotic.DepthAnything.dll";
 file_=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
 BY_HANDLE_FILE_INFORMATION info{};
 if(file_==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(file_,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))){reason="Depth Anything backend is not prepared in the selected local bundle";return false;}
 module_=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
 if(!module_||!guidance::LoadedFrom(module_,path)){reason="Depth Anything backend could not be loaded from the selected bundle";return false;}
 const auto get=reinterpret_cast<GetBackendApi>(GetProcAddress(module_,"NrDav2GetApi"));
 api_=get?get(BackendVersion):nullptr;
 if(!ValidApi(api_)){api_=nullptr;reason="Depth Anything backend API version is incompatible";return false;}
 reason.clear();return true;
}
}
