#include "../../../apps/NeuRoticHub/anything/AnythingController.h"
#include <chrono>
#include <thread>
#include <iostream>
using namespace nh;
template<class F>bool Wait(F f){for(unsigned i=0;i<600;++i){if(f())return true;std::this_thread::sleep_for(std::chrono::milliseconds(10));}return false;}
int main(){try{
 const auto root=std::filesystem::current_path()/L"builds/depth-anything-v2/app";std::filesystem::create_directories(root);
 AnythingController controller(root/L"DepthProtocolFixture.exe",root/L"state");controller.Connect();if(!Wait([&]{return controller.Snapshot().connected;}))throw std::runtime_error("fixture connection");
 nlohmann::json request={{"mode","countdown"},{"seconds",1},{"guides","off"},{"depthProvider","dav2"},{"depthProfile","reference"},{"depthHz",30},{"depthPreview",true}};
 if(controller.Start(request))throw std::runtime_error("unready depth preview must refuse even with an older depth-capable worker");
 if(controller.Snapshot().active||controller.Snapshot().ready)throw std::runtime_error("depth request bypassed private-model lock");
 AnythingController old(root/L"OldDepthProtocolFixture.exe",root/L"old-state");old.Connect();if(!Wait([&]{return old.Snapshot().connected;}))throw std::runtime_error("old fixture connection");
 if(old.Start(request))throw std::runtime_error("old worker must refuse unsupported preview");
 std::cout<<"PASS retired depth requests refuse for both old worker variants\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL App depth: "<<e.what()<<'\n';return 1;}}
