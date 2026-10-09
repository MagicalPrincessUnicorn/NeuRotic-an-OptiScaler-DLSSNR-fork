#include <menu/Localization.h>
#include "GameDiagnostics.h"
#include "ManualLibrary.h"
#include <vector>
#include <algorithm>
#include <sstream>
namespace nh {
namespace {
struct Handles {
 std::vector<HANDLE> values;
 ~Handles(){for(auto handle:values)CloseHandle(handle);}
 HANDLE Add(HANDLE handle){if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error(Neurotic::UiMessage("desktop.gamediagnostics.the_selected_game_folder_or_log_cannot_be_read_1a756053", "The selected game folder or log cannot be read."));values.push_back(handle);return handle;}
};
void RequireOrdinary(HANDLE handle,bool directory){
 BY_HANDLE_FILE_INFORMATION info{};
 if(!GetFileInformationByHandle(handle,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||((info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0)!=directory)throw std::runtime_error(Neurotic::UiMessage("desktop.gamediagnostics.linked_or_unexpected_game_paths_are_unavailable__e4b291de", "Linked or unexpected game paths are unavailable for diagnostics."));
}
std::string SafeText(const Json& object,const char* name,size_t limit=256){
 if(!object.is_object()||!object.contains(name)||!object[name].is_string())return Neurotic::UiMessage("desktop.anythingview.unavailable_48a4b800", "Unavailable");
 auto text=object[name].get<std::string>();if(text.size()>limit)return Neurotic::UiMessage("desktop.anythingview.unavailable_48a4b800", "Unavailable");Wide(text);
 for(char& c:text)if((unsigned char)c<32&&c!='\t')c=' ';return text;
}
bool TrimIncompleteUtf8(std::string& text){
 if(text.empty())return false;size_t start=text.size()-1;
 while(start&&((unsigned char)text[start]&0xc0)==0x80)--start;
 auto lead=(unsigned char)text[start];size_t expected=lead>=0xc2&&lead<=0xdf?2:lead>=0xe0&&lead<=0xef?3:lead>=0xf0&&lead<=0xf4?4:0;
 if(!expected||text.size()-start>=expected)return false;
 for(size_t i=start+1;i<text.size();++i)if(((unsigned char)text[i]&0xc0)!=0x80)return false;
 if(start+1<text.size()){
  auto second=(unsigned char)text[start+1];
  if((lead==0xe0&&second<0xa0)||(lead==0xed&&second>=0xa0)||(lead==0xf0&&second<0x90)||(lead==0xf4&&second>=0x90))return false;
 }
 try{Wide(text.substr(0,start));}catch(...){return false;}
 text.resize(start);return true;
}
std::string Decode(const std::string& bytes,bool truncated){
 std::string decoded;
 if(bytes.size()>=2&&(((unsigned char)bytes[0]==0xff&&(unsigned char)bytes[1]==0xfe)||((unsigned char)bytes[0]==0xfe&&(unsigned char)bytes[1]==0xff))){
  bool little=(unsigned char)bytes[0]==0xff;size_t length=bytes.size()-2;
  if(length%2&&!truncated)throw std::runtime_error(Neurotic::UiMessage("desktop.gamediagnostics.the_game_log_has_incomplete_utf_16_text_69553032", "The game log has incomplete UTF-16 text."));
  std::wstring wide;wide.reserve(length/2);
  for(size_t i=2;i+1<bytes.size();i+=2){auto a=(unsigned char)bytes[i],b=(unsigned char)bytes[i+1];wide.push_back((wchar_t)(little?(a|(b<<8)):(b|(a<<8))));}
  if(truncated&&!wide.empty()&&wide.back()>=0xd800&&wide.back()<=0xdbff)wide.pop_back();
  decoded=Utf8(wide);
 }else{
  size_t start=bytes.starts_with("\xef\xbb\xbf")?3:0;decoded=bytes.substr(start);
  try{Wide(decoded);}catch(...){
   // A bounded prefix may end inside one UTF-8 code point, but invalid bytes
   // elsewhere are refused. Do not silently accept an arbitrary encoding.
   if(!truncated||!TrimIncompleteUtf8(decoded))throw std::runtime_error(Neurotic::UiMessage("desktop.gamediagnostics.the_game_log_is_not_valid_utf_8_or_bom_marked_ut_2f986456", "The game log is not valid UTF-8 or BOM-marked UTF-16 text."));
  }
 }
 // ImGui consumes NUL-terminated text; show binary/control bytes explicitly.
 for(char& c:decoded)if((unsigned char)c<32&&c!='\n'&&c!='\r'&&c!='\t')c='?';return decoded;
}
}
GameDiagnosticsResult ReadGameDiagnostics(const std::string& executable,const Json& inspection){
 GameDiagnosticsResult result;
 try{
  std::ostringstream text;text<<Neurotic::UiLiteral("desktop.gamediagnostics.selected_game_diagnostics_last_hub_inspection_ca_79216248", "Selected game diagnostics\nLast Hub inspection (cached; Refresh checks installation files again).\n");
  text<<"Installation: "<<SafeText(inspection.at("state"),"status")<<"\nProxy: "<<SafeText(inspection.at("state"),"proxy")<<Neurotic::UiLiteral("desktop.gamediagnostics.config_revision_4f4d4e06", "\nConfig revision: ")<<SafeText(inspection,"configRevision")<<"\n";
  text<<"Runtime: "<<SafeText(inspection,Neurotic::UiLiteral("desktop.hubshell.runtime_d63852ad", "runtime"))<<Neurotic::UiLiteral("desktop.gamediagnostics.the_hub_does_not_load_the_game_runtime_saved_set_5a67ef5a", " (the Hub does not load the game runtime).\n\nSaved settings from the last inspection:\n");
  if(inspection.contains("settings")&&inspection["settings"].is_array()){
   size_t count=0;for(auto& setting:inspection["settings"]){if(++count>128){text<<Neurotic::UiLiteral("desktop.gamediagnostics.settings_projection_limited_8a092027", "[Settings projection limited]\n");break;}text<<SafeText(setting,"section")<<" / "<<SafeText(setting,"key")<<" = "<<SafeText(setting,"value",1024)<<"\n";}
  }
  result.text=text.str();
  auto path=std::filesystem::path(Wide(executable));
  if(!path.is_absolute()||path.wstring().starts_with(L"\\\\")||path.wstring().find(L':',2)!=std::wstring::npos||path.wstring().find(L'\0')!=std::wstring::npos||path.lexically_normal()!=path)throw std::runtime_error(Neurotic::UiMessage("desktop.gamediagnostics.a_local_ordinary_game_executable_path_is_require_9b1210c4", "A local ordinary game executable path is required."));
  Handles handles;auto root=path.parent_path();auto current=root.root_path();
  // Keep ancestor handles without FILE_SHARE_DELETE until the read completes.
  // This prevents a checked folder being renamed into a reparse escape.
  auto lockDirectory=[&](const std::filesystem::path& directory){auto handle=handles.Add(CreateFileW(directory.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));RequireOrdinary(handle,true);};
  lockDirectory(current);for(auto& component:root.relative_path()){current/=component;lockDirectory(current);}
  const bool prepared=InspectExecutable(path).bitness==32;
  auto logRoot=root;
  if(prepared){
   logRoot=root/L"NeuRotic/Prepared/NeuRotic.GpuHost";
   current=root;
   for(auto component:{L"NeuRotic",L"Prepared",L"NeuRotic.GpuHost"}){
    current/=component;auto attrs=GetFileAttributesW(current.c_str());
    if(attrs==INVALID_FILE_ATTRIBUTES&&(GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_PATH_NOT_FOUND)){result.text+=Neurotic::UiLiteral("desktop.gamediagnostics.no_prepared_renderer_log_is_present_yet_install__34c5b0c2", "\nNo prepared renderer log is present yet. Install the prepared route, then use Play.\n");result.status=Neurotic::UiLiteral("desktop.gamediagnostics.no_prepared_renderer_log_is_present_yet_bc31e22d", "No prepared renderer log is present yet.");return result;}
    lockDirectory(current);
   }
  }
  auto log=logRoot/L"OptiScaler.log";
  if(prepared&&!std::filesystem::exists(log)){
   // SingleFile=false writes OptiScaler_<ticks>.log beside the helper DLL.
   // Search only this pinned directory, with a fixed enumeration budget.
   std::filesystem::file_time_type newest=std::filesystem::file_time_type::min();size_t visited=0;
   for(auto& entry:std::filesystem::directory_iterator(logRoot)){
    if(++visited>512)break;auto name=entry.path().filename().wstring();
    if(!name.starts_with(L"OptiScaler_")||!name.ends_with(L".log"))continue;
    auto digits=name.substr(11,name.size()-15);if(digits.empty()||!std::all_of(digits.begin(),digits.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';}))continue;
    auto attrs=GetFileAttributesW(entry.path().c_str());if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)))continue;
    auto when=entry.last_write_time();if(when>=newest){newest=when;log=entry.path();}
   }
  }
  auto raw=CreateFileW(log.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
  if(raw==INVALID_HANDLE_VALUE&&(GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_PATH_NOT_FOUND)){result.text+=prepared?Neurotic::UiLiteral("desktop.gamediagnostics.no_prepared_renderer_log_is_present_yet_use_play_4f8f5a41", "\nNo prepared renderer log is present yet. Use Play to generate one.\n"):Neurotic::UiLiteral("desktop.gamediagnostics.optiscaler_log_no_game_log_is_present_yet_run_th_28431ba3", "\nOptiScaler.log\nNo game log is present yet. Run the game to generate one.\n");result.status=prepared?Neurotic::UiLiteral("desktop.gamediagnostics.no_prepared_renderer_log_is_present_yet_bc31e22d", "No prepared renderer log is present yet."):Neurotic::UiLiteral("desktop.gamediagnostics.no_game_log_is_present_yet_2e3c7bc5", "No game log is present yet.");return result;}
  auto file=handles.Add(raw);RequireOrdinary(file,false);
  wchar_t resolved[32768];auto resolvedSize=GetFinalPathNameByHandleW(file,resolved,32768,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
  auto expected=L"\\\\?\\"+log.wstring();std::replace(expected.begin(),expected.end(),L'/',L'\\');
  if(!resolvedSize||resolvedSize>=32768||_wcsicmp(resolved,expected.c_str())!=0)throw std::runtime_error(Neurotic::UiMessage("desktop.gamediagnostics.the_game_log_resolves_outside_its_expected_selec_d01fab27", "The game log resolves outside its expected selected folder."));
  LARGE_INTEGER size{};if(!GetFileSizeEx(file,&size)||size.QuadPart<0)throw std::runtime_error(Neurotic::UiMessage("desktop.gamediagnostics.the_game_log_size_could_not_be_checked_fa1087c0", "The game log size could not be checked."));
  auto limit=(DWORD)std::min<uint64_t>((uint64_t)size.QuadPart,GameLogByteLimit);std::string bytes(limit,'\0');DWORD total=0;
  while(total<limit){DWORD read=0;if(!ReadFile(file,bytes.data()+total,limit-total,&read,nullptr))throw std::runtime_error(Neurotic::UiMessage("desktop.gamediagnostics.the_game_log_could_not_be_read_fc17d011", "The game log could not be read."));if(!read)break;total+=read;}bytes.resize(total);
  bool truncated=size.QuadPart>(LONGLONG)GameLogByteLimit;result.text+="\n"+Utf8(log.lexically_relative(root).wstring())+"\n"+Decode(bytes,truncated);
  if(truncated)result.text+=Neurotic::UiLiteral("desktop.gamediagnostics.showing_only_the_first_256_kib_of_this_log_97263ad4", "\n[Showing only the first 256 KiB of this log.]\n");
  result.status=truncated?Neurotic::UiLiteral("desktop.gamediagnostics.diagnostics_ready_game_log_limited_to_256_kib_d833437f", "Diagnostics ready; game log limited to 256 KiB."):Neurotic::UiLiteral("desktop.gamediagnostics.diagnostics_ready_f6a90a17", "Diagnostics ready.");
 }catch(const std::exception& e){result.status=std::string(Neurotic::UiLiteral("desktop.gamediagnostics.game_log_unavailable_1f4bd10a", "Game log unavailable: "))+e.what();result.text+="\n"+result.status+"\n";}
 return result;
}
}
