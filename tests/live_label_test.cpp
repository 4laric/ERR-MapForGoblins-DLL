#include "../src/goblin_live_label.hpp"
#include <cassert>
#include <unordered_map>

int main()
{
    // Ash-of-War Scarab's baked name was copied before AP created Goods 8852.
    const std::unordered_map<int32_t, int32_t> remap{
        {201900000, 7001}, {500000100, 7002}, {500000200, 7003},
        {500000300, 7004}, {500000400, 7005}};
    const auto lookup = [](int32_t id) -> const wchar_t* {
        if (id == 7001) return L"Ash-of-War Scarab";
        if (id == 7002) return L"Valid randomized item";
        if (id == 7004) return L"";
        if (id == 7005) return L"[ERROR]";
        return nullptr; // Remap allocated, but FMG merge failed.
    };
    using goblin::resolved_item_label;
    const auto baked = resolved_item_label(201900000, -1, remap, lookup);
    assert(baked == 7001);
    assert(resolved_item_label(500008852, baked, remap, lookup) == baked);
    assert(resolved_item_label(500000100, baked, remap, lookup) == 7002);
    assert(resolved_item_label(500000200, baked, remap, lookup) == baked);
    assert(resolved_item_label(500000300, baked, remap, lookup) == baked);
    assert(resolved_item_label(500000400, baked, remap, lookup) == baked);
    assert(resolved_item_label(500008852, -1, remap, lookup) == -1);
}
