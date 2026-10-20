#include "ManualLibrary.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <vector>
int main(){
 auto root=std::filesystem::absolute(L"builds/universal-connections/x86-ui/fixtures");
 std::filesystem::create_directories(root);int failed=0;
 auto pe=[&](const wchar_t* name,WORD machine,WORD magic,WORD flags=2,bool truncate=false){
  std::vector<char> bytes(512);auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=64;
  *reinterpret_cast<DWORD*>(bytes.data()+64)=IMAGE_NT_SIGNATURE;auto header=reinterpret_cast<IMAGE_FILE_HEADER*>(bytes.data()+68);
  header->Machine=machine;header->NumberOfSections=1;header->SizeOfOptionalHeader=magic==0x10b?224:240;header->Characteristics=flags;
  *reinterpret_cast<WORD*>(bytes.data()+88)=magic;auto path=root/name;
  std::ofstream file(path,std::ios::binary);file.write(bytes.data(),truncate?90:bytes.size());return path;
 };
 auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';failed+=!ok;};
 auto x86=pe(L"Portal2.exe",0x14c,0x10b);check(nh::InspectExecutable(x86).suitable&&nh::InspectExecutable(x86).bitness==32,"manual PE32 i386 game accepted with bitness");
 auto x64=pe(L"Game64.exe",0x8664,0x20b);check(nh::InspectExecutable(x64).suitable&&nh::InspectExecutable(x64).bitness==64,"manual PE32+ AMD64 preserved with bitness");
 check(!nh::InspectExecutable(pe(L"Dll.exe",0x14c,0x10b,0x2002)).suitable,"DLL disguised as EXE refused");
 check(!nh::InspectExecutable(pe(L"Mismatch.exe",0x8664,0x10b)).suitable,"machine and magic mismatch refused");
 check(!nh::InspectExecutable(pe(L"Arm.exe",0xaa64,0x20b)).suitable,"ARM64 refused");
 check(!nh::InspectExecutable(pe(L"Truncated.exe",0x8664,0x20b,2,true)).suitable,"truncated optional header refused");
 check(!nh::InspectExecutable(pe(L"NotExecutable.exe",0x14c,0x10b,0)).suitable,"non executable image refused");
 for(auto& entry:std::filesystem::directory_iterator(root))std::filesystem::remove(entry.path());std::filesystem::remove(root);
 return failed?1:0;
}
