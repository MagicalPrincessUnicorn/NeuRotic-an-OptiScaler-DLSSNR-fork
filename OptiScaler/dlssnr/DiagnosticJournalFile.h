#pragma once
#include <windows.h>
#include <string>

// A launcher and its game may inherit one capture directory. Keep the legacy
// first journal, then use a process-qualified name without replacing evidence.
inline HANDLE OpenDiagnosticJournal(const std::wstring& directory,const std::wstring& filename,
                                    DWORD processId=GetCurrentProcessId())
{
 const auto open=[&](const std::wstring& name){return CreateFileW((directory+L"\\"+name).c_str(),
     GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);};
 auto file=open(filename);
 if(file!=INVALID_HANDLE_VALUE)return file;
 const auto error=GetLastError();
 if(error!=ERROR_FILE_EXISTS&&error!=ERROR_ALREADY_EXISTS)return INVALID_HANDLE_VALUE;
 const auto dot=filename.find_last_of(L'.');
 const auto stem=dot==std::wstring::npos?filename:filename.substr(0,dot);
 const auto extension=dot==std::wstring::npos?L"":filename.substr(dot);
 return open(stem+L"-"+std::to_wstring(processId)+extension);
}
