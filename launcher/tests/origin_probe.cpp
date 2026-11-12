#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>
int wmain(){
    wchar_t path[32768]{};if(!GetEnvironmentVariableW(L"FROGSHELL_ORIGIN_PROBE_REPORT",path,32768))return 90;
    FILE *file=nullptr;if(_wfopen_s(&file,path,L"wb")||!file)return 91;
    const auto block=GetEnvironmentStringsW();if(!block){std::fclose(file);return 92;}
    std::vector<std::wstring> rows;
    for(auto cursor=block;*cursor;cursor+=std::wcslen(cursor)+1){
        const std::wstring row(cursor);auto upper=row;std::transform(upper.begin(),upper.end(),upper.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towupper(c));});if(upper.starts_with(L"RABBITSHELL_PORTABLE_"))rows.push_back(row);
    }
    FreeEnvironmentStringsW(block);
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    if(snapshot!=INVALID_HANDLE_VALUE){if(Process32FirstW(snapshot,&entry))do{if(entry.th32ProcessID==GetCurrentProcessId()){rows.push_back(L"PROBE_PARENT_PID="+std::to_wstring(entry.th32ParentProcessID));break;}}while(Process32NextW(snapshot,&entry));CloseHandle(snapshot);}
    bool okay=true;for(const auto &row:rows){const auto bytes=static_cast<unsigned long>(row.size()*sizeof(wchar_t));okay=std::fwrite(&bytes,sizeof(bytes),1,file)==1&&std::fwrite(row.data(),1,bytes,file)==bytes&&okay;}
    std::fclose(file);return okay?0:93;
}
