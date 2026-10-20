#include <ui/ClosingSession.h>
#include <fstream>
#include <iostream>
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)throw std::runtime_error("new owned fixture root required");const std::filesystem::path root=argv[1];if(std::filesystem::exists(root))throw std::runtime_error("fixture already exists");std::filesystem::create_directories(root);
 nh::UserDataSession authority(root);if(!authority.Owns())throw std::runtime_error("fixture root session unavailable");
 nh::ClosingSession closing;closing.Begin(root);std::array<bool,9> actual{};actual[8]=true;closing.Observe(actual);
 if(closing.Settled()||!closing.Pending()[8])throw std::runtime_error("NR file job must remain a real closing gate");
 actual[8]=false;closing.Observe(actual);if(!closing.Settled())throw std::runtime_error("closing must settle after actual NR job drain");
 std::array<bool,8> legacy{};legacy[nh::ClosingSession::Installer]=true;closing.Observe(legacy);if(closing.Settled())throw std::runtime_error("existing installer gate remains active");
 legacy.fill(false);closing.Observe(legacy);if(!closing.Settled())throw std::runtime_error("existing owners retain their drain behavior");
 std::ifstream f(root/L"last-session.log");const std::string log{std::istreambuf_iterator<char>(f),{}};
 if(log.find("nr_dll_files")==std::string::npos)throw std::runtime_error("NR file job must retain its own truthful diagnostic stage");
 std::cout<<"PASS NR DLL closing gate and original eight-owner compatibility; actual coordinator/file receipt, no graphics/provider\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
