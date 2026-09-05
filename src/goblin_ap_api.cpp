#include "goblin_ap_cache.hpp"
#include <chrono>

static_assert(sizeof(MFG_AP_InfoV1) == 16);
static_assert(sizeof(MFG_AP_HoverV1) == 40);
static_assert(offsetof(MFG_AP_HoverV1, generation) == 8);
static_assert(offsetof(MFG_AP_HoverV1, original_flag) == 24);

goblin::ap::HoverCache& goblin::ap::cache()
{
    static HoverCache instance;
    return instance;
}

#if defined(_WIN32)
#define MFG_AP_EXPORT extern "C" __declspec(dllexport)
#else
#define MFG_AP_EXPORT extern "C"
#endif

MFG_AP_EXPORT uint32_t __cdecl MFG_AP_QUERY_V1(
    uint32_t abi, MFG_AP_InfoV1* out, uint32_t capacity)
{
    return goblin::ap::cache().query(abi, out, capacity);
}

MFG_AP_EXPORT uint32_t __cdecl MFG_AP_COPY_HOVER_V1(
    MFG_AP_HoverV1* out, uint32_t capacity)
{
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return goblin::ap::cache().copy(out, capacity, static_cast<uint64_t>(now));
}
