#include "PreparedGpuClient.h"
#include "PreparedGpuIo.h"
#include <bcrypt.h>
#include <sddl.h>
#include <sstream>
#include <atomic>
namespace nrpg::gpu {
namespace {std::atomic_bool ActiveSession=false;}
struct PreparedGpuClient::Impl {
    ManagedHostSpec spec;PeerPolicy peer;io::Handle pipe,process;Microsoft::WRL::ComPtr<ID3D12Fence> producer,output;
    T::FrameTicket latest{};std::unique_ptr<T::TicketSequence> sequence;bool started=false,uncertain=false,leased=false,ownsSession=false;unsigned registrations=0;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,14> retained;std::array<std::uint32_t,14> ids{};
    bool Fail(std::string&r,const char*s,bool quarantine=false){uncertain|=quarantine;return io::Fail(r,s);}
    bool Exchange(const Bytes&request,Bytes&reply,std::string&r){return (io::Write(pipe.value,request,spec.timeoutMs)&&io::Read(pipe.value,reply,spec.timeoutMs))||Fail(r,"NRH2 control connection failed",latest.identity.capture!=0);}
};
PreparedGpuClient::PreparedGpuClient():p_(std::make_unique<Impl>()){}
PreparedGpuClient::~PreparedGpuClient(){if(p_->uncertain||p_->leased)(void)p_.release();else if(p_->ownsSession)ActiveSession=false;}
ID3D12Fence* PreparedGpuClient::ProducerFence()const{return p_->producer.Get();}
ID3D12Fence* PreparedGpuClient::OutputFence()const{return p_->output.Get();}
Nonce PreparedGpuClient::SessionNonce()const{return p_->peer.nonce;}
bool PreparedGpuClient::RequiresRestart()const{return p_->uncertain;}
bool PreparedGpuClient::Start(const ManagedHostSpec&spec,const PeerPolicy&policy,std::string&r){auto&p=*p_;
    if(p.started||p.uncertain)return p.Fail(r,"NRH2 client generation cannot be restarted");p.started=true;
    bool expected=false;if(!ActiveSession.compare_exchange_strong(expected,true))return p.Fail(r,"NRH2 one active or quarantined session per process");p.ownsSession=true;
    DWORD binary=0;if(!spec.executable.is_absolute()||!spec.coreModule.is_absolute()||spec.executable.parent_path()!=spec.coreModule.parent_path()||!GetBinaryTypeW(spec.executable.c_str(),&binary)||binary!=SCS_64BIT_BINARY||spec.consumerMode!=1||!spec.timeoutMs||spec.timeoutMs>30000||!policy.device||!policy.adapterLuid||!T::detail::Value(policy.episode.producer)||!policy.episode.session||!policy.episode.stream||!policy.episode.device||!policy.episode.generation)return p.Fail(r,"NRH2 managed host paths/architecture/policy refused");
    auto actual=policy.device->GetAdapterLuid();std::uint64_t luid=0;std::memcpy(&luid,&actual,8);if(luid!=policy.adapterLuid)return p.Fail(r,"NRH2 producer adapter mismatch");
    p.spec=spec;p.peer=policy;if(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(p.peer.nonce.data()),16,BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)return p.Fail(r,"NRH2 bootstrap nonce unavailable");
    std::wostringstream name;name<<L"\\\\.\\pipe\\NeuRotic.NRH2."<<GetCurrentProcessId()<<L'.'<<std::hex<<p.peer.nonce[0]<<p.peer.nonce[1];
    io::Handle token;DWORD bytes=0;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.value))return p.Fail(r,"NRH2 owner token unavailable");GetTokenInformation(token.value,TokenUser,nullptr,0,&bytes);Bytes user(bytes);if(!GetTokenInformation(token.value,TokenUser,user.data(),bytes,&bytes))return p.Fail(r,"NRH2 owner identity unavailable");LPWSTR sid=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid,&sid))return p.Fail(r,"NRH2 owner SID unavailable");std::wstring acl=L"D:P(A;;GA;;;";acl+=sid;acl+=L")";LocalFree(sid);PSECURITY_DESCRIPTOR sd=nullptr;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,&sd,nullptr))return p.Fail(r,"NRH2 restricted ACL unavailable");SECURITY_ATTRIBUTES sa{sizeof(sa),sd,FALSE};
    p.pipe.value=CreateNamedPipeW(name.str().c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,MaxBytes,MaxBytes,spec.timeoutMs,&sa);LocalFree(sd);if(p.pipe.value==INVALID_HANDLE_VALUE)return p.Fail(r,"NRH2 private control endpoint unavailable");
    io::Handle bootRead,bootWrite;SECURITY_ATTRIBUTES inherit{sizeof(inherit),nullptr,TRUE};if(!CreatePipe(&bootRead.value,&bootWrite.value,&inherit,static_cast<DWORD>(io::BootstrapBytes))||!SetHandleInformation(bootWrite.value,HANDLE_FLAG_INHERIT,0))return p.Fail(r,"NRH2 bootstrap pipe unavailable");
    SIZE_T attributeBytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&attributeBytes);Bytes attributes(attributeBytes);auto*attrs=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());if(!InitializeProcThreadAttributeList(attrs,1,0,&attributeBytes))return p.Fail(r,"NRH2 private inheritance unavailable");
    struct AttributeCleanup{LPPROC_THREAD_ATTRIBUTE_LIST p;~AttributeCleanup(){DeleteProcThreadAttributeList(p);}}cleanup{attrs};
    HANDLE inherited[]{bootRead.value};if(!UpdateProcThreadAttribute(attrs,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr))return p.Fail(r,"NRH2 bootstrap inheritance refused");
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attrs;PROCESS_INFORMATION child{};std::wstring command=L"\""+spec.executable.wstring()+L"\" --gpu-bootstrap "+std::to_wstring(reinterpret_cast<std::uintptr_t>(bootRead.value));
    if(!CreateProcessW(spec.executable.c_str(),command.data(),nullptr,nullptr,TRUE,EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW,nullptr,spec.executable.parent_path().c_str(),&startup.StartupInfo,&child))return p.Fail(r,"NRH2 managed x64 child launch failed");p.process.value=child.hProcess;CloseHandle(child.hThread);CloseHandle(bootRead.value);bootRead.value=nullptr;
    const auto childCreation=io::Creation(p.process.value);if(!childCreation)return p.Fail(r,"NRH2 launched process identity unavailable");
    Hello hello;hello.nonce=p.peer.nonce;hello.adapterLuid=policy.adapterLuid;hello.processId=GetCurrentProcessId();hello.processBits=sizeof(void*)*8;hello.buildIdentity=spec.buildIdentity;
    Bytes boot(io::BootstrapBytes);auto encoded=Encode(hello);std::copy(encoded.begin(),encoded.end(),boot.begin());Put(boot,80,io::Creation(GetCurrentProcess()));Prefix(boot,88,policy.episode);Put(boot,128,spec.timeoutMs,4);
    if(!io::PutText(boot,136,128,name.str())||!io::PutText(boot,392,512,spec.coreModule.wstring()))return p.Fail(r,"NRH2 bootstrap path too long");DWORD written=0;if(!WriteFile(bootWrite.value,boot.data(),static_cast<DWORD>(boot.size()),&written,nullptr)||written!=boot.size())return p.Fail(r,"NRH2 bootstrap delivery failed");CloseHandle(bootWrite.value);bootWrite.value=nullptr;
    io::Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED connect{};connect.hEvent=event.value;BOOL connected=ConnectNamedPipe(p.pipe.value,&connect);auto error=connected?ERROR_SUCCESS:GetLastError();if(!connected&&error==ERROR_IO_PENDING){DWORD n=0;if(WaitForSingleObject(event.value,spec.timeoutMs)!=WAIT_OBJECT_0){CancelIoEx(p.pipe.value,&connect);GetOverlappedResult(p.pipe.value,&connect,&n,TRUE);return p.Fail(r,"NRH2 child connect timeout");}connected=GetOverlappedResult(p.pipe.value,&connect,&n,FALSE);}else connected=connected||error==ERROR_PIPE_CONNECTED;
    ULONG pid=0;if(!connected||!GetNamedPipeClientProcessId(p.pipe.value,&pid)||pid!=child.dwProcessId||io::Creation(p.process.value)!=childCreation)return p.Fail(r,"NRH2 actual child PID/creation mismatch");
    Bytes reply;Hello ack;if(!p.Exchange(Encode(hello),reply,r)||!Decode(reply,ack,Kind::HelloAck)||ack.nonce!=hello.nonce||ack.adapterLuid!=hello.adapterLuid||ack.processId!=pid||ack.processBits!=64||ack.consumerMode!=1||ack.buildIdentity!=spec.buildIdentity||(ack.capabilities&(SharedTextures|SharedFences))!=(SharedTextures|SharedFences))return p.Fail(r,"NRH2 authenticated hello acknowledgement refused");
    for(unsigned i=0;i<2;++i){GpuRegistration reg;if(!io::Read(p.pipe.value,reply,spec.timeoutMs)||!Decode(reply,reg)||reg.nonce!=p.peer.nonce||!T::detail::Prefix(reg.identity,policy.episode)||reg.id!=i+1||reg.role!=static_cast<Role>(6+i)||reg.kind!=ObjectKind::Fence||reg.direction!=Direction::HostToProducer||reg.access!=GENERIC_ALL||reg.handle>UINTPTR_MAX)return p.Fail(r,"NRH2 host fence registration refused");auto&f=i?p.output:p.producer;HANDLE h=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(reg.handle));if(FAILED(policy.device->OpenSharedHandle(h,IID_PPV_ARGS(&f))))return p.Fail(r,"NRH2 local shared fence import refused");CloseHandle(h);}
    p.sequence=std::make_unique<T::TicketSequence>(policy.episode,p.peer.nonce);return true;
}
ID3D12Resource* PreparedGpuClient::Resource(std::uint32_t id)const{for(unsigned i=0;i<p_->registrations;++i)if(p_->ids[i]==id)return p_->retained[i].Get();return nullptr;}
bool PreparedGpuClient::Register(const GpuRegistration&source,std::string&r){auto&p=*p_;
    if(!p.sequence||p.latest.identity.capture||p.uncertain||p.registrations>=14||source.handle>UINTPTR_MAX)return p.Fail(r,"NRH2 registration phase/limit refused");
    auto reg=source;reg.nonce=p.peer.nonce;reg.identity=p.peer.episode;GpuRegistration checked;
    if(!Decode(Encode(reg),checked)||reg.kind!=ObjectKind::Texture||static_cast<unsigned>(reg.role)>5)return p.Fail(r,"NRH2 local texture registration invalid");
    Microsoft::WRL::ComPtr<ID3D12Resource> retained;
    if(reg.direction==Direction::ProducerToHost){
        HANDLE local=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(source.handle));
        if(FAILED(p.peer.device->OpenSharedHandle(local,IID_PPV_ARGS(&retained))))return p.Fail(r,"NRH2 local NT texture retention failed");
        HANDLE remote=nullptr;if(!DuplicateHandle(GetCurrentProcess(),local,p.process.value,&remote,0,FALSE,DUPLICATE_SAME_ACCESS))return p.Fail(r,"NRH2 target texture duplication failed");reg.handle=reinterpret_cast<std::uintptr_t>(remote);
    }else if(reg.handle)return p.Fail(r,"NRH2 host allocation request requires zero handle");
    Bytes reply;auto message=Encode(reg);if(!p.Exchange(message,reply,r)||!Decode(reply,checked))return p.Fail(r,"NRH2 registration acknowledgement refused");
    if(reg.direction==Direction::HostToProducer){
        if(!checked.handle||checked.handle>UINTPTR_MAX)return p.Fail(r,"NRH2 returned local NT handle width refused");
        reg.handle=checked.handle;if(Encode(reg)!=reply)return p.Fail(r,"NRH2 returned registration identity/shape mismatch");
        HANDLE local=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(checked.handle));if(FAILED(p.peer.device->OpenSharedHandle(local,IID_PPV_ARGS(&retained))))return p.Fail(r,"NRH2 host texture local import refused");CloseHandle(local);
        const auto d=retained->GetDesc();if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.Width!=reg.width||d.Height!=reg.height||d.Format!=static_cast<DXGI_FORMAT>(reg.format)||d.MipLevels!=1||d.DepthOrArraySize!=1||d.SampleDesc.Count!=1)return p.Fail(r,"NRH2 host-created texture shape mismatch");
    }else if(message!=reply)return p.Fail(r,"NRH2 imported registration acknowledgement mismatch");
    p.ids[p.registrations]=reg.id;p.retained[p.registrations++]=retained;return true;
}
bool PreparedGpuClient::Process(const GpuFrameRequest&frame,GpuFrameResult&result,std::string&r){auto&p=*p_;if(!p.sequence||p.uncertain||p.leased||frame.ticket.nonce!=p.peer.nonce||!Matches(frame)||!p.sequence->Accept(frame.ticket))return p.Fail(r,"NRH2 frame identity/replay/lease refused");p.latest=frame.ticket;p.uncertain=true;Bytes reply;if(!p.Exchange(Encode(frame),reply,r)||!Decode(reply,result)||result.ticket!=frame.ticket)return p.Fail(r,"NRH2 result identity/completion unknown",true);
    if(result.facts&P::Ipc::OutputValid){if(!io::Wait(p.output.Get(),result.completedValue,p.spec.timeoutMs))return p.Fail(r,"NRH2 output fence not completed",true);p.leased=true;}p.uncertain=false;return true;}
bool PreparedGpuClient::Release(const CopybackReceipt&receipt,std::string&r){auto&p=*p_;if(p.uncertain||!p.leased||receipt.ticket!=p.latest||!receipt.restored||!io::SameDevice(p.peer.device.Get(),receipt.completion.Get())||!io::Wait(receipt.completion.Get(),receipt.value,p.spec.timeoutMs))return p.Fail(r,"NRH2 local copyback completion/state proof refused");auto message=Encode(Command{p.latest,3},Kind::Release);Bytes reply;if(!p.Exchange(message,reply,r)||reply!=message)return p.Fail(r,"NRH2 release acknowledgement unknown",true);p.leased=false;return true;}
bool PreparedGpuClient::Drain(std::string&r){auto&p=*p_;if(p.uncertain||p.leased)return p.Fail(r,"NRH2 drain refuses leased/uncertain generation");if(!p.latest.identity.capture)return true;auto message=Encode(Command{p.latest,0},Kind::Drain);Bytes reply;return (p.Exchange(message,reply,r)&&reply==message)||p.Fail(r,"NRH2 drain acknowledgement failed",true);}
}
