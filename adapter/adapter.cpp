// Version-pinned companion: upstream owns all game hooks and native children.
// This module hooks upstream's own functions, never guesses game-version RVAs.
#include <windows.h>
#include <bcrypt.h>
#include <MinHook.h>
#include <intrin.h>
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
std::mutex options_mutex;
std::atomic<uint64_t> last_view_counts{0};
std::atomic<bool> have_view_counts{false};
std::atomic<bool> settings_save_failed{false};
struct Diagnostic {
    mfg213::FilterCounts counts;
    unsigned flags{};
    int layer{};
    bool active{};
    size_t supplied{};
    bool operator==(const Diagnostic&) const = default;
};
std::mutex diagnostic_mutex;
Diagnostic diagnostic;
bool diagnostic_ready=false;
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
    uint32_t native_candidates=0;
    bool count_candidates=false;
    mfg213::FilterCounts counts;
    Scope():checks(cache.active_check_states(millis())),flags(options.load()){}
};
thread_local Scope* current_scope{};
struct ScopeGuard {
    Scope scope;
    Scope* previous=current_scope;
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
    const bool native=original_predicate(category,layer,focus);
    if(!native) {
        if(current_scope && current_scope->count_candidates)
            current_scope->counts.observe(nullptr,current_scope->checks.get(),current_scope->flags,false);
        return false;
    }
    if (current_scope && current_scope->count_candidates) ++current_scope->native_candidates;
    uint64_t key{};
    if (!copy_memory(&key,static_cast<unsigned char*>(category)+24,sizeof(key))) return false;
    if(current_scope && current_scope->count_candidates)
        current_scope->counts.observe(mfg213::find(identities,key),current_scope->checks.get(),current_scope->flags,true);
    try { return permits(key); } catch (...) { return false; }
}
struct PointVector { unsigned char* first; unsigned char* last; unsigned char* capacity; };
using Points=PointVector*(*)(PointVector*,int,bool);
Points original_points{};
PointVector* points(PointVector* out,int layer,bool all) {
    ScopeGuard scope;
    scope.scope.count_candidates=true;
    auto* result=original_points(out,layer,all);
    // Keep the allocation and cardinality upstream owns. Only intersect its
    // visible byte, including the companion completion-badge identity.
    if (out->first && out->last>=out->first && (out->last-out->first)%40==0 &&
        static_cast<size_t>(out->last-out->first)<=20000*40) {
        uint32_t visible=0;
        for (auto* p=out->first;p!=out->last;p+=40) {
            const auto key=mfg213::field<uint64_t>(p,0);
            if(scope.scope.checks && scope.scope.flags && !permits(key))p[28]=0;
            if(p[28] && !(key>>63))++visible;
        }
        if(!all) {
            last_view_counts.store((uint64_t{scope.scope.native_candidates}<<32)|visible);
            have_view_counts.store(true);
            std::lock_guard lock(diagnostic_mutex);
            diagnostic={scope.scope.counts,scope.scope.flags,layer,
                static_cast<bool>(scope.scope.checks),scope.scope.checks?scope.scope.checks->flags.size():0};
            diagnostic_ready=true;
        }
    }
    return result;
}

// These are upstream's own ImGui widgets, called only inside its Settings tab.
// No second ImGui context, graphics device, window or input hook is created.
using DrawSettings=void(*)();
using TextWrapped=void(*)(const char*,...);
using Checkbox=bool(*)(const char*,bool*);
using SliderScalar=bool(*)(const char*,int,void*,const void*,const void*,const char*,int);
DrawSettings original_settings{};
using DrawSection=void(*)(const void*,bool*);
DrawSection original_section{};
using MenuMode=int(*)();
MenuMode original_menu_mode{};
int menu_mode() {
    // 2.1.3 a8896 calls this getter during hook installation. Its imgui
    // branch skips a891c..aa0aa, including native attachMovie hooks at
    // a9e02/aa053. Runtime callers must retain the user's actual menu mode.
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    const int mode=original_menu_mode();
    return mfg213::initialization_menu_mode(mode,caller-reinterpret_cast<uintptr_t>(upstream));
}

// Upstream's legacy ImGui section drawer has no Float case: type 6 falls
// through to gamepad rebinding at 8716c. Render the Goblin section's newer
// numeric controls with its existing SliderScalar widget instead. All other
// sections retain their upstream UI. Schema section stride=48, entry=64;
// begin/end at 18/20, entry type/target at 8/10 (862e0 and 87dbc call site).
void draw_section(const void* section,bool* changed) {
    const auto name=mfg213::field<const char*>(section,0);
    if(std::strcmp(name,"Goblin")!=0) { original_section(section,changed); return; }
    const auto first=mfg213::field<const unsigned char*>(section,24);
    const auto last=mfg213::field<const unsigned char*>(section,32);
    if(!first || last<first || (last-first)%64 || last-first>64*64)return;
    const auto text=reinterpret_cast<TextWrapped>(upstream+0x109370);
    const auto checkbox=reinterpret_cast<Checkbox>(upstream+0x10b3c0);
    const auto slider=reinterpret_cast<SliderScalar>(upstream+0x10dba0);
    text("%s","Map visibility and emphasis");
    for(auto* e=first;e!=last;e+=64) {
        const auto key=mfg213::field<const char*>(e,0);
        const auto type=mfg213::field<uint8_t>(e,8);
        auto* target=mfg213::field<void*>(e,16);
        if(!target || e[40] || e[56])continue;
        const char* label=nullptr;
        if(type==0) {
            if(std::strcmp(key,"require_map_fragments")==0)label="Require area map discovery";
            if(std::strcmp(key,"location_emphasis")==0)label="Emphasize the area I am in";
            if(label && checkbox(label,static_cast<bool*>(target)))*changed=true;
        } else if(type==6) {
            float low=0.5f,high=2.0f;
            if(std::strcmp(key,"location_emphasis_own_scale")==0)label="Current area marker size";
            if(std::strcmp(key,"location_emphasis_other_scale")==0)label="Other area marker size";
            if(std::strcmp(key,"location_emphasis_other_fade")==0) {
                label="Other area opacity";low=0.2f;high=1.0f;
            }
            if(std::strcmp(key,"location_emphasis_other_cool")==0) {
                label="Other area cool tint";low=0.0f;high=1.0f;
            }
            // map_panel_offset_percent already has a proper slider at the top.
            if(label && slider(label,8,target,&low,&high,"%.2f",16))*changed=true;
        }
    }
}

bool save_options(unsigned flags) {
    std::lock_guard lock(options_mutex);
    std::wstring section;
    const wchar_t* names[]{L"checks_only",L"progression_only",L"in_logic_only"};
    for(unsigned i=0;i<3;++i) {
        section+=names[i]; section+=L"="; section+=(flags&(1u<<i))?L"1":L"0";
        section.push_back(L'\0');
    }
    section.push_back(L'\0');
    if(!WritePrivateProfileSectionW(L"AP",section.c_str(),(directory/L"MapForGoblins.AP.ini").c_str())) {
        settings_save_failed.store(true); log("Could not save AP menu settings"); return false;
    }
    options.store(flags);
    settings_save_failed.store(false);
    return true;
}

void draw_settings() {
    const auto text=reinterpret_cast<TextWrapped>(upstream+0x109370);
    const auto checkbox=reinterpret_cast<Checkbox>(upstream+0x10b3c0);
    const auto snapshot=cache.active_check_states(millis());
    text("%s",snapshot?"Archipelago - filter data active":"Archipelago - no active filter data");
    if(snapshot) text("%zu check identities supplied by the client",snapshot->flags.size());
    else text("%s","AP filters are inactive. The map uses upstream visibility.");
    unsigned flags=options.load();
    bool changed=false;
    const char* labels[]{"Checks in this seed##ap_checks","Progression items only##ap_progression",
                         "Reachable according to tracker##ap_logic"};
    for(unsigned i=0;i<3;++i) {
        bool value=(flags&(1u<<i))!=0;
        if(checkbox(labels[i],&value)) {
            if(value)flags|=1u<<i;else flags&=~(1u<<i);
            changed=true;
        }
    }
    if(changed)save_options(flags);
    if(settings_save_failed.load())text("%s","Could not save AP settings. Check that MapForGoblins.AP.ini is writable.");
    if(have_view_counts.load()) {
        const auto counts=last_view_counts.load();
        text("Last map layer: %u upstream candidates; %u after AP filters",
             static_cast<unsigned>(counts>>32),static_cast<unsigned>(counts));
    }
    {
        Diagnostic d; bool valid;
        {std::lock_guard lock(diagnostic_mutex);d=diagnostic;valid=diagnostic_ready;}
        if(valid && d.active) {
            text("Seed-matched candidates: %u; reachable: %u; progression: %u; both: %u",
                d.counts.seed,d.counts.logic,d.counts.progression,d.counts.both);
            text("With tracker filter off: %u matched candidates. Unmatched: %u. Upstream rejected: %u.",
                d.counts.without_logic,d.counts.unmatched,d.counts.upstream_hidden);
            text("%s","Counts describe predicate evaluations on the last map layer, not checks in the current viewport.");
        }
    }
    text("%s","Missing pins? Turn off the tracker filter first. Category, discovery and collected-marker filters below still apply. Some checks have no matched pin.");
    original_settings();
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
    std::lock_guard lock(options_mutex);
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
            {0x8f390,reinterpret_cast<void*>(close_map),reinterpret_cast<void**>(&original_close)},
            {0x875b0,reinterpret_cast<void*>(draw_settings),reinterpret_cast<void**>(&original_settings)},
            {0x862e0,reinterpret_cast<void*>(draw_section),reinterpret_cast<void**>(&original_section)},
            {0x1bc50,reinterpret_cast<void*>(menu_mode),reinterpret_cast<void**>(&original_menu_mode)}};
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
        log("Pinned 2.1.3 loaded; eight AP hooks armed; ImGui native-attachment initialization corrected");
        // File I/O stays off render callbacks. Log only changed diagnostics.
        Diagnostic previous;bool logged=false;
        for(;;){
            Sleep(1000);load_options();
            Diagnostic d;bool valid;
            {std::lock_guard lock(diagnostic_mutex);d=diagnostic;valid=diagnostic_ready;}
            if(valid && (!logged || !(d==previous))) {
                char line[512];
                std::snprintf(line,sizeof(line),
                    "AP visibility layer=%d active=%u options=%u supplied=%zu tested=%u upstream_hidden=%u unmatched=%u seed=%u logic=%u progression=%u both=%u without_logic=%u",
                    d.layer,static_cast<unsigned>(d.active),d.flags,d.supplied,d.counts.tested,
                    d.counts.upstream_hidden,d.counts.unmatched,d.counts.seed,d.counts.logic,
                    d.counts.progression,d.counts.both,d.counts.without_logic);
                log(line);previous=d;logged=true;
            }
        }
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
