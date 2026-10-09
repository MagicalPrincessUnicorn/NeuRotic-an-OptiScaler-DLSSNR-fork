#include "LanguageFonts.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <windows.h>
#include <filesystem>
#include <string>
#include <algorithm>
#include <cctype>
namespace Neurotic::Localization {
size_t MissingGlyphs(ImFont* font,float size,std::string_view text){if(!font)return 0;size_t missing=0;auto* baked=font->GetFontBaked(size);for(auto* p=text.data();p<text.data()+text.size();){unsigned cp=0;int bytes=ImTextCharFromUtf8(&cp,p,text.data()+text.size());if(bytes<=0)break;p+=bytes;if(cp>32&&!baked->FindGlyphNoFallback(cp))++missing;}return missing;}
std::string FontNotices(){HMODULE module=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&FontNotices),&module))return {};std::string text;for(int id:{5104,5105}){auto resource=FindResourceW(module,MAKEINTRESOURCEW(id),MAKEINTRESOURCEW(10));if(!resource)continue;auto* data=static_cast<const char*>(LockResource(LoadResource(module,resource)));auto length=SizeofResource(module,resource);if(data&&length){text.append(data,length);text+="\n\n";}}return text;}
bool QualifiedInitialScript(std::string_view locale){std::string base(locale.substr(0,locale.find('-')));std::transform(base.begin(),base.end(),base.begin(),[](unsigned char c){return char(std::tolower(c));});return base=="en"||base=="es"||base=="pt"||base=="de"||base=="fr"||base=="pl"||base=="ru"||base=="zh"||base=="ja"||base=="ko";}
void AddFontFallbacks(ImFontAtlas* atlas,float size,std::string_view locale,bool useInstalled){
 if(!atlas)return;if(atlas->Fonts.empty())atlas->AddFontDefault();
 wchar_t windows[MAX_PATH]{};if(useInstalled&&GetWindowsDirectoryW(windows,MAX_PATH)){
  std::string tag(locale);std::transform(tag.begin(),tag.end(),tag.begin(),[](unsigned char c){return char(std::tolower(c));});auto fonts=std::filesystem::path(windows)/L"Fonts";const wchar_t* regional=tag.starts_with("ja")?L"meiryo.ttc":tag.starts_with("ko")?L"malgun.ttf":L"msyh.ttc";
  for(auto name:{L"arial.ttf",regional}){auto file=fonts/name;std::error_code error;if(!std::filesystem::is_regular_file(file,error))continue;ImFontConfig config;config.MergeMode=true;atlas->AddFontFromFileTTF(file.string().c_str(),size,&config);}
 }
 HMODULE module=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&AddFontFallbacks),&module))return;
 for(int id:{5103,5102}){auto resource=FindResourceW(module,MAKEINTRESOURCEW(id),MAKEINTRESOURCEW(10));if(!resource)continue;auto handle=LoadResource(module,resource);void* bytes=LockResource(handle);auto length=SizeofResource(module,resource);
  if(bytes&&length){ImFontConfig config;config.MergeMode=true;config.FontDataOwnedByAtlas=false;atlas->AddFontFromMemoryTTF(bytes,int(length),size,&config);}}
}
}
