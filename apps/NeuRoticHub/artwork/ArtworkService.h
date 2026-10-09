#pragma once
#include <memory>
struct ID3D11Device;
namespace nh {
struct Game;
struct ArtworkView {void* texture=nullptr;float width=0,height=0;explicit operator bool()const{return texture!=nullptr;}};
class ArtworkService {
 struct Impl;std::unique_ptr<Impl> impl;
public:
 ArtworkService();~ArtworkService();void Attach(ID3D11Device* device);void SetOnlineEnabled(bool enabled);ArtworkView Get(const Game& game,const char* role,bool online);void Poll();void Shutdown();
};
}
