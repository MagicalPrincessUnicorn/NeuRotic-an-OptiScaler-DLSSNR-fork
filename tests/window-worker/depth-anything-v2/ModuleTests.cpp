#include <iostream>
#if __has_include("../../../apps/NeuRoticWindowWorker/guidance/dav2/Dav2Module.h")
#include "../../../apps/NeuRoticWindowWorker/guidance/dav2/Dav2Module.h"
#if __has_include("../../../apps/NeuRoticWindowWorker/guidance/dav2/Dav2EngineCache.h")
#include "../../../apps/NeuRoticWindowWorker/guidance/dav2/Dav2EngineCache.h"
#include <fstream>
#endif
int main(){nrw::depth::Dav2Module module;std::string reason;
 if(module.Open(L"C:/absent-depth-module",reason)||reason.empty()){std::cerr<<"FAIL missing backend did not fail locally\n";return 1;}
 if(nrw::depth::ValidApi(nullptr)){std::cerr<<"FAIL null ABI accepted\n";return 1;}
 nrw::depth::BackendApi api{};api.size=sizeof(api);api.version=99;
 if(nrw::depth::ValidApi(&api)){std::cerr<<"FAIL wrong ABI accepted\n";return 1;}
 #if __has_include("../../../apps/NeuRoticWindowWorker/guidance/dav2/Dav2EngineCache.h")
 nrw::depth::Dav2EngineCache cache;
 if(cache.Open(L"C:/absent-depth-cache",518,518,reason)||reason.empty()){std::cerr<<"FAIL missing cache accepted\n";return 1;}
 if(nrw::depth::ValidManifest(nlohmann::json::object(),518,518)){std::cerr<<"FAIL unpinned engine accepted\n";return 1;}
 #else
 std::cerr<<"FAIL verified local engine cache missing\n";return 1;
 #endif
 nrw::depth::Dav2Module built;
 if(!built.Open(std::filesystem::absolute(L"builds/depth-anything-v2/backend"),reason)){std::cerr<<"FAIL compiled optional module: "<<reason<<'\n';return 1;}
 char error[1024]{};nrw::depth::BackendCreate invalid;
 if(built.Api()->create(&invalid,error,sizeof(error))||!error[0]||built.Api()->poll(nullptr,error,sizeof(error))!=nrw::depth::BackendPoll::Faulted||built.Api()->submit(nullptr,1,1,error,sizeof(error))||!built.Api()->close(nullptr)){std::cerr<<"FAIL actual backend refuses invalid ownership\n";return 1;}
 nlohmann::json manifest={{"schema",1},{"builder","neurotic-dav2-local-v1"},{"checkpointSha256",nrw::depth::Dav2Checkpoint},{"sourceRevision",nrw::depth::Dav2Source},{"width",518},{"height",518},{"precision","fp32-io-fp16-tactics"},{"io","image->relative_depth"},{"parity","synthetic-fp32-reference-passed"},{"identity",{{"uuid",std::string(32,'0')},{"driver",12090},{"runtime",12090},{"tensorRT",101303},{"computeMajor",8},{"computeMinor",9},{"driverFileVersion",uint64_t(1)}}},{"files",nlohmann::json::object()}};
 for(const char* name:{"model.onnx","engine.plan","NeuRotic.DepthAnything.dll","cudart64_12.dll","nvinfer_10.dll"})manifest["files"][name]=std::string(64,'0');manifest["files"]["model.onnx"]=nrw::depth::Dav2ReferenceOnnx;
 if(!nrw::depth::ValidManifest(manifest,518,518)){std::cerr<<"FAIL pinned manifest admission\n";return 1;}
 auto fake=manifest;fake["files"]["model.onnx"]=std::string(64,'1');if(nrw::depth::ValidManifest(fake,518,518)){std::cerr<<"FAIL substituted ONNX is not trusted\n";return 1;}
 const auto fixture=std::filesystem::absolute(L"builds/depth-anything-v2/module/cache-fixture");if(std::filesystem::exists(fixture)){std::cerr<<"FAIL cache fixture unexpectedly exists\n";return 1;}std::filesystem::create_directory(fixture);
 {std::ofstream file(fixture/L"depth-cache.json");file<<manifest.dump();}
 {std::ofstream file(fixture/L"model.onnx");file<<"corrupt-model";}
 {nrw::depth::Dav2EngineCache corrupt;if(corrupt.Open(fixture,518,518,reason)||reason.find("hash mismatch")==std::string::npos){std::cerr<<"FAIL corrupted model admission: "<<reason<<'\n';return 1;}}
 std::filesystem::remove(fixture/L"depth-cache.json");std::filesystem::remove(fixture/L"model.onnx");std::filesystem::remove(fixture);
 std::cout<<"PASS optional backend ABI, compiled API and pinned/corrupt cache admission\n";return 0;}
#else
int main(){std::cerr<<"FAIL optional Depth Anything backend loader missing\n";return 1;}
#endif
