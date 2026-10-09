#pragma once
#include "install/OperationController.h"
#include <fstream>
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")
namespace nh::test {
// An inert, tiny package for dispatch/approval tests. Never loads its stand-in
// loader or Inspector. Runtime/cohort qualification has separate real fixtures.
class NativeInstallerPackageFixture {
 std::filesystem::path registry;std::string previous;bool existed=false;
 static std::string Read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
 static void Write(const std::filesystem::path& p,const std::string& bytes){std::filesystem::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f.write(bytes.data(),bytes.size());if(!f)throw std::runtime_error("Fixture write failed.");}
 static std::string Digest(const std::string& bytes){
  BCRYPT_ALG_HANDLE a=nullptr;BCRYPT_HASH_HANDLE h=nullptr;unsigned char output[32];
  if(BCryptOpenAlgorithmProvider(&a,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("Fixture hash unavailable.");
  bool ok=BCryptCreateHash(a,&h,nullptr,0,nullptr,0,0)>=0&&BCryptHashData(h,(PUCHAR)bytes.data(),(ULONG)bytes.size(),0)>=0&&BCryptFinishHash(h,output,32,0)>=0;
  if(h)BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(a,0);if(!ok)throw std::runtime_error("Fixture hash failed.");std::string out;constexpr char hex[]="0123456789abcdef";for(auto v:output){out+=hex[v>>4];out+=hex[v&15];}return out;
 }
public:
 explicit NativeInstallerPackageFixture(const std::filesystem::path& fixture){
  auto root=fixture/L"inert-native-package";registry=AppRoot()/L"support/Hub-Packages.json";existed=std::filesystem::exists(registry);if(existed)previous=Read(registry);
  wchar_t system[MAX_PATH];GetSystemDirectoryW(system,MAX_PATH);
  const std::string worker="OptiScaler/CharacterInspector/Version 1/worker.py";
  std::map<std::string,std::string> payload={{"OptiScaler.dll",Read(std::filesystem::path(system)/L"cmd.exe")},{"OptiScaler.ini","[Hooks]\nDxgi=auto\n[Plugins]\nLoadReShade=false\n[DlssNr]\nEnabled=false\n"},{worker,"# Inert protocol fixture; never run.\n"}};
  Json rows=Json::array();for(const auto& [path,bytes]:payload){Write(root/Wide("payload/"+path),bytes);rows.push_back({{"path","payload/"+path},{"bytes",bytes.size()},{"sha256",Digest(bytes)}});}
  auto manifest=Json{{"components",{{"characterInspector",{{"version","Version 1"},{"relativeRoot","OptiScaler/CharacterInspector/Version 1"}}}}},{"files",rows}}.dump();Write(root/L"support/PACKAGE-MANIFEST.json",manifest);
  auto relative=std::filesystem::relative(root,registry.parent_path());Write(registry,Json{{"schemaVersion",1},{"packages",Json::array({{{"id","flagship-approved"},{"relativeRoot",Utf8(relative.wstring())},{"manifestSha256",Digest(manifest)}}})}}.dump());
 }
 ~NativeInstallerPackageFixture(){if(existed){try{Write(registry,previous);}catch(...){}}else{std::error_code e;std::filesystem::remove(registry,e);}}
 NativeInstallerPackageFixture(const NativeInstallerPackageFixture&)=delete;
};
}
