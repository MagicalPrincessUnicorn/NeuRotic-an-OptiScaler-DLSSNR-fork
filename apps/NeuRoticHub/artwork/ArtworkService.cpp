#include <menu/Localization.h>
#include "ArtworkService.h"
#include "ArtworkPolicy.h"
#include "storage/UserFile.h"
#include "ui/HubViewModel.h"
#include <d3d11.h>
#include <winhttp.h>
#include <wrl/client.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <unordered_map>
#include <atomic>
#include <fstream>
#include <algorithm>
#include <array>
#include <unordered_set>
using Microsoft::WRL::ComPtr;
namespace nh {
namespace {
struct HttpHandle {HINTERNET value=nullptr;explicit HttpHandle(HINTERNET v=nullptr):value(v){}~HttpHandle(){if(value)WinHttpCloseHandle(value);}operator HINTERNET()const{return value;}HttpHandle(const HttpHandle&)=delete;};
bool SafePath(const std::filesystem::path& path){auto text=path.native();if(text.size()<3||text.size()>32767||!iswalpha(text[0])||text[1]!=L':'||(text[2]!=L'\\'&&text[2]!=L'/')||text.find(L':',2)!=std::wstring::npos||text.find(L'\0')!=std::wstring::npos)return false;for(auto& part:path)if(part==L"..")return false;for(auto p=path;!p.empty();){auto attrs=GetFileAttributesW(p.c_str());if(attrs!=INVALID_FILE_ATTRIBUTES&&(attrs&FILE_ATTRIBUTE_REPARSE_POINT))return false;auto parent=p.parent_path();if(parent==p)break;p=parent;}return true;}
std::vector<uint8_t> ReadImage(const std::filesystem::path& path){if(!SafePath(path))return {};std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)return {};auto size=file.tellg();if(size<8||size>8388608)return {};std::vector<uint8_t> bytes((size_t)size);file.seekg(0);if(!file.read((char*)bytes.data(),size))return {};return bytes;}
std::string HashKey(const std::string& value){uint64_t hash=14695981039346656037ull;for(auto c:value){hash^=(uint8_t)c;hash*=1099511628211ull;}char text[17];sprintf_s(text,"%016llx",(unsigned long long)hash);return text;}
std::vector<uint8_t> Fetch(std::wstring url,size_t limit,ULONGLONG deadline,const std::atomic<bool>& stop,const std::atomic<bool>& onlineEnabled){
 for(int redirect=0;redirect<=4&&!stop&&onlineEnabled&&GetTickCount64()<deadline;redirect++){
  if(!AllowedArtworkUrl(url))return {};
  URL_COMPONENTS parts{sizeof(parts)};parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=(DWORD)-1;if(!WinHttpCrackUrl(url.c_str(),(DWORD)url.size(),0,&parts))return {};
  std::wstring host(parts.lpszHostName,parts.dwHostNameLength),path(parts.lpszUrlPath,parts.dwUrlPathLength);if(parts.dwExtraInfoLength)path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
  HttpHandle session(WinHttpOpen(L"NeuRotic-Hub/fourth-pass",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));if(!session)return {};
  WinHttpSetTimeouts(session,1000,2000,2000,1000);HttpHandle connection(WinHttpConnect(session,host.c_str(),443,0));if(!connection)return {};
  HttpHandle request(WinHttpOpenRequest(connection,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));if(!request)return {};
  DWORD disabled=WINHTTP_DISABLE_REDIRECTS|WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION;WinHttpSetOption(request,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled));
  if(stop||!onlineEnabled||GetTickCount64()>=deadline)return {};
  if(!WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(request,nullptr))return {};
  DWORD status=0,length=sizeof(status);if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&length,WINHTTP_NO_HEADER_INDEX))return {};
  if(status>=300&&status<400){wchar_t destination[4097];length=sizeof(destination);if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_LOCATION,WINHTTP_HEADER_NAME_BY_INDEX,destination,&length,WINHTTP_NO_HEADER_INDEX))return {};url.assign(destination,length/sizeof(wchar_t));while(!url.empty()&&url.back()==0)url.pop_back();continue;}
  if(status!=200)return {};
  DWORD declared=0;length=sizeof(declared);if(WinHttpQueryHeaders(request,WINHTTP_QUERY_CONTENT_LENGTH|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&declared,&length,WINHTTP_NO_HEADER_INDEX)&&declared>limit)return {};
  std::vector<uint8_t> bytes;uint8_t buffer[16384];DWORD read=0;
  while(!stop&&onlineEnabled&&GetTickCount64()<deadline){if(!WinHttpReadData(request,buffer,sizeof(buffer),&read))return {};if(!read)return bytes;if(bytes.size()+read>limit)return {};bytes.insert(bytes.end(),buffer,buffer+read);}return {};
 }
 return {};
}
std::wstring EncodeQuery(const std::string& input){static const wchar_t hex[]=L"0123456789ABCDEF";std::wstring result;for(unsigned char c:input){if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.')result+=(wchar_t)c;else{result+=L'%';result+=hex[c>>4];result+=hex[c&15];}}return result;}
bool AppId(const std::string& value){return !value.empty()&&value.size()<=10&&std::all_of(value.begin(),value.end(),[](unsigned char c){return c>='0'&&c<='9';})&&std::stoull(value)<=UINT32_MAX&&value!="0";}
bool AssetHash(const std::string& value){return value.size()==40&&std::all_of(value.begin(),value.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');});}
int RoleIndex(const std::string& role){return role=="icon"?0:role=="hero"?1:role=="logo"?2:role=="cover"?3:-1;}
struct LocalAsset {std::filesystem::path path;bool square=false;int priority=0;};
using LocalAssets=std::array<std::vector<LocalAsset>,4>;
// Explicit role tokens only. A hash without a role is considered solely as a
// square icon inside its own numeric app-ID directory, never as a hero/logo.
void IndexAsset(std::unordered_map<std::string,LocalAssets>& index,const std::filesystem::path& path,const std::string& nestedApp={},bool custom=false){
 auto extension=Utf8(path.extension().wstring());std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return (char)tolower(c);});if(extension!=".png"&&extension!=".jpg"&&extension!=".jpeg"&&extension!=".ico")return;
 auto stem=Utf8(path.stem().wstring());std::transform(stem.begin(),stem.end(),stem.begin(),[](unsigned char c){return (char)tolower(c);});auto separator=stem.find('_');std::string app=nestedApp,token;bool square=false;
 if(app.empty()){
  auto prefix=stem.substr(0,separator);if(custom&&separator==std::string::npos&&prefix.ends_with("p")&&AppId(prefix.substr(0,prefix.size()-1))){app=prefix.substr(0,prefix.size()-1);token="library_600x900";}else{if(!AppId(prefix))return;app=prefix;
  if(separator==std::string::npos){if(custom)return;square=true;}else token=stem.substr(separator+1);}
 }else if(separator==std::string::npos){if(stem==app||AssetHash(stem))square=true;else token=stem;}
 else{auto prefix=stem.substr(0,separator);token=(prefix==app||AssetHash(prefix))?stem.substr(separator+1):stem;}
 int role=-1,priority=square?2:0;
 if(square)role=0;
 else if(token=="icon")role=0;
 else if(token=="library_hero"||token=="hero")role=1;
 else if(token=="logo"||token=="library_logo")role=2;
 else if(token=="library_600x900"||token=="library_300x450"||token=="library_capsule")role=3;
 else if(!custom&&token==Neurotic::UiLiteral("desktop.translationcontrolpreview.header_8d37258e", "header")){role=1;priority=1;}
 // New Steam caches may append a content hash after the exact role token.
 else for(auto name:{"library_600x900","library_300x450","library_capsule","library_hero","library_logo","icon","logo","hero",Neurotic::UiLiteral("desktop.translationcontrolpreview.header_8d37258e", "header")}){std::string prefix=std::string(name)+"_";if(token.starts_with(prefix)&&AssetHash(token.substr(prefix.size()))){role=(std::string(name)=="library_600x900"||std::string(name)=="library_300x450"||std::string(name)=="library_capsule")?3:std::string(name)=="icon"?0:(std::string(name)=="logo"||std::string(name)=="library_logo")?2:1;priority=std::string(name)==Neurotic::UiLiteral("desktop.translationcontrolpreview.header_8d37258e", "header")?1:0;break;}}
 if(role<0||(extension==".ico"&&role!=0))return;auto& choices=index[app][role];choices.push_back({path,square,priority});std::sort(choices.begin(),choices.end(),[](const auto& a,const auto& b){if(a.priority!=b.priority)return a.priority<b.priority;return a.path.native()<b.path.native();});if(choices.size()>8)choices.resize(8);
}
}
struct ArtworkService::Impl {
 struct Job {std::string key,role,exe,steamRoot,appId,icon,diskKey;bool online;uint64_t revision=0;};
 struct Output {std::string key;ArtworkPixels pixels;uint64_t revision;bool remote,definitive;};
 struct Entry {ComPtr<ID3D11ShaderResourceView> view;unsigned width=0,height=0;uint64_t used=0,revision=0;ULONGLONG retry=0;bool queued=false,definitive=false;};
 std::unordered_map<std::string,Entry> entries;ComPtr<ID3D11Device> device;uint64_t frame=0,nextRevision=0;
 std::mutex mutex;std::condition_variable cv;std::deque<Job> jobs,remoteJobs;std::deque<Output> outputs;std::thread workers[2];std::atomic<bool> stop=false;
 std::atomic<bool> onlineEnabled=true;
 std::unordered_set<std::string> remotePending;
 struct LocalIndex {ULONGLONG updated=0;std::unordered_map<std::string,LocalAssets> central,custom,nested;std::unordered_set<std::string> nestedScanned;};
 // Owned by the local worker. At most four roots and one bounded scan per
 // minute; per-app subdirectories are scanned once per snapshot.
 std::unordered_map<std::string,LocalIndex> localIndexes;
 std::filesystem::path cache;
 Impl(){cache=UserRoot()/L"artwork";workers[0]=std::thread([this]{Work(false);});workers[1]=std::thread([this]{Work(true);});}
 ~Impl(){Stop();}
 void Stop(){stop=true;cv.notify_all();for(auto& worker:workers)if(worker.joinable())worker.join();entries.clear();jobs.clear();remoteJobs.clear();outputs.clear();remotePending.clear();localIndexes.clear();device.Reset();}
 void TrimDisk(){
  std::error_code error;if(!SafePath(cache)||!std::filesystem::is_directory(cache,error))return;
  struct File{std::filesystem::path path;uintmax_t bytes;std::filesystem::file_time_type time;};std::vector<File> files;uintmax_t size=0;
  for(std::filesystem::directory_iterator it(cache,error),end;!error&&it!=end;it.increment(error)){auto path=it->path();if((path.extension()!=L".image"&&path.extension()!=L".missing")||!SafePath(path))continue;if(files.size()>=2048){std::filesystem::remove(path,error);error.clear();continue;}auto bytes=it->file_size(error);if(error)break;auto time=it->last_write_time(error);if(error)break;files.push_back({path,bytes,time});size+=bytes;}
  std::sort(files.begin(),files.end(),[](auto& a,auto& b){return a.time<b.time;});for(auto& file:files){if(size<=134217728)break;std::filesystem::remove(file.path,error);if(!error)size-=file.bytes;error.clear();}
 }
 template<class Callback> void ScanFolder(const std::filesystem::path& folder,size_t& remaining,Callback visit){
  std::error_code error;if(!remaining||stop||!SafePath(folder)||!std::filesystem::is_directory(folder,error))return;
  for(std::filesystem::directory_iterator it(folder,error),end;!error&&it!=end&&remaining&&!stop;it.increment(error)){--remaining;auto path=it->path();auto attrs=GetFileAttributesW(path.c_str());if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&FILE_ATTRIBUTE_REPARSE_POINT))continue;try{visit(path,(attrs&FILE_ATTRIBUTE_DIRECTORY)!=0);}catch(const std::exception&){}}
 }
 LocalAssets LocalChoices(const Job& job){
  LocalAssets choices;if(job.steamRoot.empty()||!AppId(job.appId))return choices;auto root=std::filesystem::path(Wide(job.steamRoot));if(!SafePath(root))return choices;
  auto key=Utf8(root.lexically_normal().wstring());std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return (char)tolower(c);});
  if(!localIndexes.contains(key)&&localIndexes.size()>=4){auto oldest=std::min_element(localIndexes.begin(),localIndexes.end(),[](auto& a,auto& b){return a.second.updated<b.second.updated;});localIndexes.erase(oldest);}
  auto& index=localIndexes[key];auto now=GetTickCount64();auto folder=root/L"appcache"/L"librarycache";
  if(!index.updated||now-index.updated>=60000){
   index=LocalIndex{};index.updated=now;size_t remaining=20000;ScanFolder(folder,remaining,[&](const auto& path,bool directory){if(!directory)IndexAsset(index.central,path);});
   size_t users=64,gridEntries=20000;ScanFolder(root/L"userdata",users,[&](const auto& path,bool directory){if(!directory||!AppId(Utf8(path.filename().wstring())))return;ScanFolder(path/L"config"/L"grid",gridEntries,[&](const auto& image,bool subdir){if(!subdir)IndexAsset(index.custom,image,{},true);});});
  }
  if(!index.nestedScanned.contains(job.appId)&&index.nestedScanned.size()<256){index.nestedScanned.insert(job.appId);size_t remaining=512;ScanFolder(folder/Wide(job.appId),remaining,[&](const auto& path,bool directory){if(!directory)IndexAsset(index.nested,path,job.appId);});}
  for(auto* source:{&index.central,&index.nested}){auto it=source->find(job.appId);if(it==source->end())continue;for(size_t role=0;role<choices.size();role++)choices[role].insert(choices[role].end(),it->second[role].begin(),it->second[role].end());}
  for(size_t role=0;role<choices.size();role++){std::stable_sort(choices[role].begin(),choices[role].end(),[](auto& a,auto& b){return a.priority<b.priority;});auto custom=index.custom.find(job.appId);if(custom!=index.custom.end())choices[role].insert(choices[role].begin(),custom->second[role].begin(),custom->second[role].end());}
  return choices;
 }
 ArtworkPixels LoadLocal(const Job& job,bool& definitive){
  definitive=true;
  auto choices=LocalChoices(job);for(auto& candidate:choices[RoleIndex(job.role)]){auto image=DecodeArtwork(ReadImage(candidate.path),job.role);if(image&&(!candidate.square||image.width==image.height))return image;}
  if(job.role=="icon"&&!job.icon.empty()){auto path=std::filesystem::path(Wide(job.icon));if(auto image=DecodeArtwork(ReadImage(path),job.role))return image;}
  const auto cached=cache/(Wide(HashKey(job.diskKey))+L".image");
  auto bytes=ReadImage(cached);if(auto pixels=DecodeArtwork(bytes,job.role)){std::error_code e;std::filesystem::last_write_time(cached,std::filesystem::file_time_type::clock::now(),e);return pixels;}
  definitive=false;
  if(job.role=="icon"&&!job.exe.empty()&&SafePath(Wide(job.exe))){auto icon=ExtractGameIcon(Wide(job.exe));if(icon)return icon;}
  return {};
 }
 ArtworkPixels LoadRemote(const Job& job){
  if(!job.online||!onlineEnabled||!AppId(job.appId)||stop)return {};
  const auto cached=cache/(Wide(HashKey(job.diskKey))+L".image"),missing=cache/(Wide(HashKey(job.diskKey))+L".missing");
  std::error_code error;if(SafePath(missing)&&std::filesystem::exists(missing,error)){auto age=std::filesystem::file_time_type::clock::now()-std::filesystem::last_write_time(missing,error);if(!error&&age<std::chrono::hours(24))return {};}
  auto deadline=GetTickCount64()+8000;std::vector<std::wstring> urls;
  Json query={{"ids",Json::array({{{"appid",std::stoul(job.appId)}}})},{"context",{{"country_code","US"},{"language","english"}}},{"data_request",{{"include_assets",true}}}};
  auto metadata=Fetch(L"https://api.steampowered.com/IStoreBrowseService/GetItems/v1/?input_json="+EncodeQuery(query.dump()),1048576,deadline,stop,onlineEnabled);
  try{auto json=Json::parse(metadata);auto& items=json.at("response").at("store_items");if(items.is_array()&&!items.empty()){
    auto& assets=items[0].at("assets");const char* field=job.role=="hero"?"library_hero":job.role=="logo"?"library_logo":job.role=="cover"?"library_capsule":"community_icon";
    auto format=assets.value("asset_url_format",std::string());auto asset=assets.value(field,std::string());if(asset.empty()&&job.role=="hero")asset=assets.value(Neurotic::UiLiteral("desktop.translationcontrolpreview.header_8d37258e", "header"),std::string());if(asset.empty()&&job.role=="logo")asset="logo.png";
    if(job.role=="icon"&&AssetHash(asset))urls.push_back(L"https://cdn.akamai.steamstatic.com/steamcommunity/public/images/apps/"+Wide(job.appId)+L"/"+Wide(asset)+L".jpg");
    else{auto marker=format.find("${FILENAME}");if(marker!=std::string::npos&&!asset.empty()&&format.size()<2048&&asset.size()<512){format.replace(marker,11,asset);urls.push_back(L"https://shared.fastly.steamstatic.com/store_item_assets/"+Wide(format));}}
  }}catch(...){}
  auto asset=job.role=="hero"?L"library_hero.jpg":job.role=="logo"?L"logo.png":job.role=="cover"?L"library_600x900.jpg":L"icon.jpg";urls.push_back(L"https://shared.fastly.steamstatic.com/steam/apps/"+Wide(job.appId)+L"/"+asset);
  if(job.role=="cover")urls.push_back(L"https://shared.fastly.steamstatic.com/steam/apps/"+Wide(job.appId)+L"/library_300x450.jpg");
  if(job.role=="logo")urls.push_back(L"https://shared.fastly.steamstatic.com/steam/apps/"+Wide(job.appId)+L"/library_logo.png");
  if(job.role=="hero")urls.push_back(L"https://shared.fastly.steamstatic.com/steam/apps/"+Wide(job.appId)+L"/header.jpg");
  for(auto& url:urls){if(stop||!onlineEnabled||GetTickCount64()>=deadline)break;auto fetched=Fetch(url,8388608,deadline,stop,onlineEnabled);auto image=DecodeArtwork(fetched,job.role);if(image){
    try{SaveUserFile(cached,std::string_view(reinterpret_cast<const char*>(fetched.data()),fetched.size()));TrimDisk();}catch(...){} // Optional disk caching must not discard a decoded image.
    return image;
   }}
  // Transport/decode failures are transient; never persist a negative cache marker.
  return {};
 }
 void Work(bool remote){CoInitializeEx(nullptr,COINIT_MULTITHREADED);while(!stop){Job job;{std::unique_lock lock(mutex);auto& queue=remote?remoteJobs:jobs;cv.wait(lock,[&]{return stop||!queue.empty();});if(stop)break;job=std::move(queue.front());queue.pop_front();}ArtworkPixels image;bool definitive=false;try{image=remote?LoadRemote(job):LoadLocal(job,definitive);}catch(...){}
   {std::unique_lock lock(mutex);cv.wait(lock,[&]{return stop||outputs.size()<2;});if(stop)break;outputs.push_back({job.key,std::move(image),job.revision,remote,definitive});if(remote)remotePending.erase(job.key);else if(!definitive&&job.online&&onlineEnabled&&AppId(job.appId)&&remoteJobs.size()<64&&remotePending.insert(job.key).second)remoteJobs.push_back(std::move(job));}cv.notify_all();}
  CoUninitialize();}
 void Evict(){size_t bytes=0,count=0;for(auto& pair:entries)if(pair.second.view){bytes+=(size_t)pair.second.width*pair.second.height*4;++count;}
  while(bytes>67108864||count>96){auto oldest=entries.end();for(auto it=entries.begin();it!=entries.end();++it)if(it->second.view&&(oldest==entries.end()||it->second.used<oldest->second.used))oldest=it;if(oldest==entries.end())break;bytes-=(size_t)oldest->second.width*oldest->second.height*4;--count;oldest->second.view.Reset();oldest->second.retry=0;}
 }
};
ArtworkService::ArtworkService():impl(std::make_unique<Impl>()){}
ArtworkService::~ArtworkService()=default;
void ArtworkService::Attach(ID3D11Device* device){impl->device=device;}
void ArtworkService::SetOnlineEnabled(bool enabled){if(impl->onlineEnabled.exchange(enabled)==enabled)return;if(!enabled){std::lock_guard lock(impl->mutex);impl->remoteJobs.clear();impl->remotePending.clear();}else for(auto& pair:impl->entries)if(!pair.second.definitive&&pair.first.ends_with("|online"))pair.second.retry=0;impl->cv.notify_all();}
void ArtworkService::Shutdown(){if(impl)impl->Stop();}
void ArtworkService::Poll(){
 ++impl->frame;std::deque<Impl::Output> outputs;{std::lock_guard lock(impl->mutex);outputs.swap(impl->outputs);}impl->cv.notify_all();
 for(auto& output:outputs){auto it=impl->entries.find(output.key);if(it==impl->entries.end())continue;auto& entry=it->second;if(entry.revision!=output.revision||(output.remote&&entry.definitive))continue;if(!output.remote){entry.queued=false;entry.definitive=output.definitive&&(bool)output.pixels;}entry.retry=GetTickCount64()+(output.pixels?60000:30000);if(!output.pixels||!impl->device)continue;
  auto& image=output.pixels;D3D11_TEXTURE2D_DESC desc{};desc.Width=image.width;desc.Height=image.height;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA data{image.rgba.data(),image.width*4,0};ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> view;
  if(SUCCEEDED(impl->device->CreateTexture2D(&desc,&data,&texture))&&SUCCEEDED(impl->device->CreateShaderResourceView(texture.Get(),nullptr,&view))){entry.view=view;entry.width=image.width;entry.height=image.height;}
 }impl->Evict();
}
ArtworkView ArtworkService::Get(const Game& game,const char* role,bool online){
 if(!role||RoleIndex(role)<0||!impl->device||impl->stop)return {};
 auto diskKey=game.id+"|"+role+"|"+game.target.path+"|"+game.storeId+"|"+game.steamRoot;auto key=diskKey+"|"+game.iconHint+"|"+(online?"online":"local");
 if(!impl->entries.contains(key)&&impl->entries.size()>=256){auto oldest=impl->entries.end();for(auto it=impl->entries.begin();it!=impl->entries.end();++it)if(!it->second.queued&&(oldest==impl->entries.end()||it->second.used<oldest->second.used))oldest=it;if(oldest!=impl->entries.end())impl->entries.erase(oldest);else return {};}
 auto& entry=impl->entries[key];entry.used=impl->frame;
 if(!entry.queued&&GetTickCount64()>=entry.retry){std::lock_guard lock(impl->mutex);if(impl->jobs.size()<64){entry.queued=true;entry.revision=++impl->nextRevision;Impl::Job job{key,role,game.target.path,game.steamRoot,game.store=="Steam"?game.storeId:"",game.iconHint,diskKey,online,entry.revision};impl->jobs.push_back(std::move(job));impl->cv.notify_all();}}
 return {entry.view.Get(),(float)entry.width,(float)entry.height};
}
}
