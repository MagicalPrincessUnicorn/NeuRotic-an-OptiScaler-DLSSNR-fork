// GPL-3.0. Optional TensorRT 10 / CUDA 12 module; no mandatory worker imports.
#include "Dav2BackendApi.h"
#include "DriverIdentity.h"
#include <cuda_runtime_api.h>
#pragma warning(push)
#pragma warning(disable:4100) // Vendor virtual default implementations name unused parameters.
#include <NvInferRuntime.h>
#pragma warning(pop)
#include <memory>
#include <string>
#include <stdexcept>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <vector>
#include <initializer_list>
#include "../DepthBundle.h"
#if NV_TENSORRT_MAJOR != 10
#error Qualify TensorRT major migrations separately.
#endif
namespace nrw::depth {
namespace {
void Error(char* out,size_t n,const char* value)noexcept{if(out&&n)strncpy_s(out,n,value?value:"Depth backend failed",_TRUNCATE);}
struct Library {
 HMODULE value=nullptr;
 Library(const std::filesystem::path& path){value=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!value||!guidance::LoadedFrom(value,path))throw std::runtime_error("Selected depth runtime DLL could not be loaded");}
 ~Library(){if(value)FreeLibrary(value);}
 template<class T>T Symbol(const char* name){auto p=GetProcAddress(value,name);if(!p)throw std::runtime_error(std::string("Depth runtime symbol missing: ")+name);return reinterpret_cast<T>(p);}
};
struct Cuda {
 Library library;
#define DAV2_STRING_IMPL(name) #name
#define DAV2_STRING(name) DAV2_STRING_IMPL(name)
#define CU_FUNCTION(name) decltype(&name) name##_=library.Symbol<decltype(&name)>(DAV2_STRING(name))
 CU_FUNCTION(cudaGetDeviceCount);CU_FUNCTION(cudaGetDeviceProperties);CU_FUNCTION(cudaSetDevice);
 CU_FUNCTION(cudaStreamCreateWithFlags);CU_FUNCTION(cudaStreamDestroy);CU_FUNCTION(cudaEventCreateWithFlags);
 CU_FUNCTION(cudaEventRecord);CU_FUNCTION(cudaEventQuery);CU_FUNCTION(cudaEventDestroy);
 CU_FUNCTION(cudaImportExternalMemory);CU_FUNCTION(cudaExternalMemoryGetMappedBuffer);CU_FUNCTION(cudaDestroyExternalMemory);CU_FUNCTION(cudaFree);
 CU_FUNCTION(cudaImportExternalSemaphore);CU_FUNCTION(cudaWaitExternalSemaphoresAsync);CU_FUNCTION(cudaSignalExternalSemaphoresAsync);CU_FUNCTION(cudaDestroyExternalSemaphore);
 CU_FUNCTION(cudaDriverGetVersion);CU_FUNCTION(cudaRuntimeGetVersion);
#undef CU_FUNCTION
 explicit Cuda(const std::filesystem::path& root):library(root/L"cudart64_12.dll"){}
};
void Require(cudaError_t e){if(e!=cudaSuccess)throw std::runtime_error("CUDA depth operation failed ("+std::to_string(int(e))+")");}
struct Logger:nvinfer1::ILogger{void log(Severity severity,const char* message)noexcept override{if(severity<=Severity::kWARNING&&message)std::fprintf(stderr,"Depth TensorRT: %.1024s\n",message);}};
bool Shape(nvinfer1::Dims dims,std::initializer_list<int64_t> wanted){if(dims.nbDims!=int(wanted.size()))return false;int i=0;for(auto d:wanted)if(dims.d[i++]!=d)return false;return true;}
struct Session {
 Cuda cuda;Library trt;Logger logger;
 std::unique_ptr<nvinfer1::IRuntime> runtime;std::unique_ptr<nvinfer1::ICudaEngine> engine;std::unique_ptr<nvinfer1::IExecutionContext> context;
 cudaStream_t stream=nullptr;cudaEvent_t outer=nullptr;
 cudaExternalMemory_t inputMemory=nullptr,outputMemory=nullptr;void* input=nullptr;void* output=nullptr;
 cudaExternalSemaphore_t prep=nullptr,ready=nullptr;
 BackendPoll phase=BackendPoll::Idle;int device=-1;
 Session(const BackendCreate& c):cuda(c.runtimeRoot),trt(std::filesystem::path(c.runtimeRoot)/L"nvinfer_10.dll"){}
 void Setup(const BackendCreate& c){
  int count=0;Require(cuda.cudaGetDeviceCount_(&count));
  for(int i=0;i<count;++i){cudaDeviceProp properties{};Require(cuda.cudaGetDeviceProperties_(&properties,i));
   if(!std::memcmp(properties.luid,&c.adapter,sizeof(LUID))&&properties.luidDeviceNodeMask==1){
    if(std::memcmp(properties.uuid.bytes,c.identity.uuid,16)||properties.major!=c.identity.computeMajor||properties.minor!=c.identity.computeMinor)throw std::runtime_error("Depth engine cache GPU identity differs from actual capture device");device=i;break;}}
  if(device<0)throw std::runtime_error("No CUDA device matches the capture adapter LUID/node");
  Require(cuda.cudaSetDevice_(device));
  int driver=0,version=0;Require(cuda.cudaDriverGetVersion_(&driver));Require(cuda.cudaRuntimeGetVersion_(&version));
  using Version=int32_t (__cdecl *)();const auto tensorRT=trt.Symbol<Version>("getInferLibVersion")();
  if(driver!=c.identity.driver||version!=c.identity.runtime||tensorRT!=c.identity.tensorRT||!c.identity.driverFileVersion||CudaDriverFileVersion()!=c.identity.driverFileVersion)throw std::runtime_error("Depth cache runtime/driver identity changed; prepare a new local cache");
  // Any failure from here retains the session and its host allocations conservatively.
  phase=BackendPoll::Faulted;
  using Create=void* (__cdecl *)(void*,int32_t);
  runtime.reset(static_cast<nvinfer1::IRuntime*>(trt.Symbol<Create>("createInferRuntime_INTERNAL")(&logger,NV_TENSORRT_VERSION)));
  if(!runtime)throw std::runtime_error("TensorRT runtime creation failed");
  engine.reset(runtime->deserializeCudaEngine(c.engine,c.engineBytes));
  if(!engine||engine->getNbIOTensors()!=2)throw std::runtime_error("Invalid depth engine IO count");
  for(const char* name:{"image","relative_depth"}){
   if(engine->getTensorIOMode(name)!=(std::strcmp(name,"image")==0?nvinfer1::TensorIOMode::kINPUT:nvinfer1::TensorIOMode::kOUTPUT)||engine->getTensorDataType(name)!=nvinfer1::DataType::kFLOAT||engine->getTensorFormat(name)!=nvinfer1::TensorFormat::kLINEAR||engine->getTensorLocation(name)!=nvinfer1::TensorLocation::kDEVICE)throw std::runtime_error("Depth engine requires named FP32 LINEAR device IO");
  }
  if(!Shape(engine->getTensorShape("image"),{1,3,c.height,c.width})||!Shape(engine->getTensorShape("relative_depth"),{1,c.height,c.width}))throw std::runtime_error("Depth engine fixed shape differs from selected profile");
  context.reset(engine->createExecutionContext());if(!context)throw std::runtime_error("Depth execution context creation failed");
  Require(cuda.cudaStreamCreateWithFlags_(&stream,cudaStreamNonBlocking));Require(cuda.cudaEventCreateWithFlags_(&outer,cudaEventDisableTiming));
  Import(c.input,c.inputAllocation,c.inputBytes,inputMemory,input);Import(c.output,c.outputAllocation,c.outputBytes,outputMemory,output);
  Semaphore(c.preparedFence,prep);Semaphore(c.outputFence,ready);
  if(!context->setTensorAddress("image",input)||!context->setTensorAddress("relative_depth",output))throw std::runtime_error("Depth tensor binding failed");
  phase=BackendPoll::Idle;
 }
 void Import(HANDLE handle,uint64_t allocation,uint64_t bytes,cudaExternalMemory_t& memory,void*& pointer){
  cudaExternalMemoryHandleDesc desc{};desc.type=cudaExternalMemoryHandleTypeD3D12Resource;desc.handle.win32.handle=handle;desc.size=allocation;desc.flags=cudaExternalMemoryDedicated;
  Require(cuda.cudaImportExternalMemory_(&memory,&desc));cudaExternalMemoryBufferDesc buffer{};buffer.size=bytes;Require(cuda.cudaExternalMemoryGetMappedBuffer_(&pointer,memory,&buffer));
 }
 void Semaphore(HANDLE handle,cudaExternalSemaphore_t& value){cudaExternalSemaphoreHandleDesc desc{};desc.type=cudaExternalSemaphoreHandleTypeD3D12Fence;desc.handle.win32.handle=handle;Require(cuda.cudaImportExternalSemaphore_(&value,&desc));}
 ~Session(){
  // Only reached after the outer event (including output signal) has retired.
  if(device>=0)cuda.cudaSetDevice_(device);
  if(input)cuda.cudaFree_(input);if(output)cuda.cudaFree_(output);
  if(inputMemory)cuda.cudaDestroyExternalMemory_(inputMemory);if(outputMemory)cuda.cudaDestroyExternalMemory_(outputMemory);
  if(prep)cuda.cudaDestroyExternalSemaphore_(prep);if(ready)cuda.cudaDestroyExternalSemaphore_(ready);
  if(outer)cuda.cudaEventDestroy_(outer);if(stream)cuda.cudaStreamDestroy_(stream);
 }
};
void* __cdecl Create(const BackendCreate* c,char* error,size_t n)noexcept{
 try{if(!c||c->size!=sizeof(*c)||c->version!=BackendVersion||!c->runtimeRoot||!c->engine||!c->engineBytes||c->engineBytes>(512ull<<20)||c->width!=518||(c->height!=518&&c->height!=294)||!c->input||!c->output||!c->preparedFence||!c->outputFence||c->inputBytes!=uint64_t(c->width)*c->height*12||c->outputBytes!=uint64_t(c->width)*c->height*4||c->inputAllocation<c->inputBytes||c->outputAllocation<c->outputBytes)throw std::runtime_error("Invalid depth generation description");
  auto s=std::make_unique<Session>(*c);try{s->Setup(*c);}catch(const std::exception& e){Error(error,n,e.what());if(s->phase==BackendPoll::Faulted)return s.release();throw;}
  return s.release();
 }catch(const std::exception& e){Error(error,n,e.what());return nullptr;}catch(...){Error(error,n,"Depth generation creation failed");return nullptr;}
}
bool __cdecl Submit(void* p,uint64_t prepared,uint64_t output,char* error,size_t n)noexcept{
 if(!p||!prepared||!output){Error(error,n,"Invalid depth submission");return false;}auto& s=*static_cast<Session*>(p);
 if(s.phase!=BackendPoll::Idle){Error(error,n,"Depth inference slot busy");return false;}
 try{
  Require(s.cuda.cudaSetDevice_(s.device));
  // Mark possible submission before the first asynchronous API; a false return is not retirement.
  s.phase=BackendPoll::Faulted;
  cudaExternalSemaphoreWaitParams wait{};wait.params.fence.value=prepared;
  Require(s.cuda.cudaWaitExternalSemaphoresAsync_(&s.prep,&wait,1,s.stream));
  if(!s.context->enqueueV3(s.stream))throw std::runtime_error("TensorRT enqueue failed; possible work retained");
  cudaExternalSemaphoreSignalParams signal{};signal.params.fence.value=output;
  Require(s.cuda.cudaSignalExternalSemaphoresAsync_(&s.ready,&signal,1,s.stream));
  Require(s.cuda.cudaEventRecord_(s.outer,s.stream));s.phase=BackendPoll::Pending;return true;
 }catch(const std::exception& e){Error(error,n,e.what());return false;}catch(...){Error(error,n,"Depth submission faulted");return false;}
}
BackendPoll __cdecl Poll(void* p,char* error,size_t n)noexcept{
 if(!p)return BackendPoll::Faulted;auto& s=*static_cast<Session*>(p);if(s.phase!=BackendPoll::Pending)return s.phase;
 if(s.cuda.cudaSetDevice_(s.device)!=cudaSuccess){Error(error,n,"Depth CUDA device selection failed");return s.phase=BackendPoll::Faulted;}
 auto result=s.cuda.cudaEventQuery_(s.outer);if(result==cudaErrorNotReady)return BackendPoll::Pending;
 if(result!=cudaSuccess){Error(error,n,"Depth output signal completion unknown");return s.phase=BackendPoll::Faulted;}
 s.phase=BackendPoll::Idle;return BackendPoll::Complete;
}
bool __cdecl Close(void* p)noexcept{if(!p)return true;auto* s=static_cast<Session*>(p);if(s->phase!=BackendPoll::Idle)return false;delete s;return true;}
const BackendApi api{sizeof(BackendApi),BackendVersion,Create,Submit,Poll,Close};
}
}
extern "C" __declspec(dllexport) const nrw::depth::BackendApi* __cdecl NrDav2GetApi(uint32_t version)noexcept{return version==nrw::depth::BackendVersion?&nrw::depth::api:nullptr;}
