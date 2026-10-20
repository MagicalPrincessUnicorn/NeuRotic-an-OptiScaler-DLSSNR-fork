#include "dlssnr/NativeFsr3D3D12.h"

namespace DlssNr::NativeFg {
// Prepared addons/helpers do not own the game's presentation scheduler.
bool LoadFsrApi(FsrApi& api, std::string& reason) {
    api = {};
    reason = "Standalone frame generation requires the in-game native Vulkan owner";
    return false;
}
}
