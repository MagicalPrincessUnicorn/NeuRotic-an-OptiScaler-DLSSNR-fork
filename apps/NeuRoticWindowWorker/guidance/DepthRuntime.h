// GPL-3.0. Adapted from IndependentDepthRuntime.h; see guidance provenance.
#pragma once
#include <windows.h>
#include <wrl/client.h>
#pragma push_macro("OPTIONAL")
#include "vendor/dml_provider_factory.h"
#pragma pop_macro("OPTIONAL")

#include "DepthBundle.h"
#include <filesystem>
#include <string>
#include <vector>
#include <array>
#include <cmath>

namespace nrw::guidance
{
// Explicit-device DirectML session. Host tensors intentionally isolate this
// first preview port from vendor ownership of game/derived GPU resources.
// Initialization and synchronous Run must only occur on the provider worker.
class DepthRuntime
{
    HMODULE ortModule_=nullptr,dmlModule_=nullptr,sharedModule_=nullptr;
    const OrtApi* api_=nullptr;
    OrtEnv* environment_=nullptr;
    OrtSession* session_=nullptr;
    OrtMemoryInfo* memory_=nullptr;
    bool initialized_=false;
    Microsoft::WRL::ComPtr<IDMLDevice> dml_;
    std::string error_;
    bool Check(OrtStatus* status)
    {
        if(!status)return true;
        error_=api_->GetErrorMessage(status);api_->ReleaseStatus(status);return false;
    }
    bool Shape(bool input,const std::vector<int64_t>& expected)
    {
        size_t count=0;
        if(!Check(input?api_->SessionGetInputCount(session_,&count):api_->SessionGetOutputCount(session_,&count)) || count!=1)return false;
        OrtTypeInfo* type=nullptr;
        if(!Check(input?api_->SessionGetInputTypeInfo(session_,0,&type):api_->SessionGetOutputTypeInfo(session_,0,&type)))return false;
        const OrtTensorTypeAndShapeInfo* info=nullptr;
        size_t dimensions=0;ONNXTensorElementDataType element{};std::vector<int64_t> shape(expected.size());
        bool ok=Check(api_->CastTypeInfoToTensorInfo(type,&info)) && info &&
            Check(api_->GetTensorElementType(info,&element)) && element==ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT &&
            Check(api_->GetDimensionsCount(info,&dimensions)) && dimensions==expected.size() &&
            Check(api_->GetDimensions(info,shape.data(),shape.size())) && shape==expected;
        api_->ReleaseTypeInfo(type);return ok;
    }
public:
    DepthRuntime()=default;
    DepthRuntime(const DepthRuntime&)=delete;
    DepthRuntime& operator=(const DepthRuntime&)=delete;
    ~DepthRuntime()
    {
        if(api_){if(session_)api_->ReleaseSession(session_);if(memory_)api_->ReleaseMemoryInfo(memory_);if(environment_)api_->ReleaseEnv(environment_);}
        dml_.Reset();
        if(ortModule_)FreeLibrary(ortModule_);
        if(dmlModule_)FreeLibrary(dmlModule_);
        if(sharedModule_)FreeLibrary(sharedModule_);
    }
    const std::string& Error()const{return error_;}
    bool Initialize(ID3D12Device* device,ID3D12CommandQueue* queue,const std::filesystem::path& runtime,
        const std::filesystem::path& model,const std::filesystem::path& profile)
    {
        if(ortModule_ || !device || !queue || !runtime.is_absolute() || !model.is_absolute() || (!profile.empty() && !profile.is_absolute()))
        {error_="Invalid/repeated DirectML initialization";return false;}
        Microsoft::WRL::ComPtr<ID3D12Device> queueDevice;
        const auto kind=queue->GetDesc().Type;
        if((kind!=D3D12_COMMAND_LIST_TYPE_DIRECT && kind!=D3D12_COMMAND_LIST_TYPE_COMPUTE) ||
            FAILED(queue->GetDevice(IID_PPV_ARGS(&queueDevice))) || device!=queueDevice.Get())
        {error_="DirectML device and command queue differ";return false;}
        // Absolute paths and restricted dependency search; no working-directory
        // or game-directory DLL lookup and no process-global search-path change.
        constexpr DWORD search=LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32;
        sharedModule_=LoadLibraryExW((runtime/L"onnxruntime_providers_shared.dll").c_str(),nullptr,search);
        dmlModule_=LoadLibraryExW((runtime/L"DirectML.dll").c_str(),nullptr,search);
        ortModule_=LoadLibraryExW((runtime/L"onnxruntime.dll").c_str(),nullptr,search);
        if(!sharedModule_ || !dmlModule_ || !ortModule_ || !LoadedFrom(sharedModule_,runtime/L"onnxruntime_providers_shared.dll") || !LoadedFrom(dmlModule_,runtime/L"DirectML.dll") || !LoadedFrom(ortModule_,runtime/L"onnxruntime.dll"))
        {error_="Requested DirectML/ONNX runtime files are unavailable or a different module is already loaded";return false;}
        auto create=reinterpret_cast<decltype(&DMLCreateDevice)>(GetProcAddress(dmlModule_,"DMLCreateDevice"));
        auto getApi=reinterpret_cast<decltype(&OrtGetApiBase)>(GetProcAddress(ortModule_,"OrtGetApiBase"));
        if(!create || !getApi || !(api_=getApi()->GetApi(ORT_API_VERSION)) ||
            FAILED(create(device,DML_CREATE_DEVICE_FLAG_NONE,IID_PPV_ARGS(&dml_))))
        {error_="Explicit-device DirectML or pinned ORT API is unavailable";return false;}
        const OrtDmlApi* provider=nullptr;
        if(!Check(api_->GetExecutionProviderApi("DML",ORT_API_VERSION,reinterpret_cast<const void**>(&provider))) || !provider)return false;
        OrtSessionOptions* options=nullptr;
        if(!Check(api_->CreateSessionOptions(&options)))return false;
        struct Release {const OrtApi* api;OrtSessionOptions* value;~Release(){api->ReleaseSessionOptions(value);}} release{api_,options};
        if(!Check(api_->CreateEnv(ORT_LOGGING_LEVEL_WARNING,"NeuRoticWindowDepth",&environment_)) ||
            !Check(api_->DisableMemPattern(options)) || !Check(api_->SetSessionExecutionMode(options,ORT_SEQUENTIAL)) ||
            !Check(api_->AddSessionConfigEntry(options,"session.disable_cpu_ep_fallback","1")) ||
            (!profile.empty() && !Check(api_->EnableProfiling(options,profile.c_str()))) ||
            !Check(provider->SessionOptionsAppendExecutionProvider_DML1(options,dml_.Get(),queue)) ||
            !Check(api_->CreateSession(environment_,model.c_str(),options,&session_)) ||
            !Check(api_->CreateCpuMemoryInfo(OrtArenaAllocator,OrtMemTypeDefault,&memory_)))return false;
        if(!Shape(true,{1,3,294,518}) || !Shape(false,{1,294,518}))
        {error_="Depth model does not match the fixed FP32 profile";return false;}
        initialized_=true;error_.clear();return true;
    }
    bool Run(std::vector<float>& input,std::vector<float>& output)
    {
        output.clear();
        if(!initialized_ || !session_ || input.size()!=3*294*518){if(error_.empty())error_="Depth input/session is unavailable";return false;}
        OrtValue* tensor=nullptr;OrtValue* result=nullptr;
        const std::array<int64_t,4> shape{1,3,294,518};
        if(!Check(api_->CreateTensorWithDataAsOrtValue(memory_,input.data(),input.size()*sizeof(float),shape.data(),shape.size(),ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,&tensor)))return false;
        const char* inputName="rgb";const char* outputName="relative_depth";
        const OrtValue* inputs[]{tensor};
        const bool ran=Check(api_->Run(session_,nullptr,&inputName,inputs,1,&outputName,1,&result));
        api_->ReleaseValue(tensor);
        if(!ran){if(result)api_->ReleaseValue(result);return false;}
        struct Release {const OrtApi* api;OrtValue* value;~Release(){api->ReleaseValue(value);}} release{api_,result};
        float* values=nullptr;
        if(!Check(api_->GetTensorMutableData(result,reinterpret_cast<void**>(&values))) || !values)return false;
        output.assign(values,values+294*518);
        // Relative inverse depth: zero is valid. Never label these DeviceZ.
        for(float value:output)if(!std::isfinite(value) || value<0){output.clear();error_="Depth result contains invalid relative values";return false;}
        return true;
    }
};
}

