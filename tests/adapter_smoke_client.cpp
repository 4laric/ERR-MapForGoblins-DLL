// Diagnostic DLL only. Load via a separate offline me3 test profile WITHOUT
// the real AP client. It calls only public ABI functions, never game memory.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../include/mfg_ap_readonly_v1.h"
#include <cstdio>
#include <filesystem>
static HMODULE self;
static DWORD WINAPI run(void*) {
    wchar_t path[32768];
    if(!GetModuleFileNameW(self,path,32768))return 1;
    const auto dir=std::filesystem::path(path).parent_path();
    const auto ini=(dir/L"smoke.ini").wstring();
    unsigned previous=99;
    for(;;) {
        const auto module=GetModuleHandleW(L"MapForGoblins.dll");
        const auto query=reinterpret_cast<MFG_AP_QueryV1>(GetProcAddress(module,"MFG_AP_QUERY_V1"));
        const auto copy=reinterpret_cast<MFG_AP_CopyHoverV1>(GetProcAddress(module,"MFG_AP_COPY_HOVER_V1"));
        const auto set=reinterpret_cast<MFG_AP_SetCheckStatesV1>(GetProcAddress(module,"MFG_AP_SET_CHECK_STATES_V1"));
        if(query&&copy&&set) {
            MFG_AP_InfoV1 info{}; MFG_AP_HoverV1 hover{};
            const auto q=query(1,&info,sizeof(info)), h=copy(&hover,sizeof(hover));
            const auto mode=GetPrivateProfileIntW(L"Test",L"mode",0,ini.c_str());
            uint32_t result=99;
            if(mode==0 && previous!=0)result=set(1,nullptr,0,0);
            if(mode==1)result=set(1,nullptr,0,2000);
            if(mode==2) {
                MFG_AP_CheckStateV1 entry{
                    GetPrivateProfileIntW(L"Test",L"table",1,ini.c_str()),
                    GetPrivateProfileIntW(L"Test",L"lot",0,ini.c_str()),
                    GetPrivateProfileIntW(L"Test",L"flags",1,ini.c_str())};
                result=set(1,&entry,1,2000);
            }
            // mode 3 deliberately stops renewing; exercise lease expiry.
            previous=mode;
            FILE* out{};
            if(_wfopen_s(&out,(dir/L"smoke.log").c_str(),L"a")==0 && out) {
                std::fprintf(out,"%llu mode=%u q=%u cap=%u copy=%u hover=%u gen=%llu handle=%llu table=%u lot=%u age=%u set=%u\n",
                    GetTickCount64(),mode,q,info.capabilities,h,hover.status,hover.generation,hover.handle,hover.lot_table,hover.lot_row,hover.age_ms,result);
                std::fclose(out);
            }
        }
        Sleep(500);
    }
}
BOOL APIENTRY DllMain(HMODULE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        self=module; DisableThreadLibraryCalls(module);
        if(auto h=CreateThread(nullptr,0,run,nullptr,0,nullptr))CloseHandle(h);
    }
    return TRUE;
}
