#include "../addons/prepared-guides/helper/PreparedHelperClient.h"
#include <cassert>
#include <iostream>
namespace H=Neurotic::Feed::Prepared::Helper;
int wmain(int argc,wchar_t** argv){
    H::Client client;H::ClientOptions options;std::string reason;
    assert(!client.Start(options,reason));assert(!client.Alive());
    if(argc!=2){std::cerr<<"Pass explicit freshly built helper path for IPC-only test\n";return 2;}
    options.executable=std::filesystem::absolute(argv[1]).wstring();
    options.model=L"C:\\NoModelFixture\\nvngx_dlssnr.dll";
    options.forwarder=L"C:\\NoModelFixture\\nvngx.dll_dlssnr.dll";
    options.adapterLuid=L"0:1";options.timeoutMs=5000;
    assert(client.Start(options,reason));
    H::Request cancel;cancel.flags=H::Cancel;cancel.guides.producer=1;cancel.guides.session=2;
    cancel.guides.stream=3;cancel.guides.device=4;cancel.guides.generation=5;cancel.guides.capture=6;
    H::Reply reply;std::vector<std::uint8_t> output;
    assert(!client.Process(cancel,{},reply,output,reason));
    assert(reply.result.state==Neurotic::Feed::Prepared::Ipc::Outcome::Cancelled);
    assert(reply.result.completionValue==0&&reply.result.facts==0&&output.empty());
    client.Stop();std::cout<<"Actual helper child IPC cancellation passed; no GPU/model initialization\n";
}
