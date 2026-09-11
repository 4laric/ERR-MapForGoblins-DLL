#include "../adapter/release213.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace mfg213;
    Identities ids{{123,{123,1,456,{1,456}}}};
    assert(find(ids,123)==find(ids,123|(uint64_t{1}<<63)));
    assert(!find(ids,124));
    goblin::ap::CheckStateSnapshot empty{1,{}};
    assert(allowed(find(ids,123),nullptr,7));
    assert(!allowed(find(ids,123),&empty,1));
    assert(!allowed(nullptr,&empty,1));
    assert(allowed(nullptr,&empty,0));
    empty.flags.emplace(goblin::ap::check_key(1,456),7);
    assert(allowed(find(ids,123),&empty,3));
    assert(!allowed(find(ids,123),&empty,6));
    empty.flags.begin()->second=15;
    assert(allowed(find(ids,123),&empty,7));
    bool rejected=false;
    try { (void)identities({}); } catch (const std::runtime_error&) { rejected=true; }
    assert(rejected);
    std::vector<unsigned char> image(entries_rva+entry_count*entry_size+8);
    auto put=[&](size_t offset,auto value){std::memcpy(image.data()+offset,&value,sizeof(value));};
    put(entries_rva+entry_count*entry_size,uint64_t{entry_count});
    for(size_t i=0;i<entry_count;++i)put(entries_rva+i*entry_size,uint64_t{i+1});
    put(entries_rva+280,uint32_t{456}); put(entries_rva+284,uint8_t{1});
    put(entries_rva+entry_size+264,uint8_t{46});
    put(entries_rva+entry_size+8+0x14,uint32_t{12345});
    const auto decoded=identities(image);
    assert(decoded.size()==entry_count);
    assert(find(decoded,1)->table==1 && find(decoded,1)->lot==456);
    assert(find(decoded,2)->check.kind==MFG_AP_BOSS_DEFEAT_FLAG);
    assert(find(decoded,2)->check.row==12345);
    put(entries_rva+entry_size,uint64_t{1});
    rejected=false;
    try {(void)identities(image);}catch(const std::runtime_error&){rejected=true;}
    assert(rejected);
    std::cout << "Adapter identity/filter checks passed\n";
}
