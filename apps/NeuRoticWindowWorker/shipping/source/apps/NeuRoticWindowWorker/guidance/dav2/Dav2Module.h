#pragma once
#include "Dav2BackendApi.h"
#include "../DepthBundle.h"
#include <memory>
namespace nrw::depth {
class Dav2Module {
 std::unique_ptr<guidance::DirectoryLeases> directories_;
 HANDLE file_=INVALID_HANDLE_VALUE;HMODULE module_=nullptr;const BackendApi* api_=nullptr;
public:
 Dav2Module()=default;Dav2Module(const Dav2Module&)=delete;~Dav2Module();
 bool Open(const std::filesystem::path&,std::string&);
 const BackendApi* Api()const noexcept{return api_;}
};
}
