#define NOMINMAX
#include "nr/semantic/character/CharacterCohort.h"
#include <fstream>
#include <iostream>
using namespace Neurotic::Semantic::Character;
int main(int argc,char** argv){
 if(argc!=2)return 2;auto root=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(root);int failed=0;
 auto check=[&](bool good,const char* text){std::cout<<(good?"PASS ":"FAIL ")<<text<<'\n';failed+=!good;};
 auto write=[](const auto& path,const auto& bytes){std::ofstream file(path,std::ios::binary);file<<bytes;};
 auto files=nlohmann::json::array({{{"path","server.py"},{"bytes",4},{"sha256",CharacterCohort::Digest("safe")}}});auto manifest=nlohmann::json{{"files",files}};
 write(root/L"server.py","safe");write(root/L"DISTRIBUTION-MANIFEST.json",manifest.dump());auto admitted=CharacterCohort::Digest(files.dump());
 {CharacterCohort cohort;cohort.Verify(root,admitted);auto h=CreateFileW((root/L"server.py").c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);check(h==INVALID_HANDLE_VALUE,"AUDIT-RL08: admitted script remains write-locked through lifetime");if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
 write(root/L"foreign.pyc","code");bool extraRefused=false;try{CharacterCohort cohort;cohort.Verify(root,admitted);}catch(...){extraRefused=true;}check(extraRefused,"AUDIT-RL08: extra unlisted bytecode cannot enter the approved import tree");DeleteFileW((root/L"foreign.pyc").c_str());
 write(root/L"server.py","evil");bool refused=false;try{CharacterCohort cohort;cohort.Verify(root,admitted);}catch(...){refused=true;}check(refused,"AUDIT-RL08: changed code rejected before process launch");
 files[0]["sha256"]=CharacterCohort::Digest("evil");manifest["files"]=files;write(root/L"DISTRIBUTION-MANIFEST.json",manifest.dump());refused=false;try{CharacterCohort cohort;cohort.Verify(root,admitted);}catch(...){refused=true;}check(refused,"AUDIT-RL08: modified manifest cannot self-authorize changed code");
 refused=false;try{CharacterCohort cohort;cohort.Verify(root);}catch(...){refused=true;}check(refused,"AUDIT-RL08: production pin rejects unapproved test distribution");
 DeleteFileW((root/L"server.py").c_str());DeleteFileW((root/L"DISTRIBUTION-MANIFEST.json").c_str());RemoveDirectoryW(root.c_str());return failed;
}
