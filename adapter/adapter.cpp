// Version-pinned companion: upstream owns all game hooks and native children.
// This module hooks upstream's own functions, never guesses game-version RVAs.
#include <windows.h>
#include <bcrypt.h>
#include <MinHook.h>
#include "release213.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>

namespace {
HMODULE self_module{};
unsigned char* upstream{};
std::atomic<bool> ready{false};
std::atomic<unsigned> options{0};
mfg213::Identities identities;
goblin::ap::HoverCache cache;
std::mutex catalog_mutex;
uintptr_t catalog_begin{}, catalog_end{};
std::filesystem::path directory;
uint64_t millis() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
void log(const char* text) {
    FILE* file{};
    if (_wfopen_s(&file,(directory/L"MapForGoblins.AP.log").c_str(),L"a")==0 && file) {
        std::fprintf(file,"%llu %s\n",static_cast<unsigned long long>(GetTickCount64()),text);
        std::fclose(file);
    }
}
bool copy_memory(void* dst,const void* src,size_t size) {
    __try { std::memcpy(dst,src,size); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> T read_global(size_t rva) {
    T v{}; if (!copy_memory(&v,upstream+rva,sizeof(v))) throw std::runtime_error("upstream global unreadable");
    return v;
}
void install_catalog() {
    std::lock_guard lock(catalog_mutex);
    const auto first=read_global<uintptr_t>(0x66ff48), last=read_global<uintptr_t>(0x66ff50);
    if (first==catalog_begin && last==catalog_end) return;
    if (!first || last<first || (last-first)%112 || (last-first)/112!=mfg213::entry_count)
        throw std::runtime_error("upstream catalogue not ready");
    std::vector<unsigned char> bytes(last-first);
    if (!copy_memory(bytes.data(),reinterpret_cast<void*>(first),bytes.size()))
        throw std::runtime_error("upstream catalogue unreadable");
    std::vector<goblin::ap::MarkerIdentity> rows;
    rows.reserve(mfg213::entry_count);
    for (size_t i=0;i<bytes.size();i+=112) {
        const auto row=mfg213::field<uintptr_t>(bytes.data()+i,0);
        const auto key=mfg213::field<uint64_t>(bytes.data()+i,24);
        const auto* id=mfg213::find(identities,key);
        if (!row || !id || id->handle!=key) throw std::runtime_error("unmatched catalogue identity");
        rows.push_back({row,id->handle,id->table,id->lot});
    }
    if (!cache.install_rows(rows)) throw std::runtime_error("catalogue identities rejected");
    catalog_begin=first; catalog_end=last;
    log("7039 upstream catalogue identities installed");
}
struct Scope {
    std::shared_ptr<const goblin::ap::CheckStateSnapshot> checks;
    unsigned flags;
    Scope():checks(cache.active_check_states(millis())),flags(options.load()){}
};
thread_local const Scope* current_scope{};
struct ScopeGuard {
    Scope scope;
    const Scope* previous=current_scope;
    ScopeGuard(){current_scope=&scope;}
    ~ScopeGuard(){current_scope=previous;}
};
bool permits(uint64_t key) {
    if (current_scope) {
        if (!current_scope->checks || !current_scope->flags) return true;
        return mfg213::allowed(mfg213::find(identities,key),current_scope->checks.get(),current_scope->flags);
    }
    Scope scope;
    if (!scope.checks || !scope.flags) return true;
    return mfg213::allowed(mfg213::find(identities,key),scope.checks.get(),scope.flags);
}
using Predicate=bool(*)(void*,int,int);
Predicate original_predicate{};
bool predicate(void* category,int layer,int focus) {
    if (!original_predicate(category,layer,focus)) return false;
    uint64_t key{};
    if (!copy_memory(&key,static_cast<unsigned char*>(category)+24,sizeof(key))) return false;
    try { return permits(key); } catch (...) { return false; }
}
struct PointVector { unsigned char* first; unsigned char* last; unsigned char* capacity; };
using Points=PointVector*(*)(PointVector*,int,bool);
Points original_points{};
PointVector* points(PointVector* out,int layer,bool all) {
    ScopeGuard scope;
    auto* result=original_points(out,layer,all);
    // Keep the allocation and cardinality upstream owns. Only intersect its
    // visible byte, including the companion completion-badge identity.
    if (scope.scope.checks && scope.scope.flags && out->first && out->last>=out->first && (out->last-out->first)%40==0 &&
        static_cast<size_t>(out->last-out->first)<=20000*40) {
        for (auto* p=out->first;p!=out->last;p+=40)
            if (!permits(mfg213::field<uint64_t>(p,0))) p[28]=0;
    }
    return result;
}
using Hover=void*(*)(void*,void*,void*);
Hover original_hover{};
void* hover(void* panel,void* pin,void* area) {
    // Catalogue reads and cache publication occur on the map callback thread.
    bool catalog_ok=true;
    try { install_catalog(); } catch (...) { catalog_ok=false; cache.set_hooks_ready(false); }
    ScopeGuard scope;
    auto* result=original_hover(panel,pin,area);
    try {
        const auto row=read_global<uintptr_t>(0x68e120);
        cache.set_hooks_ready(catalog_ok && ready.load());
        cache.observe(row,millis());
    } catch (...) { cache.set_hooks_ready(false); }
    return result;
}
using Build=void*(*)(void*,void*,void*,void*);
Build original_build{};
void* build(void* a,void* b,void* c,void* d) {
    cache.map_rebuild();
    auto* result=original_build(a,b,c,d);
    try { install_catalog(); } catch (...) { cache.set_hooks_ready(false); }
    return result;
}
using Close=void*(*)(void*);
Close original_close{};
void* close_map(void* map) {
    cache.map_rebuild();
    return original_close(map);
}
std::string hash_file(HANDLE file) {
    BCRYPT_ALG_HANDLE alg{}; BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
        throw std::runtime_error("SHA256 provider unavailable");
    struct Cleanup { BCRYPT_ALG_HANDLE a; BCRYPT_HASH_HANDLE& h; ~Cleanup(){if(h)BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(a,0);} } cleanup{alg,hash};
    if (BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)<0) throw std::runtime_error("SHA256 create failed");
    unsigned char block[65536]; DWORD got;
    for (;;) {
        if (!ReadFile(file,block,sizeof(block),&got,nullptr)) throw std::runtime_error("DLL read failed");
        if (!got) break;
        if (BCryptHashData(hash,block,got,0)<0) throw std::runtime_error("SHA256 update failed");
    }
    unsigned char digest[32];
    if (BCryptFinishHash(hash,digest,sizeof(digest),0)<0) throw std::runtime_error("SHA256 finish failed");
    std::string hex;
    for (auto b:digest) {hex.push_back("0123456789abcdef"[b>>4]);hex.push_back("0123456789abcdef"[b&15]);}
    return hex;
}
void load_options() {
    const auto file=(directory/L"MapForGoblins.AP.ini").wstring();
    unsigned next=0;
    if(GetPrivateProfileIntW(L"AP",L"checks_only",1,file.c_str()))next|=1;
    if(GetPrivateProfileIntW(L"AP",L"progression_only",0,file.c_str()))next|=2;
    if(GetPrivateProfileIntW(L"AP",L"in_logic_only",1,file.c_str()))next|=4;
    if(options.exchange(next)!=next) log("AP filter options reloaded");
}
DWORD WINAPI start(void*) {
    // me3 loads these for the process lifetime. Pin the worker's code so an
    // accidental FreeLibrary cannot leave callbacks/worker executing freed code.
    HMODULE pinned{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&start),&pinned)) return 1;
    wchar_t path[32768];
    const DWORD length=GetModuleFileNameW(self_module,path,32768);
    if(!length || length>=32768)return 1;
    directory=std::filesystem::path(path).parent_path();
    try {
        const auto dll=directory/L"MapForGoblins.upstream.dll";
        HANDLE file=CreateFileW(dll.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open MapForGoblins.upstream.dll");
        struct FileClose {HANDLE h;~FileClose(){CloseHandle(h);}} close{file};
        if(hash_file(file)!=mfg213::sha256)throw std::runtime_error("Unsupported upstream SHA256; no DLL loaded or hooks installed");
        // Keep the read handle denying writes/deletes through verification/load.
        upstream=reinterpret_cast<unsigned char*>(LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS));
        if(!upstream)throw std::runtime_error("Upstream DLL load failed");
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(upstream),&pinned))throw std::runtime_error("Upstream lifetime pin failed");
        identities=mfg213::identities({upstream,0x6c0000});
        load_options();
        if(MH_Initialize()!=MH_OK)throw std::runtime_error("Adapter MinHook initialization failed");
        struct Hook {size_t rva;void* hook;void** original;};
        Hook hooks[]{
            {0x43a10,reinterpret_cast<void*>(predicate),reinterpret_cast<void**>(&original_predicate)},
            {0x444b0,reinterpret_cast<void*>(points),reinterpret_cast<void**>(&original_points)},
            {0xcdfb0,reinterpret_cast<void*>(hover),reinterpret_cast<void**>(&original_hover)},
            {0xcc390,reinterpret_cast<void*>(build),reinterpret_cast<void**>(&original_build)},
            {0x8f390,reinterpret_cast<void*>(close_map),reinterpret_cast<void**>(&original_close)}};
        for(auto& h:hooks) {
            if(MH_CreateHook(upstream+h.rva,h.hook,h.original)!=MH_OK) {
                MH_Uninitialize(); throw std::runtime_error("Adapter hook creation failed; none enabled");
            }
        }
        cache.set_active(true);
        ready.store(true);
        if(MH_EnableHook(MH_ALL_HOOKS)!=MH_OK) {
            ready.store(false); cache.set_hooks_ready(false); MH_DisableHook(MH_ALL_HOOKS);
            throw std::runtime_error("Adapter hook enable failed");
        }
        log("Pinned 2.1.3 loaded; five AP hooks armed; game validation required");
        // Resident DLL. Only configuration file I/O occurs on this worker.
        for(;;){Sleep(1000);load_options();}
    } catch(const std::exception& e) { log(e.what()); }
    catch(...) {log("Adapter initialization failed");}
    return 1;
}
}

#define API extern "C" __declspec(dllexport) uint32_t __cdecl
API MFG_AP_QUERY_V1(uint32_t abi,MFG_AP_InfoV1* out,uint32_t capacity) {
    if(!ready.load())return MFG_AP_UNAVAILABLE;
    return cache.query(abi,out,capacity);
}
API MFG_AP_COPY_HOVER_V1(MFG_AP_HoverV1* out,uint32_t capacity) {
    if(!ready.load())return MFG_AP_UNAVAILABLE;
    return cache.copy(out,capacity,millis());
}
API MFG_AP_SET_CHECK_STATES_V1(uint32_t abi,const MFG_AP_CheckStateV1* entries,uint32_t count,uint32_t lease) {
    if(!ready.load())return MFG_AP_UNAVAILABLE;
    try{return cache.set_check_states(abi,entries,count,lease,millis());}catch(...){return MFG_AP_UNAVAILABLE;}
}
API MFG_AP_SET_LOT_STYLES_V1(uint32_t abi,const MFG_AP_LotStyleV1* entries,uint32_t count,uint32_t lease) {
    if(!ready.load())return MFG_AP_UNAVAILABLE;
    try{return cache.set_lot_styles(abi,entries,count,lease,millis());}catch(...){return MFG_AP_UNAVAILABLE;}
}
BOOL APIENTRY DllMain(HMODULE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        self_module=module;
        DisableThreadLibraryCalls(module);
        HANDLE worker=CreateThread(nullptr,0,start,nullptr,0,nullptr);
        if(worker)CloseHandle(worker);else return FALSE;
    }
    return TRUE;
}
