#include <menu/Localization.h>
#include "ManualLibrary.h"
#include <windows.h>
#include <fstream>
#include <array>
#include <vector>
#include <algorithm>
#include <optional>
#include <limits>
namespace nh {
std::wstring Wide(const std::string& value){if(value.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),(int)value.size(),nullptr,0);if(!n)throw std::runtime_error(Neurotic::UiMessage("desktop.manuallibrary.invalid_utf_8_7246fb96", "Invalid UTF-8"));std::wstring s(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),(int)value.size(),s.data(),n);return s;}
std::string Utf8(const std::wstring& value){if(value.empty())return {};int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),(int)value.size(),nullptr,0,nullptr,nullptr);if(!n)throw std::runtime_error(Neurotic::UiMessage("desktop.manuallibrary.invalid_unicode_c147a104", "Invalid Unicode"));std::string s(n,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),(int)value.size(),s.data(),n,nullptr,nullptr);return s;}
// Read declared DLL names only. This is a proxy recommendation, never evidence
// that a renderer is active, that an image can execute, or that NR is compatible.
static std::string ImportedProxy(std::ifstream& stream,std::streamoff optionalStart,const IMAGE_FILE_HEADER& header,bool pe32,uint64_t bytes){
 size_t reads=0;
 auto read=[&](uint64_t at,void* out,size_t count){if(++reads>16384||at>bytes||count>bytes-at)return false;stream.clear();stream.seekg(static_cast<std::streamoff>(at));return bool(stream.read(static_cast<char*>(out),static_cast<std::streamsize>(count)));};
 IMAGE_DATA_DIRECTORY imports{},delays{};uint32_t headerBytes=0;uint64_t imageBase=0;
 if(pe32){IMAGE_OPTIONAL_HEADER32 optional{};if(!read(optionalStart,&optional,sizeof(optional)))return {};headerBytes=optional.SizeOfHeaders;imageBase=optional.ImageBase;if(optional.NumberOfRvaAndSizes>IMAGE_DIRECTORY_ENTRY_IMPORT)imports=optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(optional.NumberOfRvaAndSizes>IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT)delays=optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];}
 else{IMAGE_OPTIONAL_HEADER64 optional{};if(!read(optionalStart,&optional,sizeof(optional)))return {};headerBytes=optional.SizeOfHeaders;imageBase=optional.ImageBase;if(optional.NumberOfRvaAndSizes>IMAGE_DIRECTORY_ENTRY_IMPORT)imports=optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(optional.NumberOfRvaAndSizes>IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT)delays=optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];}
 // Limit recommendation work independently from executable eligibility.
 if(header.NumberOfSections>256)return {};
 std::vector<IMAGE_SECTION_HEADER> sections(header.NumberOfSections);
 if(!read(uint64_t(optionalStart)+header.SizeOfOptionalHeader,sections.data(),sections.size()*sizeof(IMAGE_SECTION_HEADER)))return {};
 auto offset=[&](uint64_t rva,size_t count)->std::optional<uint64_t>{
  if(rva>UINT32_MAX||count>uint64_t(UINT32_MAX)+1-rva)return {};
  std::optional<uint64_t> found;
  if(rva<headerBytes&&count<=uint64_t(headerBytes)-rva&&rva<=bytes&&count<=bytes-rva)found=rva;
  for(const auto& section:sections){if(rva<section.VirtualAddress)continue;const auto delta=rva-section.VirtualAddress;
   if(delta>=section.SizeOfRawData||count>uint64_t(section.SizeOfRawData)-delta)continue;
   const auto raw=uint64_t(section.PointerToRawData)+delta;if(raw>bytes||count>bytes-raw)return {};
   if(found)return {};found=raw;
  }return found;
 };
 bool dx=false,vulkan=false;
 auto name=[&](uint64_t rva){
  std::string text;
  for(size_t i=0;i<260;++i){auto at=offset(rva+i,1);char c;if(!at||!read(*at,&c,1))return false;if(!c){if(text.empty())return false;
    std::transform(text.begin(),text.end(),text.begin(),[](unsigned char v){return static_cast<char>(v>='A'&&v<='Z'?v+('a'-'A'):v);});
    dx|=text=="dxgi.dll"||text=="d3d11.dll"||text=="d3d12.dll";vulkan|=text=="vulkan-1.dll";return true;}
   if(static_cast<unsigned char>(c)<32||static_cast<unsigned char>(c)>=127||c=='/'||c=='\\'||c==':')return false;text+=c;
  }return false;
 };
 auto directory=[&](IMAGE_DATA_DIRECTORY entry,bool delay){
  if(!entry.VirtualAddress&&!entry.Size)return true;
  const size_t stride=delay?sizeof(DWORD)*8:sizeof(IMAGE_IMPORT_DESCRIPTOR);
  if(!entry.VirtualAddress||entry.Size<stride||!offset(entry.VirtualAddress,entry.Size))return false;
  for(uint64_t used=0;used+stride<=entry.Size;used+=stride){
   // Bound metadata work for malformed/adversarial tables. Exceeding this
   // diagnostic budget keeps manual choice available; it never rejects a game.
   if(used/stride>=4096)return false;
   auto at=offset(uint64_t(entry.VirtualAddress)+used,stride);std::array<DWORD,8> descriptor{};
   if(!at||!read(*at,descriptor.data(),stride))return false;
   if(std::all_of(descriptor.begin(),descriptor.begin()+stride/sizeof(DWORD),[](DWORD v){return !v;}))return true;
   uint64_t rva=descriptor[delay?1:3];if(!rva)return false;
   if(delay){if(descriptor[0]&~DWORD(1))return false;if(!(descriptor[0]&1)){if(rva<imageBase)return false;rva-=imageBase;}}
   if(!name(rva))return false;
  }return false;
 };
 if(!directory(imports,false)||!directory(delays,true)||dx==vulkan)return {};
 return dx?"dxgi.dll":"winmm.dll";
}
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
  if(header.SizeOfOptionalHeader<sizeof(WORD))return target;
  auto optionalStart=stream.tellg();stream.seekg(0,std::ios::end);auto bytes=stream.tellg();
  if(bytes<optionalStart+std::streamoff(header.SizeOfOptionalHeader+header.NumberOfSections*sizeof(IMAGE_SECTION_HEADER)))return target;
  stream.seekg(optionalStart);WORD magic{};if(!stream.read(reinterpret_cast<char*>(&magic),sizeof(magic)))return target;
  const bool pe32=magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC,pe64=magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC;
  if((!pe32&&!pe64)||header.SizeOfOptionalHeader<(pe32?sizeof(IMAGE_OPTIONAL_HEADER32):sizeof(IMAGE_OPTIONAL_HEADER64)))return target;
  if((header.Machine==IMAGE_FILE_MACHINE_I386&&!pe32)||(header.Machine==IMAGE_FILE_MACHINE_AMD64&&!pe64))return target;
  target.path=Utf8(absolute.wstring());target.suitable=true;target.bitness=pe32?32:64;target.machine=header.Machine;
  target.proxyRecommendation=ImportedProxy(stream,static_cast<std::streamoff>(optionalStart),header,pe32,static_cast<uint64_t>(bytes));
  target.reason=header.Machine==IMAGE_FILE_MACHINE_I386?Neurotic::UiLiteral("desktop.installation.architecture_warning", "32-bit executable. This 64-bit package cannot provide in-game integration. Use NR Anything until a compatible integration is available."):header.Machine==IMAGE_FILE_MACHINE_AMD64?Neurotic::UiLiteral("desktop.manuallibrary.compatible_x64_executable_confirm_this_is_the_ga_10dba3ac", "Compatible x64 executable. Confirm this is the game, not its launcher."):Neurotic::UiLiteral("desktop.installation.other_architecture_warning", "Executable architecture differs from this package. If compatible in-game integration is unavailable, use NR Anything.");
 }catch(const std::exception& e){target.reason=e.what();}
 return target;
}
}
