#pragma once
#include "Dav2BackendApi.h"
#include "../DepthBundle.h"
#include <json.hpp>
#include <fstream>
#include <map>
#include <memory>
namespace nrw::depth {
inline constexpr const char* Dav2Checkpoint="715fade13be8f229f8a70cc02066f656f2423a59effd0579197bbf57860e1378";
inline constexpr const char* Dav2Source="a561b849ebae10a6f5ef49e26c83cbbcd36c71bf";
inline constexpr const char* Dav2ReferenceOnnx="9bf391ddca2ad1cda193d5ad2164b56b57e17ee3960821809cfdbb17a6897f0c";
inline constexpr const char* Dav2FastOnnx="aa6f80401c2b70802cbe0112bac51fd2c38003d8513798f916ad5474377eb74c";
inline bool ValidManifest(const nlohmann::json& j,uint32_t w,uint32_t h)noexcept{
 try{
  if(j.at("schema")!=1||j.at("builder")!="neurotic-dav2-local-v1"||j.at("checkpointSha256")!=Dav2Checkpoint||j.at("sourceRevision")!=Dav2Source||j.at("width")!=w||j.at("height")!=h||w!=518||(h!=518&&h!=294)||j.at("precision")!="fp32-io-fp16-tactics"||j.at("io")!="image->relative_depth")return false;
  if(j.at("files").at("model.onnx")!=(h==518?Dav2ReferenceOnnx:Dav2FastOnnx)||j.at("parity")!="synthetic-fp32-reference-passed")return false;
  for(const char* name:{"model.onnx","engine.plan","NeuRotic.DepthAnything.dll","cudart64_12.dll","nvinfer_10.dll"}){
   const auto hash=j.at("files").at(name).get<std::string>();
   if(hash.size()!=64||hash.find_first_not_of("0123456789abcdef")!=std::string::npos)return false;
  }
  const auto& i=j.at("identity");if(i.at("uuid").get<std::string>().size()!=32||i.at("driver").get<int>()<=0||i.at("runtime").get<int>()<12000||i.at("runtime").get<int>()>=13000||i.at("tensorRT").get<int>()/10000!=10||i.at("computeMajor").get<int>()<=0||i.at("driverFileVersion").get<uint64_t>()==0)return false;
  return true;
 }catch(...){return false;}
}
class Dav2EngineCache {
 guidance::DirectoryLeases directories_;std::vector<HANDLE> files_;std::vector<std::byte> engine_;CacheIdentity identity_{};
 HANDLE Hold(const std::filesystem::path& path){auto f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);if(f==INVALID_HANDLE_VALUE)throw std::runtime_error("Depth asset missing: "+path.filename().string());files_.push_back(f);BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(f,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)))throw std::runtime_error("Depth assets cannot be linked or directories");return f;}
 static std::vector<std::byte> Read(HANDLE f,uint64_t limit){LARGE_INTEGER length{},begin{};if(!GetFileSizeEx(f,&length)||length.QuadPart<=0||uint64_t(length.QuadPart)>limit||!SetFilePointerEx(f,begin,nullptr,FILE_BEGIN))throw std::runtime_error("Depth asset length/read refused");std::vector<std::byte> bytes(size_t(length.QuadPart));DWORD read=0;if(!ReadFile(f,bytes.data(),DWORD(bytes.size()),&read,nullptr)||read!=bytes.size())throw std::runtime_error("Depth asset read failed");return bytes;}
public:
 ~Dav2EngineCache(){for(auto f:files_)CloseHandle(f);}
 bool Open(const std::filesystem::path& root,uint32_t w,uint32_t h,std::string& reason){try{
  if(!directories_.Open(root,reason))return false;
  const auto bytes=Read(Hold(directories_.Root()/L"depth-cache.json"),65536);
  const auto j=nlohmann::json::parse(reinterpret_cast<const char*>(bytes.data()),reinterpret_cast<const char*>(bytes.data()+bytes.size()));
  if(!ValidManifest(j,w,h))throw std::runtime_error("Depth cache model/profile/build identity is incompatible");
  const auto& i=j.at("identity");const auto uuid=i.at("uuid").get<std::string>();
  if(uuid.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("Depth cache UUID invalid");
  for(size_t n=0;n<16;++n)identity_.uuid[n]=uint8_t(std::stoul(uuid.substr(n*2,2),nullptr,16));
  identity_.driver=i.at("driver");identity_.runtime=i.at("runtime");identity_.tensorRT=i.at("tensorRT");identity_.computeMajor=i.at("computeMajor");identity_.computeMinor=i.at("computeMinor");identity_.driverFileVersion=i.at("driverFileVersion");
  for(const char* name:{"model.onnx","engine.plan","NeuRotic.DepthAnything.dll","cudart64_12.dll","nvinfer_10.dll"}){const auto f=Hold(directories_.Root()/name);if(guidance::HashFile(f)!=j.at("files").at(name).get<std::string>())throw std::runtime_error(std::string("Depth asset hash mismatch: ")+name);if(std::string(name)=="engine.plan")engine_=Read(f,512ull<<20);}
  reason.clear();return true;
 }catch(const std::exception& e){reason=e.what();return false;}}
 const std::vector<std::byte>& Engine()const{return engine_;}
 const CacheIdentity& Identity()const{return identity_;}
 const std::filesystem::path& Root()const{return directories_.Root();}
};
}
