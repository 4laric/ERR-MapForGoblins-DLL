#pragma once
#include "../src/goblin_ap_cache.hpp"
#include <cstring>
#include <span>
#include <stdexcept>
#include <unordered_map>

namespace mfg213 {
inline constexpr char sha256[] = "ed984d5bb3ee49e304ab02e5ac1bc1bfc3a6368c2bc8743f85edefe2a73f2ea3";
inline constexpr size_t entries_rva = 0x1d7850, entry_size = 296, entry_count = 7039;
template<class T> T field(const void* p, size_t offset) {
    T v; std::memcpy(&v, static_cast<const unsigned char*>(p) + offset, sizeof(v)); return v;
}
struct Identity { uint64_t handle; uint32_t table, lot; goblin::ap::CheckIdentity check; };
using Identities = std::unordered_map<uint64_t, Identity>;
inline Identities identities(std::span<const unsigned char> image) {
    if (image.size() < entries_rva + entry_count * entry_size + 8)
        throw std::runtime_error("2.1.3 image/table truncated");
    if (field<uint64_t>(image.data(), entries_rva + entry_count * entry_size) != entry_count)
        throw std::runtime_error("2.1.3 table count mismatch");
    Identities out;
    for (size_t i=0; i<entry_count; ++i) {
        const auto* p=image.data()+entries_rva+i*entry_size;
        const auto id=field<uint64_t>(p,0);
        const auto cat=field<uint8_t>(p,264);
        const auto lot=field<uint32_t>(p,280);
        const auto table=field<uint8_t>(p,284);
        if (!id || (id>>63) || cat>62 || table>2 || ((lot==0)!=(table==0)))
            throw std::runtime_error("2.1.3 baked identity invalid");
        Identity identity{id,table,lot,goblin::ap::marker_check_identity(
            table,lot,cat==46,field<uint32_t>(p,8+0x14),field<uint32_t>(p,8+0x38))};
        if (!out.emplace(id,identity).second) throw std::runtime_error("duplicate baked identity");
    }
    return out;
}
// Native completion badges add bit 63 to the original marker's identity.
inline const Identity* find(const Identities& ids, uint64_t handle) {
    const auto it=ids.find(handle & ~(uint64_t{1}<<63));
    return it==ids.end()?nullptr:&it->second;
}
inline bool allowed(const Identity* id, const goblin::ap::CheckStateSnapshot* checks, unsigned options) {
    return goblin::ap::check_filter_allows(checks,id?id->check.kind:0,id?id->check.row:0,
        (options&1)!=0,(options&2)!=0,(options&4)!=0);
}
// Count independent alternatives against the same immutable snapshot. These
// counters explain filtering; they never override upstream visibility.
struct FilterCounts {
    uint32_t tested{}, upstream_hidden{}, unmatched{}, seed{}, logic{}, progression{}, both{}, without_logic{};
    bool operator==(const FilterCounts&) const = default;
    void observe(const Identity* id, const goblin::ap::CheckStateSnapshot* checks,
                 unsigned options, bool upstream_visible) {
        ++tested;
        if(!upstream_visible) { ++upstream_hidden; return; }
        if(!checks)return;
        if(!allowed(id,checks,1)) { ++unmatched; return; }
        ++seed;
        logic+=allowed(id,checks,5);
        progression+=allowed(id,checks,3);
        both+=allowed(id,checks,7);
        without_logic+=allowed(id,checks,options&~4u);
    }
};
}
