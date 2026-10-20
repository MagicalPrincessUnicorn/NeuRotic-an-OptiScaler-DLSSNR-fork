// GPL-3.0. Dedicated supervised preparation process, never a capture/render callback.
#include "../../apps/NeuRoticWindowWorker/guidance/dav2/Dav2EngineCache.h"
#include "../../apps/NeuRoticWindowWorker/guidance/dav2/DriverIdentity.h"
#include <cuda_runtime_api.h>
#pragma warning(push)
#pragma warning(disable:4100)
#include <NvInfer.h>
#include <NvOnnxParser.h>
#pragma warning(pop)
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <cmath>
using namespace nrw::depth;using Json=nlohmann::json;
struct Library {
 HMODULE value=nullptr;
 explicit Library(const std::filesystem::path& path){value=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!value||!nrw::guidance::LoadedFrom(value,path))throw std::runtime_error("Missing selected official preparation runtime: "+path.filename().string());}
 ~Library(){if(value)FreeLibrary(value);}
 template<class T>T Symbol(const char* name){const auto p=GetProcAddress(value,name);if(!p)throw std::runtime_error(std::string("Missing runtime API: ")+name);return reinterpret_cast<T>(p);}
};
#define STRING_IMPL(x) #x
#define STRING(x) STRING_IMPL(x)
struct Cuda {
 Library lib;
#define FN(name) decltype(&name) name##_=lib.Symbol<decltype(&name)>(STRING(name))
 FN(cudaSetDevice);FN(cudaGetDeviceProperties);FN(cudaDriverGetVersion);FN(cudaRuntimeGetVersion);FN(cudaMalloc);FN(cudaFree);FN(cudaMemcpy);FN(cudaStreamCreateWithFlags);FN(cudaStreamSynchronize);FN(cudaStreamDestroy);
#undef FN
 explicit Cuda(const std::filesystem::path& root):lib(root/L"cudart64_12.dll"){}
};
void CheckCuda(cudaError_t result){if(result!=cudaSuccess)throw std::runtime_error("CUDA preparation failed: "+std::to_string(int(result)));}
struct Assets {
 nrw::guidance::DirectoryLeases dirs;std::vector<HANDLE> handles;
 ~Assets(){for(auto f:handles)CloseHandle(f);}
 HANDLE Hold(const std::filesystem::path& p){auto f=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(f==INVALID_HANDLE_VALUE)throw std::runtime_error("Missing preparation asset: "+p.filename().string());handles.push_back(f);BY_HANDLE_FILE_INFORMATION i{};if(!GetFileInformationByHandle(f,&i)||(i.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)))throw std::runtime_error("Linked preparation assets refused");return f;}
};
std::vector<char> Read(const std::filesystem::path& path,size_t limit){std::ifstream f(path,std::ios::binary|std::ios::ate);const auto bytes=f.tellg();if(bytes<=0||uint64_t(bytes)>limit)throw std::runtime_error("Invalid preparation asset size");std::vector<char> data((size_t(bytes)));f.seekg(0);f.read(data.data(),bytes);if(!f)throw std::runtime_error("Preparation asset read failed");return data;}
struct Log:nvinfer1::ILogger{void log(Severity s,const char* message)noexcept override{if(s<=Severity::kWARNING&&message)std::fprintf(stderr,"Depth preparation: %.1024s\n",message);}};
bool Shape(nvinfer1::Dims d,std::initializer_list<int64_t> dimensions){if(d.nbDims!=int(dimensions.size()))return false;int i=0;for(auto v:dimensions)if(d.d[i++]!=v)return false;return true;}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=3){std::cerr<<"Usage: NeuRotic.DepthPrepare.exe absolute-building-directory cuda-device\n";return 2;}
 const auto root=std::filesystem::absolute(argv[1]);const int device=std::stoi(argv[2]);if(device<0||std::filesystem::exists(root/L"engine.plan")||std::filesystem::exists(root/L"depth-cache.json"))throw std::runtime_error("Preserve existing local cache; fresh preparation directory required");
 Assets assets;std::string reason;if(!assets.dirs.Open(root,reason))throw std::runtime_error(reason);
 for(const wchar_t* name:{L"model.onnx",L"model.manifest.json",L"parity-input.r32",L"parity-output.r32",L"NeuRotic.DepthAnything.dll",L"cudart64_12.dll",L"nvinfer_10.dll",L"nvonnxparser_10.dll",L"nvinfer_builder_resource_10.dll"})assets.Hold(root/name);
 const auto metadata=Read(root/L"model.manifest.json",65536);const auto m=Json::parse(metadata.begin(),metadata.end());const auto shape=m.at("input_shape");const int height=shape.at(2),width=shape.at(3);
 if(m.at("source_commit")!=Dav2Source||m.at("checkpoint_sha256")!=Dav2Checkpoint||m.at("input_name")!="image"||m.at("output_name")!="relative_depth"||m.at("io_dtype")!="float32"||shape!=Json::array({1,3,height,width})||width!=518||(height!=518&&height!=294))throw std::runtime_error("Preparation requires verified pinned Small static export");
 if(nrw::guidance::HashFile(assets.handles[0])!=m.at("onnx_sha256").get<std::string>())throw std::runtime_error("ONNX export hash mismatch");
 Cuda cuda(root);CheckCuda(cuda.cudaSetDevice_(device));cudaDeviceProp properties{};CheckCuda(cuda.cudaGetDeviceProperties_(&properties,device));if(properties.luidDeviceNodeMask!=1)throw std::runtime_error("Single-node Windows CUDA device required");int driver=0,runtimeVersion=0;CheckCuda(cuda.cudaDriverGetVersion_(&driver));CheckCuda(cuda.cudaRuntimeGetVersion_(&runtimeVersion));const auto driverFileVersion=CudaDriverFileVersion();if(!driverFileVersion||runtimeVersion<12000||runtimeVersion>=13000)throw std::runtime_error("CUDA 12 and actual NVIDIA driver identity required");
 Library trt(root/L"nvinfer_10.dll"),onnxParser(root/L"nvonnxparser_10.dll");using Create=void* (__cdecl *)(void*,int32_t);using Parser=void* (__cdecl *)(void*,void*,int32_t);using Version=int32_t (__cdecl *)();const auto trtVersion=trt.Symbol<Version>("getInferLibVersion")();if(trtVersion/10000!=10)throw std::runtime_error("TensorRT 10 required");
 Log log;std::unique_ptr<nvinfer1::IBuilder> builder(static_cast<nvinfer1::IBuilder*>(trt.Symbol<Create>("createInferBuilder_INTERNAL")(&log,NV_TENSORRT_VERSION)));if(!builder)throw std::runtime_error("TensorRT builder unavailable");
 std::unique_ptr<nvinfer1::INetworkDefinition> net(builder->createNetworkV2(0));if(!net)throw std::runtime_error("Depth network creation failed");std::unique_ptr<nvonnxparser::IParser> parser(static_cast<nvonnxparser::IParser*>(onnxParser.Symbol<Parser>("createNvOnnxParser_INTERNAL")(net.get(),&log,NV_ONNX_PARSER_VERSION)));auto model=Read(root/L"model.onnx",512ull<<20);if(!parser||!parser->parse(model.data(),model.size()))throw std::runtime_error("Depth ONNX parse failed");
 if(net->getNbInputs()!=1||net->getNbOutputs()!=1||std::strcmp(net->getInput(0)->getName(),"image")||std::strcmp(net->getOutput(0)->getName(),"relative_depth")||!Shape(net->getInput(0)->getDimensions(),{1,3,height,width})||!Shape(net->getOutput(0)->getDimensions(),{1,height,width}))throw std::runtime_error("Depth graph binding/shape mismatch");
 for(auto* t:{net->getInput(0),net->getOutput(0)}){if(t->getType()!=nvinfer1::DataType::kFLOAT)throw std::runtime_error("FP32 IO required");t->setAllowedFormats(1u<<unsigned(nvinfer1::TensorFormat::kLINEAR));}
 std::unique_ptr<nvinfer1::IBuilderConfig> config(builder->createBuilderConfig());if(!config)throw std::runtime_error("Depth builder configuration failed");
#pragma warning(push)
#pragma warning(disable:4996) // TensorRT 10 weakly typed FP16 tactics are the explicitly selected contract.
 config->setFlag(nvinfer1::BuilderFlag::kFP16);
#pragma warning(pop)
 config->setMemoryPoolLimit(nvinfer1::MemoryPoolType::kWORKSPACE,512ull<<20);config->setMaxAuxStreams(0);std::unique_ptr<nvinfer1::IHostMemory> plan(builder->buildSerializedNetwork(*net,*config));if(!plan||plan->size()>(512ull<<20))throw std::runtime_error("Depth engine build failed");
 std::unique_ptr<nvinfer1::IRuntime> rt(static_cast<nvinfer1::IRuntime*>(trt.Symbol<Create>("createInferRuntime_INTERNAL")(&log,NV_TENSORRT_VERSION)));if(!rt)throw std::runtime_error("Depth engine validation runtime unavailable");std::unique_ptr<nvinfer1::ICudaEngine> engine(rt->deserializeCudaEngine(plan->data(),plan->size()));if(!engine||engine->getNbIOTensors()!=2)throw std::runtime_error("Built depth engine failed validation");
 for(const char* name:{"image","relative_depth"})if(engine->getTensorDataType(name)!=nvinfer1::DataType::kFLOAT||engine->getTensorFormat(name)!=nvinfer1::TensorFormat::kLINEAR||engine->getTensorLocation(name)!=nvinfer1::TensorLocation::kDEVICE)throw std::runtime_error("Built engine IO format mismatch");
 const size_t pixels=size_t(width)*height;auto input=Read(root/L"parity-input.r32",pixels*12),expected=Read(root/L"parity-output.r32",pixels*4);if(input.size()!=pixels*12||expected.size()!=pixels*4)throw std::runtime_error("Pinned FP32 parity tensor size mismatch");
 void* in=nullptr;void* out=nullptr;cudaStream_t stream=nullptr;CheckCuda(cuda.cudaMalloc_(&in,input.size()));CheckCuda(cuda.cudaMalloc_(&out,expected.size()));CheckCuda(cuda.cudaMemcpy_(in,input.data(),input.size(),cudaMemcpyHostToDevice));CheckCuda(cuda.cudaStreamCreateWithFlags_(&stream,cudaStreamNonBlocking));std::unique_ptr<nvinfer1::IExecutionContext> context(engine->createExecutionContext());if(!context||!context->setTensorAddress("image",in)||!context->setTensorAddress("relative_depth",out))throw std::runtime_error("Depth smoke bindings failed");
 if(!context->enqueueV3(stream)||cuda.cudaStreamSynchronize_(stream)!=cudaSuccess){std::cerr<<"Possible preparation submission did not retire; generation unpublished\n";std::cerr.flush();std::_Exit(3);}
 std::vector<float> result(pixels);CheckCuda(cuda.cudaMemcpy_(result.data(),out,pixels*4,cudaMemcpyDeviceToHost));for(size_t i=0;i<pixels;++i){float oracle=0;std::memcpy(&oracle,expected.data()+i*4,4);if(!std::isfinite(result[i])||result[i]<0||!std::isfinite(oracle)||std::abs(result[i]-oracle)>.05f+.05f*std::abs(oracle))throw std::runtime_error("TensorRT FP16-tactic synthetic parity exceeded rtol/atol .05; cache refused");}
 CheckCuda(cuda.cudaStreamDestroy_(stream));CheckCuda(cuda.cudaFree_(in));CheckCuda(cuda.cudaFree_(out));
 std::ofstream engineFile(root/L"engine.plan",std::ios::binary);engineFile.write(static_cast<const char*>(plan->data()),std::streamsize(plan->size()));engineFile.close();if(!engineFile)throw std::runtime_error("Depth engine write failed");
 constexpr char hex[]="0123456789abcdef";std::string uuid;for(unsigned char b:properties.uuid.bytes){uuid+=hex[b>>4];uuid+=hex[b&15];}
 Json receipt={{"schema",1},{"builder","neurotic-dav2-local-v1"},{"checkpointSha256",Dav2Checkpoint},{"sourceRevision",Dav2Source},{"width",width},{"height",height},{"precision","fp32-io-fp16-tactics"},{"io","image->relative_depth"},{"parity","synthetic-fp32-reference-passed"},{"identity",{{"uuid",uuid},{"driver",driver},{"driverFileVersion",driverFileVersion},{"runtime",runtimeVersion},{"tensorRT",trtVersion},{"computeMajor",properties.major},{"computeMinor",properties.minor}}},{"files",Json::object()}};
 for(const char* name:{"model.onnx","engine.plan","NeuRotic.DepthAnything.dll","cudart64_12.dll","nvinfer_10.dll"})receipt["files"][name]=nrw::guidance::HashFile(assets.Hold(root/name));
 if(!ValidManifest(receipt,unsigned(width),unsigned(height)))throw std::runtime_error("Prepared cache manifest invalid");std::ofstream manifest(root/L"depth-cache.json",std::ios::binary);manifest<<receipt.dump(2);manifest.close();if(!manifest)throw std::runtime_error("Depth manifest write failed");std::cout<<receipt.dump(2)<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
