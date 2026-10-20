#pragma once
// Test-launcher isolation only. Never include this in a shipping provider
// selector: provenance binds a local 05-built fixture, not an approved model.
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace DlssNr::AdaptivePublicFixture
{
class BinaryPreflight final
{
    HANDLE file_=INVALID_HANDLE_VALUE,mapping_=nullptr;
    const unsigned char* bytes_=nullptr;
    std::size_t size_=0,sections_=0;unsigned sectionCount_=0,headersSize_=0;
    std::wstring path_;
    static void Require(bool pass,const char* why){if(!pass)throw std::runtime_error(why);}
    bool Range(std::size_t at,std::size_t count)const{return at<=size_&&count<=size_-at;}
    template<class T>T Read(std::size_t at)const
    {Require(Range(at,sizeof(T)),"fixture PE truncated");T value{};std::memcpy(&value,bytes_+at,sizeof(value));return value;}
    struct RawMapping{std::size_t offset,available;};
    RawMapping MapRva(std::uint32_t address,std::size_t count)const
    {
        if(address<headersSize_&&count<=std::size_t(headersSize_)-address&&Range(address,count))
            return {address,(std::min)(std::size_t(headersSize_)-address,size_-address)};
        for(unsigned i=0;i<sectionCount_;++i){const auto s=Read<IMAGE_SECTION_HEADER>(sections_+i*sizeof(IMAGE_SECTION_HEADER));
            if(address<s.VirtualAddress)continue;const std::uint64_t delta=std::uint64_t(address)-s.VirtualAddress;
            if(delta>s.SizeOfRawData||count>std::uint64_t(s.SizeOfRawData)-delta)continue;
            const std::uint64_t at=std::uint64_t(s.PointerToRawData)+delta;
            if(at<=size_&&count<=size_-std::size_t(at))
                return {std::size_t(at),(std::min)(std::size_t(s.SizeOfRawData-delta),size_-std::size_t(at))};
        }
        throw std::runtime_error("fixture PE unmapped RVA");
    }
    std::size_t Rva(std::uint32_t address,std::size_t count)const{return MapRva(address,count).offset;}
    bool Executable(std::uint32_t address)const
    {
        for(unsigned i=0;i<sectionCount_;++i){const auto s=Read<IMAGE_SECTION_HEADER>(sections_+i*sizeof(IMAGE_SECTION_HEADER));
            if(address>=s.VirtualAddress&&std::uint64_t(address)-s.VirtualAddress<s.SizeOfRawData&&
               (s.Characteristics&IMAGE_SCN_MEM_EXECUTE)){(void)Rva(address,1);return true;}}
        return false;
    }
    std::string_view ExportName(std::uint32_t address)const
    {
        // Offset and scan extent must come from the same selected raw mapping.
        // Header metadata may cover an RVA whose bytes map only in a section.
        const auto mapping=MapRva(address,1);const auto at=mapping.offset;
        const auto* end=static_cast<const unsigned char*>(std::memchr(bytes_+at,0,mapping.available));
        Require(end!=nullptr,"fixture export name unterminated");
        return {reinterpret_cast<const char*>(bytes_+at),std::size_t(end-(bytes_+at))};
    }
    void Close()noexcept
    {if(bytes_)UnmapViewOfFile(bytes_);if(mapping_)CloseHandle(mapping_);if(file_!=INVALID_HANDLE_VALUE)CloseHandle(file_);bytes_=nullptr;mapping_=nullptr;file_=INVALID_HANDLE_VALUE;}
public:
    BinaryPreflight(const BinaryPreflight&)=delete;
    BinaryPreflight& operator=(const BinaryPreflight&)=delete;
    explicit BinaryPreflight(const wchar_t* file)
    {
        try{
            Require(file&&std::filesystem::path(file).is_absolute(),"fixture binary path must be absolute");
            // No write/delete sharing: keep the verified file stable through
            // subsequent executable loading. Map ordinary file data, never an
            // image or a DLL, so inspected DllMain cannot run here.
            file_=CreateFileW(file,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            Require(file_!=INVALID_HANDLE_VALUE,"fixture binary read/pin failed");LARGE_INTEGER length{};
            Require(GetFileSizeEx(file_,&length)&&length.QuadPart>0&&
                std::uint64_t(length.QuadPart)<=(std::numeric_limits<std::size_t>::max)(),"fixture binary size invalid");
            size_=std::size_t(length.QuadPart);
            mapping_=CreateFileMappingW(file_,nullptr,PAGE_READONLY,0,0,nullptr);Require(mapping_!=nullptr,"fixture data mapping failed");
            bytes_=static_cast<const unsigned char*>(MapViewOfFile(mapping_,FILE_MAP_READ,0,0,0));Require(bytes_!=nullptr,"fixture data view failed");
            const DWORD needed=GetFinalPathNameByHandleW(file_,nullptr,0,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
            Require(needed>0,"fixture final path unavailable");std::vector<wchar_t> path(needed+1);
            const DWORD written=GetFinalPathNameByHandleW(file_,path.data(),DWORD(path.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
            Require(written&&written<path.size(),"fixture final path truncated");path_.assign(path.data(),written);
        }catch(...){Close();throw;}
    }
    ~BinaryPreflight(){Close();}
    const wchar_t* Path()const{return path_.c_str();}
    void RequireExports(std::span<const char* const> required)
    {
        const auto dos=Read<IMAGE_DOS_HEADER>(0);Require(dos.e_magic==IMAGE_DOS_SIGNATURE&&dos.e_lfanew>=LONG(sizeof(dos)),"fixture PE DOS header invalid");
        const auto pe=std::size_t(dos.e_lfanew);Require(Read<DWORD>(pe)==IMAGE_NT_SIGNATURE,"fixture PE signature invalid");
        const auto coff=Read<IMAGE_FILE_HEADER>(pe+sizeof(DWORD));
        Require(coff.Machine==IMAGE_FILE_MACHINE_AMD64&&(coff.Characteristics&IMAGE_FILE_DLL),"fixture requires an x64 DLL");
        const auto optional=pe+sizeof(DWORD)+sizeof(coff);
        Require(coff.SizeOfOptionalHeader>=120&&Range(optional,coff.SizeOfOptionalHeader)&&Read<WORD>(optional)==IMAGE_NT_OPTIONAL_HDR64_MAGIC,"fixture PE64 optional header invalid");
        Require(Read<DWORD>(optional+108)>0,"fixture exports absent");headersSize_=Read<DWORD>(optional+60);
        sections_=optional+coff.SizeOfOptionalHeader;sectionCount_=coff.NumberOfSections;
        Require(sectionCount_&&Range(sections_,std::size_t(sectionCount_)*sizeof(IMAGE_SECTION_HEADER)),"fixture sections truncated");
        const auto directory=Read<IMAGE_DATA_DIRECTORY>(optional+112);
        Require(directory.VirtualAddress&&directory.Size>=sizeof(IMAGE_EXPORT_DIRECTORY),"fixture exports absent");
        const auto exports=Read<IMAGE_EXPORT_DIRECTORY>(Rva(directory.VirtualAddress,sizeof(IMAGE_EXPORT_DIRECTORY)));
        Require(exports.NumberOfNames&&exports.NumberOfFunctions,"fixture named exports absent");
        const auto names=Rva(exports.AddressOfNames,std::size_t(exports.NumberOfNames)*sizeof(DWORD));
        const auto ordinals=Rva(exports.AddressOfNameOrdinals,std::size_t(exports.NumberOfNames)*sizeof(WORD));
        const auto functions=Rva(exports.AddressOfFunctions,std::size_t(exports.NumberOfFunctions)*sizeof(DWORD));
        struct NamedExport{std::string_view name;DWORD function;};
        std::vector<NamedExport> declared;declared.reserve(exports.NumberOfNames);
        // Validate every declared name and ordinal before reporting a missing
        // required export. Malformed unrelated entries must not pass the
        // ordinary-product rejection test, even after all required names match.
        for(DWORD i=0;i<exports.NumberOfNames;++i){
            const auto name=ExportName(Read<DWORD>(names+std::size_t(i)*sizeof(DWORD)));
            const auto ordinal=Read<WORD>(ordinals+std::size_t(i)*sizeof(WORD));
            Require(ordinal<exports.NumberOfFunctions,"fixture export ordinal invalid");
            declared.push_back({name,Read<DWORD>(functions+std::size_t(ordinal)*sizeof(DWORD))});
        }
        for(const auto* wanted:required){bool found=false;
            for(const auto& entry:declared){
                if(entry.name!=wanted)continue;
                const auto function=entry.function;
                Require(function&&(function<directory.VirtualAddress||std::uint64_t(function)>=std::uint64_t(directory.VirtualAddress)+directory.Size)&&Executable(function),"fixture export must be local executable code");
                found=true;break;
            }
            Require(found,"required fixture export absent; executable loading refused");
        }
    }
    std::string Sha256()const
    {
        BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
        // The CNG hash owns a reference to this buffer until DestroyHash.
        // Declare it before Cleanup so the handle is destroyed first.
        std::vector<unsigned char> object;
        struct Cleanup{BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~Cleanup(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}}cleanup{algorithm,hash};
        Require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"fixture SHA256 provider");
        DWORD objectSize=0,written=0;Require(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectSize),sizeof(objectSize),&written,0)>=0&&written==sizeof(objectSize),"fixture hash object size");
        object.resize(objectSize);Require(BCryptCreateHash(algorithm,&hash,object.data(),objectSize,nullptr,0,0)>=0,"fixture hash create");
        for(std::size_t at=0;at<size_;){const auto count=ULONG((std::min)(size_-at,std::size_t(1024*1024)));
            Require(BCryptHashData(hash,const_cast<PUCHAR>(bytes_+at),count,0)>=0,"fixture hash data");at+=count;}
        std::array<unsigned char,32> digest{};Require(BCryptFinishHash(hash,digest.data(),ULONG(digest.size()),0)>=0,"fixture hash finish");
        constexpr char hex[]="0123456789abcdef";std::string result;result.reserve(64);
        for(auto byte:digest){result+=hex[byte>>4];result+=hex[byte&15];}return result;
    }
    void RequireProvenance(const wchar_t* receipt,std::string_view role,std::string_view sourceManifest)const
    {
        const auto validHash=[](std::string_view text){if(text.size()!=64)return false;for(char c:text)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;};
        Require(validHash(sourceManifest),"fixture expected source manifest invalid");
        std::ifstream stream(std::filesystem::path(receipt),std::ios::binary);Require(bool(stream),"05 fixture build provenance absent");
        std::array<char,4097> bytes{};stream.read(bytes.data(),4097);const auto count=stream.gcount();
        Require(count>0&&count<=4096&&!stream.bad(),"fixture provenance unreadable or oversized");
        const std::string expected="NR_ADAPTIVE_PUBLIC_FIXTURE_BUILD_V1\nrole="+std::string(role)+
            "\nbinary_sha256="+Sha256()+"\nsource_manifest_sha256="+std::string(sourceManifest)+
            "\nflags=NR_ADAPTIVE_PUBLIC_PROVIDER_FIXTURE=1;NR_GPU_SAFETY_TEST\nall_translation_units_verified=1\n";
        Require(std::string_view(bytes.data(),std::size_t(count))==expected,"fixture build provenance mismatch; executable loading refused");
    }
};
inline constexpr std::array<const char*,3> ProductFixtureExports{
    "NR_AdaptiveFixtureInitializeRuntime","NR_AdaptiveFixtureBindPublicProvider","NR_AdaptiveFixtureConfigureNativePostSr"};
inline constexpr std::array<const char*,3> ProviderFixtureExports{
    "nr_adaptive_public_fixture_identity_v1","nr_adaptive_public_fixture_stats_v1","dlssnr_call_evaluate_guides_v4"};
}
