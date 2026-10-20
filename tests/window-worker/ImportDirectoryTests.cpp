// GPL-3.0. Native destination namespace tests; no provider or model load.
#include "../../apps/NeuRoticWindowWorker/control/ImportDirectoryLeases.h"
#include <iostream>
#include <stdexcept>
#include <winioctl.h>
#include <cstring>
static int checks=0;
static void Require(bool v,const char* message){++checks;if(!v)throw std::runtime_error(message);}
struct EmptyDirectory {
    std::filesystem::path path;
    explicit EmptyDirectory(std::filesystem::path p):path(std::move(p)){Require(CreateDirectoryW(path.c_str(),nullptr)!=FALSE,"create owned directory fixture");}
    ~EmptyDirectory(){RemoveDirectoryW(path.c_str());}
};
struct Junction {
    std::filesystem::path path;
    Junction(std::filesystem::path p,const std::filesystem::path& target):path(std::move(p)) {
        Require(CreateDirectoryW(path.c_str(),nullptr)!=FALSE,"create owned junction fixture");
        HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(h==INVALID_HANDLE_VALUE){RemoveDirectoryW(path.c_str());throw std::runtime_error("open fixture junction");}
        auto print=std::filesystem::absolute(target).make_preferred().wstring();auto sub=L"\\??\\"+print;
        auto bytes=(sub.size()+1+print.size()+1)*2;std::vector<uint8_t> data(16+bytes);
        DWORD tag=IO_REPARSE_TAG_MOUNT_POINT;WORD length=WORD(8+bytes),subLength=WORD(sub.size()*2),printOffset=WORD((sub.size()+1)*2),printLength=WORD(print.size()*2);
        std::memcpy(data.data(),&tag,4);std::memcpy(data.data()+4,&length,2);std::memcpy(data.data()+10,&subLength,2);std::memcpy(data.data()+12,&printOffset,2);std::memcpy(data.data()+14,&printLength,2);
        std::memcpy(data.data()+16,sub.c_str(),(sub.size()+1)*2);std::memcpy(data.data()+16+printOffset,print.c_str(),(print.size()+1)*2);DWORD returned=0;
        bool ok=DeviceIoControl(h,FSCTL_SET_REPARSE_POINT,data.data(),DWORD(data.size()),nullptr,0,&returned,nullptr)!=FALSE;CloseHandle(h);
        if(!ok){RemoveDirectoryW(path.c_str());throw std::runtime_error("native fixture junction creation");}
    }
    ~Junction(){RemoveDirectoryW(path.c_str());}
};
int wmain(int,wchar_t** argv)try {
    const auto parent=std::filesystem::path(argv[0]).parent_path();std::string reason;
    EmptyDirectory owned(parent/L"import-directory-fixture");
    const auto root=owned.path/L"Models"/L"hash";
    struct Cleanup {std::filesystem::path p;~Cleanup(){RemoveDirectoryW(p.c_str());RemoveDirectoryW(p.parent_path().c_str());}} cleanup{root};
    {
        nrw::detail::ImportDirectoryLeases lease;Require(lease.Open(root,reason),"create missing managed root top-down under leases");
        auto temporary=root/L"tiny.tmp",published=root/L"tiny.published";
        struct FileCleanup {std::filesystem::path a,b;~FileCleanup(){DeleteFileW(a.c_str());DeleteFileW(b.c_str());}} files{temporary,published};
        HANDLE tiny=CreateFileW(temporary.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        Require(tiny!=INVALID_HANDLE_VALUE,"create tiny import publication fixture");DWORD written=0;
        struct Close {HANDLE h;~Close(){CloseHandle(h);}} close{tiny};
        bool wrote=WriteFile(tiny,"safe",4,&written,nullptr)!=FALSE && written==4 && FlushFileBuffers(tiny)!=FALSE;Require(wrote,"flush tiny import bytes");
        auto attacker=CreateFileW(temporary.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);auto attackError=GetLastError();if(attacker!=INVALID_HANDLE_VALUE)CloseHandle(attacker);
        Require(attacker==INVALID_HANDLE_VALUE && attackError==ERROR_SHARING_VIOLATION,"exact verified temporary rejects write mutation");
        Require(!DeleteFileW(temporary.c_str()) && GetLastError()==ERROR_SHARING_VIOLATION,"exact verified temporary rejects deletion/replacement");
        LARGE_INTEGER zero{};char content[4]{};DWORD read=0;
        Require(SetFilePointerEx(tiny,zero,nullptr,FILE_BEGIN) && ReadFile(tiny,content,4,&read,nullptr) && read==4 && std::memcmp(content,"safe",4)==0,"verify exact retained tiny file before publication");
        BY_HANDLE_FILE_INFORMATION before{},after{};Require(GetFileInformationByHandle(tiny,&before)!=FALSE,"retained temporary file identity");
        bool publication=lease.Publish(tiny,L"tiny.published",reason);std::cout<<"owned publication result="<<publication<<" reason="<<reason<<"\n";
        Require(publication,"publish tiny import while exact directory namespace remains leased");
        Require(GetFileAttributesW(temporary.c_str())==INVALID_FILE_ATTRIBUTES && GetFileAttributesW(published.c_str())!=INVALID_FILE_ATTRIBUTES,"atomic same-directory publication changes only owned filename");
        Require(GetFileInformationByHandle(tiny,&after) && before.nFileIndexHigh==after.nFileIndexHigh && before.nFileIndexLow==after.nFileIndexLow,"published file is the verified exact file object");
        Require(SetFilePointerEx(tiny,zero,nullptr,FILE_BEGIN) && ReadFile(tiny,content,4,&read,nullptr) && read==4 && std::memcmp(content,"safe",4)==0,"published exact bytes remain unchanged under ownership");
        {
            auto collision=root/L"collision.tmp";HANDLE competing=CreateFileW(collision.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            Require(competing!=INVALID_HANDLE_VALUE,"create owned rollback fixture");
            Close own{competing};Require(WriteFile(competing,"evil",4,&written,nullptr) && written==4 && FlushFileBuffers(competing),"flush owned collision bytes");
            Require(!lease.Publish(competing,L"tiny.published",reason),"publication never overwrites existing verified destination");
            Require(nrw::detail::ImportDirectoryLeases::Rollback(competing,reason),"rollback exact failed-copy handle while directory namespace remains protected");
        }
        Require(GetFileAttributesW((root/L"collision.tmp").c_str())==INVALID_FILE_ATTRIBUTES,"rollback retires owned temporary after final handle close");
        Require(SetFilePointerEx(tiny,zero,nullptr,FILE_BEGIN) && ReadFile(tiny,content,4,&read,nullptr) && read==4 && std::memcmp(content,"safe",4)==0,"collision/rollback preserves original published bytes");
        {
            auto alien=owned.path/L"alien.tmp";HANDLE foreign=CreateFileW(alien.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            Require(foreign!=INVALID_HANDLE_VALUE,"create owned foreign-parent fixture");Close own{foreign};
            Require(!lease.Publish(foreign,L"must-not-publish",reason),"same-directory publication refuses a file outside leased parent");
            Require(nrw::detail::ImportDirectoryLeases::Rollback(foreign,reason),"retire owned foreign-parent fixture without path mutation");
        }
        auto moved=root.parent_path()/L"renamed";BOOL renamed=MoveFileExW(root.c_str(),moved.c_str(),0);auto error=GetLastError();
        if(renamed)MoveFileExW(moved.c_str(),root.c_str(),0);
        Require(!renamed && error==ERROR_SHARING_VIOLATION,"import leaf directory remains pinned through publication/rollback");
        renamed=MoveFileExW(root.parent_path().c_str(),(owned.path/L"ancestor-renamed").c_str(),0);error=GetLastError();
        if(renamed)MoveFileExW((owned.path/L"ancestor-renamed").c_str(),root.parent_path().c_str(),0);
        Require(!renamed && error==ERROR_SHARING_VIOLATION,"import ancestor directory cannot be retargeted during transaction");
        HANDLE write=CreateFileW(root.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        error=GetLastError();if(write!=INVALID_HANDLE_VALUE)CloseHandle(write);
        Require(write==INVALID_HANDLE_VALUE && error==ERROR_SHARING_VIOLATION,"import directory mutation handle denied throughout transaction");
    }
    Require(MoveFileExW(root.c_str(),(root.parent_path()/L"renamed").c_str(),0)!=FALSE,"import lease released after transaction");
    Require(MoveFileExW((root.parent_path()/L"renamed").c_str(),root.c_str(),0)!=FALSE,"restore fixture root");
    std::filesystem::remove(root);std::filesystem::remove(root.parent_path());
    EmptyDirectory target(owned.path/L"target");
    {
        Junction junction(owned.path/L"leaf-link",target.path);nrw::detail::ImportDirectoryLeases lease;
        Require(!lease.Open(junction.path,reason),"managed destination leaf junction refused");
    }
    {
        Junction junction(owned.path/L"link",target.path);nrw::detail::ImportDirectoryLeases lease;
        Require(!lease.Open(junction.path/L"must-not-create",reason),"reparse ancestor rejected before creating outside selected namespace");
        Require(!std::filesystem::exists(target.path/L"must-not-create"),"refused import creates no redirected directory");
    }
    std::cout<<"PASS ImportDirectoryTests "<<checks<<" checks\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL ImportDirectoryTests: "<<e.what()<<"\n";return 1;}
