#include <menu/Localization.h>
#include "ManualLibrary.h"
#include <windows.h>
#include <fstream>
#include <array>
namespace nh {
std::wstring Wide(const std::string& value){if(value.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),(int)value.size(),nullptr,0);if(!n)throw std::runtime_error(Neurotic::UiMessage("desktop.manuallibrary.invalid_utf_8_7246fb96", "Invalid UTF-8"));std::wstring s(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),(int)value.size(),s.data(),n);return s;}
std::string Utf8(const std::wstring& value){if(value.empty())return {};int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),(int)value.size(),nullptr,0,nullptr,nullptr);if(!n)throw std::runtime_error(Neurotic::UiMessage("desktop.manuallibrary.invalid_unicode_c147a104", "Invalid Unicode"));std::string s(n,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),(int)value.size(),s.data(),n,nullptr,nullptr);return s;}
Target InspectExecutable(const std::filesystem::path& path){
 Target target{Utf8(path.wstring()),Utf8(path.stem().wstring()),false,Neurotic::UiLiteral("desktop.manuallibrary.select_an_existing_32_bit_or_64_bit_game_executa_1badada3", "Select an existing 32-bit or 64-bit game executable")};
 try{
  if(_wcsicmp(path.extension().c_str(),L".exe")!=0)return target;
  auto absolute=std::filesystem::absolute(path);
  for(auto walk=absolute;!walk.empty();walk=walk.parent_path()){
   DWORD attrs=GetFileAttributesW(walk.c_str());
   if(attrs!=INVALID_FILE_ATTRIBUTES&&(attrs&FILE_ATTRIBUTE_REPARSE_POINT)){target.reason=Neurotic::UiLiteral("desktop.manuallibrary.linked_paths_are_unsuitable_for_installation_f6529994", "Linked paths are unsuitable for installation");return target;}
   if(walk==walk.parent_path())break;
  }
  std::ifstream stream(absolute,std::ios::binary);IMAGE_DOS_HEADER dos{};
  if(!stream.read(reinterpret_cast<char*>(&dos),sizeof(dos))||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<sizeof(dos)||dos.e_lfanew>1024*1024)return target;
  stream.seekg(dos.e_lfanew);DWORD signature{};IMAGE_FILE_HEADER header{};
  if(!stream.read(reinterpret_cast<char*>(&signature),sizeof(signature))||!stream.read(reinterpret_cast<char*>(&header),sizeof(header))||signature!=IMAGE_NT_SIGNATURE||!(header.Characteristics&IMAGE_FILE_EXECUTABLE_IMAGE)||(header.Characteristics&IMAGE_FILE_DLL)||!header.NumberOfSections)return target;
  const bool x86=header.Machine==IMAGE_FILE_MACHINE_I386;
  if(!x86&&header.Machine!=IMAGE_FILE_MACHINE_AMD64)return target;
  const auto required=x86?sizeof(IMAGE_OPTIONAL_HEADER32):sizeof(IMAGE_OPTIONAL_HEADER64);
  if(header.SizeOfOptionalHeader<required)return target;
  auto optionalStart=stream.tellg();stream.seekg(0,std::ios::end);auto bytes=stream.tellg();
  if(bytes<optionalStart+std::streamoff(header.SizeOfOptionalHeader+header.NumberOfSections*sizeof(IMAGE_SECTION_HEADER)))return target;
  stream.seekg(optionalStart);WORD magic{};if(!stream.read(reinterpret_cast<char*>(&magic),sizeof(magic))||magic!=(x86?IMAGE_NT_OPTIONAL_HDR32_MAGIC:IMAGE_NT_OPTIONAL_HDR64_MAGIC))return target;
  target.path=Utf8(absolute.wstring());target.suitable=true;target.bitness=x86?32:64;target.reason=x86?Neurotic::UiLiteral("desktop.manuallibrary.a_compatible_in_game_integration_is_unavailable__87dd6414", "A compatible in-game integration is unavailable for this architecture. Use NR Anything."):Neurotic::UiLiteral("desktop.manuallibrary.compatible_x64_executable_confirm_this_is_the_ga_10dba3ac", "Compatible x64 executable. Confirm this is the game, not its launcher.");
 }catch(const std::exception& e){target.reason=e.what();}
 return target;
}
}
