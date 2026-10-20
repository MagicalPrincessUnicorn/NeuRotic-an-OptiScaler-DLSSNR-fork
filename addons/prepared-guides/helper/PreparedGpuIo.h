#pragma once
#include "PreparedGpuClient.h"
#include <windows.h>
#include <limits>
namespace nrpg::gpu::io {
struct Handle {HANDLE value=nullptr;Handle()=default;explicit Handle(HANDLE h):value(h){};~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}Handle(const Handle&)=delete;Handle&operator=(const Handle&)=delete;};
inline bool Fail(std::string&r,const char*s){r=s;return false;}
inline std::uint64_t Creation(HANDLE process){FILETIME c{},e{},k{},u{};if(!GetProcessTimes(process,&c,&e,&k,&u))return 0;return (std::uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;}
inline bool Transfer(HANDLE pipe,void* data,DWORD size,bool writing,DWORD timeout){
    auto*p=static_cast<unsigned char*>(data);const auto end=GetTickCount64()+timeout;
    while(size){Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!event.value)return false;OVERLAPPED o{};o.hEvent=event.value;DWORD done=0;
        BOOL ok=writing?WriteFile(pipe,p,size,&done,&o):ReadFile(pipe,p,size,&done,&o);
        if(!ok){if(GetLastError()!=ERROR_IO_PENDING)return false;auto now=GetTickCount64();auto wait=now<end?static_cast<DWORD>(end-now):0;
            if(WaitForSingleObject(event.value,wait)!=WAIT_OBJECT_0){CancelIoEx(pipe,&o);GetOverlappedResult(pipe,&o,&done,TRUE);return false;}
            if(!GetOverlappedResult(pipe,&o,&done,FALSE))return false;}
        if(!done)return false;p+=done;size-=done;
    }return true;
}
inline bool Write(HANDLE pipe,const Bytes& b,DWORD timeout){return Transfer(pipe,const_cast<std::uint8_t*>(b.data()),static_cast<DWORD>(b.size()),true,timeout);}
inline bool Read(HANDLE pipe,Bytes& b,DWORD timeout){b.assign(16,0);if(!Transfer(pipe,b.data(),16,false,timeout))return false;Kind k{};if(!Header(b,k))return false;const auto n=PayloadSize(k);b.resize(16+n);return Transfer(pipe,b.data()+16,static_cast<DWORD>(n),false,timeout);}
inline bool Wait(ID3D12Fence*f,std::uint64_t value,DWORD timeout){if(!f||!T::detail::Value(value))return false;auto n=f->GetCompletedValue();if(n==UINT64_MAX)return false;if(n>=value)return true;Handle event(CreateEventW(nullptr,FALSE,FALSE,nullptr));if(!event.value||FAILED(f->SetEventOnCompletion(value,event.value))||WaitForSingleObject(event.value,timeout)!=WAIT_OBJECT_0)return false;n=f->GetCompletedValue();return n!=UINT64_MAX&&n>=value;}
inline bool SameDevice(ID3D12Device*d,ID3D12DeviceChild*c){Microsoft::WRL::ComPtr<ID3D12Device> owner;Microsoft::WRL::ComPtr<IUnknown>a,b;return c&&SUCCEEDED(c->GetDevice(IID_PPV_ARGS(&owner)))&&SUCCEEDED(d->QueryInterface(IID_PPV_ARGS(&a)))&&SUCCEEDED(owner.As(&b))&&a.Get()==b.Get();}
inline void Barrier(ID3D12GraphicsCommandList*l,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&x);}
constexpr std::size_t BootstrapBytes=2048;
inline bool PutText(std::span<std::uint8_t>b,std::size_t o,std::size_t count,const std::wstring&s){if(s.size()>=count)return false;for(std::size_t i=0;i<s.size();++i)Put(b,o+i*2,s[i],2);return true;}
inline std::wstring Text(std::span<const std::uint8_t>b,std::size_t o,std::size_t count){std::wstring s;for(std::size_t i=0;i<count;++i){auto c=Get(b,o+i*2,2);if(!c)return s;s.push_back(static_cast<wchar_t>(c));}return {};}
}
