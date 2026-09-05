#include "../src/goblin_ap_cache.hpp"
#include <atomic>
#include <chrono>

extern "C" uint32_t MFG_AP_QUERY_V1(uint32_t, MFG_AP_InfoV1*, uint32_t);
extern "C" uint32_t MFG_AP_COPY_HOVER_V1(MFG_AP_HoverV1*, uint32_t);
#include <cassert>
#include <iostream>
#include <thread>

using goblin::ap::HoverCache;
int main()
{
    static_assert(sizeof(MFG_AP_InfoV1) == 16);
    static_assert(sizeof(MFG_AP_HoverV1) == 40);
    static_assert(offsetof(MFG_AP_HoverV1, handle) == 16);
    HoverCache cache;
    MFG_AP_InfoV1 info{};
    MFG_AP_HoverV1 hover{};
    assert(cache.query(2, &info, sizeof(info)) == MFG_AP_UNSUPPORTED_ABI);
    assert(cache.query(1, nullptr, sizeof(info)) == MFG_AP_BAD_ARGUMENT);
    assert(cache.query(1, &info, sizeof(info) - 1) == MFG_AP_BAD_ARGUMENT);
    assert(cache.query(1, &info, sizeof(info)) == MFG_AP_OK && info.capabilities == 0);
    assert(cache.copy(&hover, sizeof(hover), 0) == MFG_AP_UNAVAILABLE);
    assert(!cache.install_rows({{0, 1, 1, 10}}));
    assert(!cache.install_rows({{100, 1, 0, 10}}));
    assert(!cache.install_rows({{100, 1, 1, 10}, {200, 1, 2, 20}}));
    assert(!cache.install_rows({{100, 1, 1, 10}, {100, 2, 2, 20}}));

    // Deliberately non-address pointer keys: the cache must never dereference them.
    assert(cache.install_rows({{100, 1, 1, 10}, {200, 2, 2, 20}, {300, 3, 0, 0}}));
    cache.set_active(true);
    assert(cache.copy(&hover, sizeof(hover), 0) == MFG_AP_UNAVAILABLE);
    cache.set_hooks_ready(true);
    assert(cache.query(1, &info, sizeof(info)) == MFG_AP_OK &&
           info.capabilities == MFG_AP_CAP_HOVER_V1);
    assert(cache.copy(&hover, sizeof(hover), 0) == MFG_AP_OK &&
           hover.status == MFG_AP_NO_HOVER && hover.generation > 0);
    cache.observe(100, 1000);
    assert(cache.copy(&hover, sizeof(hover), 1300) == MFG_AP_OK &&
           hover.handle == 1 && hover.original_flag == 0 &&
           hover.lot_table == 1 && hover.lot_row == 10 && hover.age_ms == 300);
    const auto generation = hover.generation;
    assert(cache.copy(&hover, sizeof(hover), 1301) == MFG_AP_OK &&
           hover.status == MFG_AP_NO_HOVER && hover.handle == 0 && hover.lot_row == 0);
    cache.observe(200, 2000);
    assert(cache.copy(&hover, sizeof(hover), 1999) == MFG_AP_OK &&
           hover.status == MFG_AP_NO_HOVER); // clock anomaly fails closed
    cache.observe(999, 2000); // unsupported vanilla pin
    assert(cache.copy(&hover, sizeof(hover), 2000) == MFG_AP_OK &&
           hover.status == MFG_AP_NO_HOVER);
    cache.observe(300, 2000);
    assert(cache.copy(&hover, sizeof(hover), 2000) == MFG_AP_OK &&
           hover.status == MFG_AP_HOVER && hover.lot_table == 0);
    cache.map_rebuild();
    assert(cache.copy(&hover, sizeof(hover), 2000) == MFG_AP_OK &&
           hover.generation > generation && hover.handle == 0);
    cache.observe(100, 2000);
    cache.set_active(false);
    assert(cache.copy(&hover, sizeof(hover), 2000) == MFG_AP_UNAVAILABLE);
    cache.set_active(true);
    assert(cache.copy(&hover, sizeof(hover), 2000) == MFG_AP_OK && hover.handle == 0);
    cache.invalidate_rows();
    assert(cache.copy(&hover, sizeof(hover), 2000) == MFG_AP_UNAVAILABLE);
    assert(cache.install_rows({{200, 4, 2, 20}}));
    cache.observe(100, 2000); // retired pointer key
    assert(cache.copy(&hover, sizeof(hover), 2000) == MFG_AP_OK && hover.handle == 0);

    assert(!cache.install_rows({{0, 4, 2, 20}}));
    assert(cache.copy(&hover, sizeof(hover), 2000) == MFG_AP_UNAVAILABLE);

    // Producer/readers cannot mix a handle from one observation with another lot.
    assert(cache.install_rows({{100, 1, 1, 10}, {200, 2, 2, 20}}));
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (int i = 0; i < 100000; ++i) cache.observe(i % 2 ? 100 : 200, 3000);
        done.store(true);
    });
    do {
        assert(cache.copy(&hover, sizeof(hover), 3000) == MFG_AP_OK);
        if (hover.status == MFG_AP_HOVER)
            assert((hover.handle == 1 && hover.lot_table == 1 && hover.lot_row == 10) ||
                   (hover.handle == 2 && hover.lot_table == 2 && hover.lot_row == 20));
    } while (!done.load());
    writer.join();

    // Exercise the exported functions too, using the same singleton the native
    // producers use, and guard adjacent caller memory against overwrite.
    struct Buffer { MFG_AP_HoverV1 value; uint64_t guard; } buffer{{}, 0xabcdef};
    auto& live = goblin::ap::cache();
    assert(live.install_rows({{100, 1, 1, 10}}));
    live.set_active(true);
    live.set_hooks_ready(true);
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    live.observe(100, static_cast<uint64_t>(now));
    assert(MFG_AP_QUERY_V1(1, &info, sizeof(info)) == MFG_AP_OK);
    assert(info.capabilities == MFG_AP_CAP_HOVER_V1);
    assert(MFG_AP_COPY_HOVER_V1(&buffer.value, sizeof(buffer.value)) == MFG_AP_OK);
    assert(buffer.value.handle == 1 && buffer.value.lot_row == 10);
    assert(buffer.guard == 0xabcdef);
    buffer.value.handle = 999;
    assert(MFG_AP_COPY_HOVER_V1(&buffer.value, sizeof(buffer.value) - 1) ==
           MFG_AP_BAD_ARGUMENT);
    assert(buffer.value.handle == 999 && buffer.guard == 0xabcdef);
    std::cout << "AP cache ABI, identity, lifecycle, expiry and concurrency checks passed\n";
}
