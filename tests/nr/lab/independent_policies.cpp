#include "../../../tools/nr/lab/OwnedCompletion.h"
#include "../../../tools/nr/lab/OwnedD3D12.h"
#include <iostream>
#include <stdexcept>
int main() {
    using lab::HealthyCompletion;
    auto expect=[](bool ok){if(!ok)throw std::runtime_error("completion policy regression");};
    expect(HealthyCompletion(1,1,true,0));
    expect(HealthyCompletion(1,2,true,0));
    expect(!HealthyCompletion(1,UINT64_MAX,true,0));
    expect(!HealthyCompletion(1,0,true,0));
    expect(!HealthyCompletion(1,1,false,0));
    expect(!HealthyCompletion(1,1,true,static_cast<int32_t>(0x887a0006u)));
    expect(!HealthyCompletion(0,1,true,0));
    expect(!HealthyCompletion(UINT64_MAX,UINT64_MAX,true,0));
    auto output=std::filesystem::temp_directory_path()/L"neurotic-physical-must-not-be-created";
    expect(!std::filesystem::exists(output));
    const auto blocked=lab::RunOwnedD3D12({{"adapter","physical"},{"adapter_luid","00000000:00000001"}},output,false);
    expect(blocked["success"]==false&&blocked["reason"]=="THIS_EDITION_IS_CPU_WARP_ONLY");
    expect(!std::filesystem::exists(output));
    std::cout << "PASS completion: 8 boundary checks; physical adapter rejected before effects\n";
}
