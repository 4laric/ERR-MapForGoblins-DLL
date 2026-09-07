#pragma once
#include <cstdint>
#include <cwchar>

namespace goblin
{
// Encoded item IDs are not PlaceName IDs. Only a copied, readable entry may
// replace an existing label; a late-added randomizer placeholder keeps it.
template<class Remap, class Lookup>
int32_t resolved_item_label(int32_t encoded, int32_t fallback,
                            const Remap& remap, Lookup lookup)
{
    const auto it = remap.find(encoded);
    if (it == remap.end()) return fallback;
    const wchar_t* text = lookup(it->second);
    if (!text || !text[0] || std::wcscmp(text, L"[ERROR]") == 0)
        return fallback;
    return it->second;
}
}
