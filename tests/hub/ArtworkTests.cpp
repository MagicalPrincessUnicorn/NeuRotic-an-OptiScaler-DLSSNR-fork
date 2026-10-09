#include "artwork/ArtworkPolicy.h"
#include <windows.h>
#include <objbase.h>
#include <d3d11.h>
#include <wrl/client.h>
#include "artwork/ArtworkService.h"
#include "ui/HubViewModel.h"
#include <iostream>
#include <fstream>
#include <wincodec.h>
#include <iterator>
namespace {
bool SafeFixturePath(const std::filesystem::path& path){for(auto p=path;!p.empty();){auto attrs=GetFileAttributesW(p.c_str());if(attrs!=INVALID_FILE_ATTRIBUTES&&(attrs&FILE_ATTRIBUTE_REPARSE_POINT))return false;auto parent=p.parent_path();if(parent==p)break;p=parent;}return path.is_absolute();}
bool WriteArtworkFixture(const std::filesystem::path& path,unsigned width,unsigned height){
 using Microsoft::WRL::ComPtr;std::error_code error;if(!SafeFixturePath(path))return false;std::filesystem::create_directories(path.parent_path(),error);if(error)return false;
 ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;
 if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))||FAILED(factory->CreateStream(&stream))||FAILED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE))||FAILED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))||FAILED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache))||FAILED(encoder->CreateNewFrame(&frame,nullptr))||FAILED(frame->Initialize(nullptr))||FAILED(frame->SetSize(width,height)))return false;
 auto format=GUID_WICPixelFormat32bppBGRA;if(FAILED(frame->SetPixelFormat(&format))||format!=GUID_WICPixelFormat32bppBGRA)return false;
 std::vector<uint8_t> pixels((size_t)width*height*4,255);return SUCCEEDED(frame->WritePixels(height,width*4,(UINT)pixels.size(),pixels.data()))&&SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit());
}
bool WriteIconFixture(const std::filesystem::path& path){auto png=path;png.replace_extension(L"png");if(!WriteArtworkFixture(png,24,24)||!SafeFixturePath(path))return false;std::ifstream source(png,std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(source)),{}),ico(22,0);source.close();std::error_code error;std::filesystem::remove(png,error);if(error||bytes.empty())return false;ico[2]=ico[4]=1;ico[6]=ico[7]=24;ico[10]=1;ico[12]=32;ico[18]=22;for(unsigned i=0;i<4;i++)ico[14+i]=(uint8_t)(bytes.size()>>(i*8));ico.insert(ico.end(),bytes.begin(),bytes.end());std::ofstream file(path,std::ios::binary);file.write((const char*)ico.data(),ico.size());return (bool)file;}
nh::ArtworkView WaitArtwork(nh::ArtworkService& service,const nh::Game& game,const char* role,bool online=false){auto start=GetTickCount64();nh::ArtworkView image;while(GetTickCount64()-start<5000){service.Poll();image=service.Get(game,role,online);if(image)break;Sleep(5);}return image;}
void RemoveArtworkFixture(const std::filesystem::path& fixture){std::error_code error;if(fixture.parent_path()!=nh::UserRoot()||fixture.filename()!=L"artwork-local-fixture"||!SafeFixturePath(fixture))return;for(std::filesystem::recursive_directory_iterator it(fixture,error),end;!error&&it!=end;it.increment(error))if(!SafeFixturePath(it->path()))return;if(!error)std::filesystem::remove_all(fixture,error);}
}
int RunArtworkTests(){
 int failed=0;auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';if(!ok)++failed;};CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 check(nh::AllowedArtworkUrl(L"https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/42/library_hero.jpg"),"NH-ART-HOST: Steam HTTPS asset accepted");
 for(auto url:{L"http://shared.fastly.steamstatic.com/a.png",L"https://shared.fastly.steamstatic.com.evil.test/a.png",L"https://user:pass@shared.fastly.steamstatic.com/a.png",L"https://shared.fastly.steamstatic.com:8443/a.png",L"file:///C:/private.png"})check(!nh::AllowedArtworkUrl(url),"NH-ART-HOST: unsupported protocol/host/credentials/port refused");
 check(!nh::DecodeArtwork(std::vector<uint8_t>(8388609,0),"hero"),"NH-ART-BOUND: oversized compressed payload refused");
 check(!nh::DecodeArtwork({0x89,0x50,0x4e,0x47,13,10,26,10},"hero"),"NH-ART-CORRUPT: truncated image is a graceful fallback");
 auto resource=FindResourceW(nullptr,MAKEINTRESOURCEW(102),RT_RCDATA);auto loaded=LoadResource(nullptr,resource);auto data=(uint8_t*)LockResource(loaded);auto size=SizeofResource(nullptr,resource);std::vector<uint8_t> bytes(data,data+size);
 auto logo=nh::DecodeArtwork(bytes,"logo");auto hero=nh::DecodeArtwork(bytes,"hero");
 check(logo&&hero&&logo.width<=1280&&hero.width<=1600,"NH-ART-DECODE: local PNG decodes independently for logo and hero roles");
 wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);check((bool)nh::ExtractGameIcon(path),"NH-ART-ICON: executable resource icon extracted without execution");
 {Microsoft::WRL::ComPtr<ID3D11Device> device;D3D_FEATURE_LEVEL level;auto created=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,nullptr);check(SUCCEEDED(created),"NH-ART-SERVICE: software graphics device available");if(device){nh::ArtworkService service;service.Attach(device.Get());nh::Game game{"local-icon","Local icon","Manual","C:/fixture",{}};game.target.path=nh::Utf8(path);auto start=GetTickCount64();nh::ArtworkView image;while(GetTickCount64()-start<5000){service.Poll();image=service.Get(game,"icon",false);if(image)break;Sleep(5);}check(image&&image.width==64&&image.height==64,"NH-ART-SERVICE: asynchronous local icon uploaded to UI device offline");service.Shutdown();check(!service.Get(game,"icon",false),"NH-ART-SERVICE: shutdown releases texture ownership and stops requests");}}
 {
  // Unique ownership under the explicit self-test root; never touch installed Steam data.
  auto fixture=nh::UserRoot()/L"artwork-local-fixture";RemoveArtworkFixture(fixture);
  auto central=fixture/L"Steam"/L"appcache"/L"librarycache",grid=fixture/L"Steam"/L"userdata"/L"12345"/L"config"/L"grid";
  bool ready=WriteArtworkFixture(grid/L"730p.png",600,900)&&WriteArtworkFixture(central/L"731"/L"library_300x450.png",300,450)&&WriteArtworkFixture(central/L"730_icon.png",24,24)&&WriteArtworkFixture(central/L"730_library_hero.png",160,50)&&WriteArtworkFixture(grid/L"730_icon.png",32,32)&&WriteArtworkFixture(grid/L"730_hero.png",180,60)&&WriteArtworkFixture(grid/L"730_logo.png",96,32)&&WriteArtworkFixture(central/L"731"/L"abcdef0123456789abcdef0123456789abcdef01_library_hero.png",150,50)&&WriteArtworkFixture(central/L"731"/L"abcdef0123456789abcdef0123456789abcdef01_logo.png",90,30)&&WriteArtworkFixture(central/L"731"/L"abcdef0123456789abcdef0123456789abcdef01.jpg",40,40)&&WriteArtworkFixture(central/L"7320_icon.png",20,20)&&WriteArtworkFixture(central/L"732"/L"abcdef0123456789abcdef0123456789abcdef01.jpg",120,40)&&WriteArtworkFixture(central/L"734_icon.png",28,28)&&WriteArtworkFixture(central/L"734_header.jpg",200,80)&&WriteArtworkFixture(fixture/L"hint.png",36,36)&&WriteIconFixture(grid/L"735_icon.ico")&&WriteArtworkFixture(central/L"737"/L"library_hero.png",140,40)&&WriteArtworkFixture(central/L"737"/L"logo.png",84,28)&&WriteArtworkFixture(central/L"737"/L"icon.png",26,26);
  check(ready,"NH-ART-LOCAL-FIXTURE: isolated Steam cache and custom artwork generated");
  Microsoft::WRL::ComPtr<ID3D11Device> device;D3D_FEATURE_LEVEL level;D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,nullptr);
  if(ready&&device){nh::ArtworkService service;service.Attach(device.Get());nh::Game game{"steam-local-custom","Fixture","Steam",nh::Utf8(fixture.wstring()),{}};game.storeId="730";game.steamRoot=nh::Utf8((fixture/L"Steam").wstring());game.target.path=nh::Utf8(path);
   auto icon=WaitArtwork(service,game,"icon"),customHero=WaitArtwork(service,game,"hero"),customLogo=WaitArtwork(service,game,"logo");
   check(icon&&icon.width==32&&icon.height==32,"NH-ART-LOCAL-PRIORITY: exact custom Steam icon precedes central and executable icons");
   check(customHero&&customHero.width==180&&customLogo&&customLogo.width==96,"NH-ART-CUSTOM: userdata grid hero and logo found offline independently");
   auto customCover=WaitArtwork(service,game,"cover");check(customCover&&customCover.width==600&&customCover.height==900,"NH-ART-PORTRAIT: Steam custom portrait grid is loaded independently of hero");
   game.id="steam-local-nested";game.storeId="731";game.target.path.clear();auto nestedIcon=WaitArtwork(service,game,"icon"),nestedHero=WaitArtwork(service,game,"hero"),nestedLogo=WaitArtwork(service,game,"logo");
   check(nestedIcon&&nestedIcon.width==40&&nestedHero&&nestedHero.width==150&&nestedLogo&&nestedLogo.width==90,"NH-ART-NESTED: app-ID folder accepts labelled hashes and square hash icon");
   auto nestedCover=WaitArtwork(service,game,"cover");check(nestedCover&&nestedCover.width==300&&nestedCover.height==450,"NH-ART-PORTRAIT: exact app-ID nested 300x450 cover is available offline");
   game.id="steam-local-no-prefix";game.storeId="732";check(!WaitArtwork(service,game,"icon"),"NH-ART-APPID: another app ID's icon and nonsquare unlabelled hash are refused");
   game.id="steam-local-hint";game.storeId="733";game.iconHint=nh::Utf8((fixture/L"hint.png").wstring());game.target.path=nh::Utf8(path);auto hint=WaitArtwork(service,game,"icon");check(hint&&hint.width==36,"NH-ART-HINT: identified local icon hint precedes executable fallback");
   game.id="steam-local-central";game.storeId="734";game.iconHint.clear();auto centralIcon=WaitArtwork(service,game,"icon"),header=WaitArtwork(service,game,"hero");check(centralIcon&&centralIcon.width==28&&header&&header.width==200,"NH-ART-CENTRAL: central icon precedes executable and header is a hero fallback");
   game.id="steam-local-ico";game.storeId="735";auto ico=WaitArtwork(service,game,"icon");check(ico&&ico.width==24,"NH-ART-ICO: custom Steam ICO icon decoded locally within the icon bounds");
   game.id="steam-local-nested-bare";game.storeId="737";auto bareIcon=WaitArtwork(service,game,"icon"),bareHero=WaitArtwork(service,game,"hero"),bareLogo=WaitArtwork(service,game,"logo");check(bareIcon&&bareIcon.width==26&&bareHero&&bareHero.width==140&&bareLogo&&bareLogo.width==84,"NH-ART-NESTED-BARE: role-only filenames remain valid inside the exact app-ID folder");
   bool lateFile=WriteArtworkFixture(central/L"736_icon.png",22,22);game.id="steam-local-index-snapshot";game.storeId="736";auto snapshot=WaitArtwork(service,game,"icon");check(lateFile&&snapshot&&snapshot.width==64,"NH-ART-INDEX: central snapshot reused across games until its bounded refresh");
   check(!service.Get(game,"unknown-role",false),"NH-ART-ROLE: unsupported role is refused without queueing");
   service.SetOnlineEnabled(false);game.id="steam-local-live-optout";game.storeId="738";auto optedOut=WaitArtwork(service,game,"icon",true);check(optedOut&&optedOut.width==64,"NH-ART-OPT-OUT: current service permission preserves local fallback for an old online request");
   service.Shutdown();
  }else check(false,"NH-ART-LOCAL: software device and isolated fixture available");
  // The entire owned subtree is bounded, has no links, and is below the explicit fixture root.
  RemoveArtworkFixture(fixture);
 }
 CoUninitialize();return failed;
}
int RunArtworkOnlineTest(bool online){
 wchar_t fixture[32768];if(!GetEnvironmentVariableW(L"NEUROTIC_HUB_FIXTURE_ROOT",fixture,32768))return 1;CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 Microsoft::WRL::ComPtr<ID3D11Device> device;D3D_FEATURE_LEVEL level;if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,nullptr)))return 1;
 nh::ArtworkService service;service.Attach(device.Get());nh::Game game{"steam-probe","Counter-Strike 2","Steam","C:/fixture",{}};game.storeId="730";nh::ArtworkView hero,logo;auto start=GetTickCount64();while(GetTickCount64()-start<25000){service.Poll();hero=service.Get(game,"hero",online);logo=service.Get(game,"logo",online);if(hero&&logo)break;Sleep(10);}
 std::cout<<(hero&&logo?"PASS ":"LIMIT ")<<"NH-ART-STEAM: "<<(online?"online":"cached offline")<<" hero "<<hero.width<<"x"<<hero.height<<", logo "<<logo.width<<"x"<<logo.height<<"\n";service.Shutdown();CoUninitialize();return hero&&logo?0:1;
}
