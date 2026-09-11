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
    // Alternatives must agree with the actual filter for every valid state and
    // option combination, including progression/logic on different checks.
    for(const unsigned state:{1u,3u,5u,7u,15u}) {
        empty.flags.begin()->second=state;
        for(unsigned options=0;options<8;++options) {
            FilterCounts counts;
            counts.observe(find(ids,123),&empty,options,true);
            assert(counts.seed==1);
            assert(counts.logic==static_cast<unsigned>(allowed(find(ids,123),&empty,5)));
            assert(counts.progression==static_cast<unsigned>(allowed(find(ids,123),&empty,3)));
            assert(counts.both==static_cast<unsigned>(allowed(find(ids,123),&empty,7)));
            assert(counts.without_logic==static_cast<unsigned>(allowed(find(ids,123),&empty,options&~4u)));
            counts.observe(nullptr,&empty,options,true);
            counts.observe(find(ids,123),&empty,options,false);
            assert(counts.tested==3 && counts.upstream_hidden==1 && counts.unmatched==1);
            assert(counts.seed==1); // Upstream-hidden rows never become candidates.
        }
    }
    FilterCounts inactive;
    inactive.observe(find(ids,123),nullptr,7,true);
    assert(inactive.tested==1 && inactive.seed==0 && inactive.unmatched==0);
    goblin::ap::CheckStateSnapshot no_checks{2,{}};
    FilterCounts active_empty;
    active_empty.observe(find(ids,123),&no_checks,5,true);
    assert(active_empty.unmatched==1 && active_empty.without_logic==0);
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
