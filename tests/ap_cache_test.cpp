#include "../src/goblin_ap_cache.hpp"
#include <atomic>
#include <chrono>

extern "C" uint32_t MFG_AP_QUERY_V1(uint32_t, MFG_AP_InfoV1*, uint32_t);
extern "C" uint32_t MFG_AP_COPY_HOVER_V1(MFG_AP_HoverV1*, uint32_t);
#include <cassert>
#include <iostream>
#include <thread>

using goblin::ap::HoverCache;
extern "C" uint32_t __cdecl MFG_AP_SET_LOT_STYLES_V1(
    uint32_t, const MFG_AP_LotStyleV1*, uint32_t, uint32_t);

extern "C" uint32_t __cdecl MFG_AP_SET_CHECK_STATES_V1(
    uint32_t, const MFG_AP_CheckStateV1*, uint32_t, uint32_t);

static void test_check_is_progression()
{
    using goblin::ap::check_is_progression;
    goblin::ap::CheckStateSnapshot snap;
    snap.generation = 1;
    snap.flags[goblin::ap::check_key(1, 100)] = MFG_AP_CHECK | MFG_AP_PROGRESSION;                 // prog, not reachable
    snap.flags[goblin::ap::check_key(1, 101)] = MFG_AP_CHECK | MFG_AP_IN_LOGIC;                    // reachable, not prog
    snap.flags[goblin::ap::check_key(1, 102)] = MFG_AP_CHECK | MFG_AP_PROGRESSION | MFG_AP_IN_LOGIC; // both, but NOT the same check
    snap.flags[goblin::ap::check_key(1, 103)] = MFG_AP_CHECK | MFG_AP_PROGRESSION | MFG_AP_IN_LOGIC | MFG_AP_PROGRESSION_IN_LOGIC;
    snap.flags[goblin::ap::check_key(3, 104)] = MFG_AP_PROGRESSION | MFG_AP_PROGRESSION_IN_LOGIC;  // no CHECK bit
    assert(!check_is_progression(nullptr, 1, 100, false));
    assert(check_is_progression(&snap, 1, 100, false));
    assert(!check_is_progression(&snap, 1, 100, true));
    assert(!check_is_progression(&snap, 1, 101, false));
    assert(!check_is_progression(&snap, 1, 101, true));
    assert(check_is_progression(&snap, 1, 102, false));
    assert(!check_is_progression(&snap, 1, 102, true));   // bit 8 is never rebuilt from 2|4
    assert(check_is_progression(&snap, 1, 103, true));
    assert(!check_is_progression(&snap, 3, 104, false));
    assert(!check_is_progression(&snap, 1, 999, false));
}

static void test_check_presentation()
{
    using goblin::ap::marker_check_identity;
    const auto godrick = marker_check_identity(0, 0, true, 10000800, 10000800);
    const auto sentinel = marker_check_identity(0, 0, true, 0, 1042360800);
    assert(godrick.kind == 3 && godrick.row == 10000800);
    assert(sentinel.kind == 3 && sentinel.row == 1042360800);
    assert(marker_check_identity(1, 30100, true, 1042360800, 0).kind == 1);
    assert(marker_check_identity(0, 0, false, 10000800, 10000800).kind == 0);
    assert(marker_check_identity(0, 0, true, 0, 0).kind == 0);
    HoverCache bosses;
    bosses.set_active(true);
    MFG_AP_CheckStateV1 boss_states[] = {{3, godrick.row, 15}, {3, sentinel.row, 3}, {1, godrick.row, 1}};
    assert(bosses.set_check_states(1, boss_states, 3, 1000, 100) == MFG_AP_OK);
    const auto snapshot = bosses.active_check_states(101);
    assert(snapshot);
    assert(goblin::ap::check_filter_allows(snapshot.get(), 3, godrick.row, true, true, true));
    assert(!goblin::ap::check_filter_allows(snapshot.get(), 3, sentinel.row, true, true, true));
    assert(!goblin::ap::check_filter_allows(snapshot.get(), 1, godrick.row, true, true, false));
    MFG_AP_LotStyleV1 invalid_style{3, godrick.row, MFG_AP_STYLE_ORANGE};
    assert(bosses.set_lot_styles(1, &invalid_style, 1, 1000, 100) == MFG_AP_BAD_ARGUMENT);
    // (The lot-style ring renderer was removed 2026-09-07; the setter stays a
    // validated no-op for older clients, so only its validation is exercised here.)
    goblin::ap::CheckFilterRefresh refresh;
    assert(refresh.due(1, 1, 1000));
    refresh.complete(1, 1, 1000, false);
    assert(!refresh.due(1, 1, 1099)); // bounded retry, not every owner tick
    assert(refresh.due(1, 1, 1100));  // identical heartbeat generation still retries
    refresh.complete(1, 1, 1100, true);
    assert(!refresh.due(1, 1, 1200));
    assert(refresh.due(1, 3, 1200)); // option changes have their own signature
    refresh.complete(1, 3, 1200, false);
    assert(refresh.due(1, 1, 1300)); // failed partial write must restore even old options
    refresh.complete(1, 1, 1300, true);
    assert(refresh.due(0, 1, 1301)); // disconnect/expiry restores native state
    refresh.complete(0, 1, 1301, true);
    assert(!refresh.due(0, 1, 1400));
}

static void test_check_states()
{
    static_assert(sizeof(MFG_AP_CheckStateV1) == 12);
    HoverCache cache;
    cache.set_active(true);
    MFG_AP_CheckStateV1 states[] = {{1, 10, 1}, {1, 11, 3}, {1, 12, 5}, {1, 13, 7}, {1, 14, 15}};
    assert(cache.set_check_states(2, states, 5, 3000, 1000) == MFG_AP_UNSUPPORTED_ABI);
    assert(cache.set_check_states(1, nullptr, 1, 3000, 1000) == MFG_AP_BAD_ARGUMENT);
    assert(cache.set_check_states(1, states, 8193, 3000, 1000) == MFG_AP_BAD_ARGUMENT);
    assert(cache.set_check_states(1, states, 5, 249, 1000) == MFG_AP_BAD_ARGUMENT);
    assert(cache.set_check_states(1, states, 5, 10001, 1000) == MFG_AP_BAD_ARGUMENT);
    assert(cache.set_check_states(1, states, 5, 3000, UINT64_MAX) == MFG_AP_BAD_ARGUMENT);
    assert(cache.set_check_states(1, states, 5, 3000, 1000) == MFG_AP_OK);
    const auto snapshot = cache.active_check_states(1000);
    assert(snapshot && snapshot->flags.size() == 5);
    assert(cache.set_check_states(1, states, 5, 3000, 1001) == MFG_AP_OK);
    assert(cache.active_check_states(1001) == snapshot); // lease-only heartbeat

    using goblin::ap::check_filter_allows;
    const auto allows = [&](uint32_t row, bool c, bool p, bool l) {
        return check_filter_allows(snapshot.get(), 1, row, c, p, l);
    };
    assert(allows(999, false, false, false));
    assert(!allows(999, true, false, false));
    assert(allows(10, true, false, false));
    assert(!allows(10, false, true, false));
    assert(allows(11, false, true, false));
    assert(!allows(11, false, false, true));
    assert(allows(12, false, false, true));
    assert(!allows(13, true, true, true)); // separate witnesses cannot satisfy conjunction
    assert(allows(14, true, true, true));
    assert(!check_filter_allows(snapshot.get(), 2, 14, true, false, false));
    assert(!check_filter_allows(snapshot.get(), 0, 0, true, false, false));
    assert(check_filter_allows(nullptr, 0, 0, true, true, true));
    // Final visibility is an intersection: an allowed AP state cannot reveal a native-hidden marker.
    const bool native_visible = false;
    assert(!(native_visible && allows(14, true, true, true)));

    for (uint32_t flags : {0u, 2u, 4u, 8u, 9u, 11u, 13u, 17u, UINT32_MAX})
    {
        MFG_AP_CheckStateV1 bad{1, 10, flags};
        assert(cache.set_check_states(1, &bad, 1, 3000, 1001) == MFG_AP_BAD_ARGUMENT);
        assert(cache.active_check_states(1001)->generation == snapshot->generation);
    }
    for (auto bad : {MFG_AP_CheckStateV1{0, 10, 1}, MFG_AP_CheckStateV1{4, 10, 1},
                     MFG_AP_CheckStateV1{1, 0, 1}})
        assert(cache.set_check_states(1, &bad, 1, 3000, 1001) == MFG_AP_BAD_ARGUMENT);
    states[1] = states[0];
    assert(cache.set_check_states(1, states, 2, 3000, 1001) == MFG_AP_BAD_ARGUMENT);
    states[0].flags = 15;
    assert(snapshot->flags.at(goblin::ap::check_key(1, 10)) == 1); // immutable copied ownership
    assert(cache.set_check_states(1, nullptr, 0, 250, 2000) == MFG_AP_OK);
    auto empty = cache.active_check_states(2249);
    assert(empty && empty->flags.empty());
    assert(!check_filter_allows(empty.get(), 1, 10, true, false, false));
    assert(!cache.active_check_states(2250));
    assert(cache.set_check_states(1, nullptr, 0, 250, 3000) == MFG_AP_OK);
    assert(!cache.active_check_states(2999));
    assert(cache.set_check_states(1, states, 1, 250, 4000) == MFG_AP_OK);
    assert(cache.set_check_states(1, states, 0, 0, 4001) == MFG_AP_BAD_ARGUMENT);
    assert(cache.set_check_states(1, nullptr, 0, 0, 4001) == MFG_AP_OK);
    assert(!cache.active_check_states(4001));
    assert(cache.set_check_states(1, states, 1, 250, 5000) == MFG_AP_OK);
    cache.set_active(false); cache.set_active(true);
    assert(!cache.active_check_states(5001));
    assert(cache.set_check_states(1, states, 1, 250, 6000) == MFG_AP_OK);
    cache.invalidate_rows();
    assert(!cache.active_check_states(6001));
    assert(cache.set_check_states(1, states, 1, 250, 7000) == MFG_AP_OK);
    cache.install_rows({{100, 1, 1, 10}});
    assert(!cache.active_check_states(7001));

    auto& live = goblin::ap::cache();
    live.set_active(true);
    assert(MFG_AP_SET_CHECK_STATES_V1(1, states, 1, 3000) == MFG_AP_OK);
    const auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    assert(live.active_check_states(now));
    assert(MFG_AP_SET_CHECK_STATES_V1(1, nullptr, 0, 3000) == MFG_AP_OK);
    assert(MFG_AP_SET_CHECK_STATES_V1(1, nullptr, 0, 0) == MFG_AP_OK);
    assert(!live.active_check_states(now));
}

int main()
{
    test_check_is_progression();
    test_check_presentation();
    test_check_states();
    static_assert(sizeof(MFG_AP_InfoV1) == 16);
    static_assert(sizeof(MFG_AP_HoverV1) == 40);
    static_assert(offsetof(MFG_AP_HoverV1, handle) == 16);
    HoverCache cache;
    MFG_AP_InfoV1 info{};
    MFG_AP_HoverV1 hover{};
    assert(cache.query(2, &info, sizeof(info)) == MFG_AP_UNSUPPORTED_ABI);
    assert(cache.query(1, nullptr, sizeof(info)) == MFG_AP_BAD_ARGUMENT);
    assert(cache.query(1, &info, sizeof(info) - 1) == MFG_AP_BAD_ARGUMENT);
    assert(cache.query(1, &info, sizeof(info)) == MFG_AP_OK && info.capabilities == (MFG_AP_CAP_LOT_STYLE_OVERLAY_V1 | MFG_AP_CAP_CHECK_STATES_V1 | MFG_AP_CAP_BOSS_CHECK_STATES_V1));
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
           info.capabilities == (MFG_AP_CAP_HOVER_V1 | MFG_AP_CAP_LOT_STYLE_OVERLAY_V1 | MFG_AP_CAP_CHECK_STATES_V1 | MFG_AP_CAP_BOSS_CHECK_STATES_V1));
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
    assert(info.capabilities == (MFG_AP_CAP_HOVER_V1 | MFG_AP_CAP_LOT_STYLE_OVERLAY_V1 | MFG_AP_CAP_CHECK_STATES_V1 | MFG_AP_CAP_BOSS_CHECK_STATES_V1));
    assert(MFG_AP_COPY_HOVER_V1(&buffer.value, sizeof(buffer.value)) == MFG_AP_OK);
    assert(buffer.value.handle == 1 && buffer.value.lot_row == 10);
    assert(buffer.guard == 0xabcdef);
    buffer.value.handle = 999;
    assert(MFG_AP_COPY_HOVER_V1(&buffer.value, sizeof(buffer.value) - 1) ==
           MFG_AP_BAD_ARGUMENT);
    assert(buffer.value.handle == 999 && buffer.guard == 0xabcdef);
    // Actual presentation export: copied ownership, bounds, duplicate/invalid
    // rejection, all-or-nothing replacement and clear.
    MFG_AP_LotStyleV1 styles[] = {{1, 10, 1}, {2, 20, 2}};
    assert(MFG_AP_SET_LOT_STYLES_V1(1, styles, 2, 3000) == MFG_AP_OK);
    std::shared_ptr<const goblin::ap::LotStyleSnapshot> visible;
    // The exported clock may advance after 'now'; sample a new monotonic time.
    const auto style_now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    assert(MFG_AP_SET_LOT_STYLES_V1(1, styles, 2, 3000) == MFG_AP_OK);
    visible = live.active_lot_styles(static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()));
    assert(visible && visible->entries.size() == 2);
    styles[0].style = 2;
    assert(visible->entries[0].style == 1); // owns a copy
    assert(MFG_AP_SET_LOT_STYLES_V1(2, styles, 2, 3000) == MFG_AP_UNSUPPORTED_ABI);
    assert(MFG_AP_SET_LOT_STYLES_V1(1, nullptr, 1, 3000) == MFG_AP_BAD_ARGUMENT);
    assert(MFG_AP_SET_LOT_STYLES_V1(1, styles, 8193, 3000) == MFG_AP_BAD_ARGUMENT);
    assert(MFG_AP_SET_LOT_STYLES_V1(1, styles, 2, 249) == MFG_AP_BAD_ARGUMENT);
    assert(MFG_AP_SET_LOT_STYLES_V1(1, styles, 2, 10001) == MFG_AP_BAD_ARGUMENT);
    styles[1] = styles[0];
    assert(MFG_AP_SET_LOT_STYLES_V1(1, styles, 2, 3000) == MFG_AP_BAD_ARGUMENT);
    assert(live.active_lot_styles(static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()))->generation == visible->generation);
    styles[0] = {1, 0, 1};
    assert(MFG_AP_SET_LOT_STYLES_V1(1, styles, 1, 3000) == MFG_AP_BAD_ARGUMENT);
    styles[0] = {0, 10, 1};
    assert(MFG_AP_SET_LOT_STYLES_V1(1, styles, 1, 3000) == MFG_AP_BAD_ARGUMENT);
    styles[0] = {1, 10, 3};
    assert(MFG_AP_SET_LOT_STYLES_V1(1, styles, 1, 3000) == MFG_AP_BAD_ARGUMENT);
    assert(MFG_AP_SET_LOT_STYLES_V1(1, nullptr, 0, 0) == MFG_AP_OK);
    assert(!live.active_lot_styles(style_now + 3));

    // Injected clock keeps deadline and clock-reversal tests deterministic.
    styles[0] = {1, 10, 1};
    assert(live.set_lot_styles(1, styles, 1, 250, 5000) == MFG_AP_OK);
    assert(live.active_lot_styles(5249));
    assert(!live.active_lot_styles(5250));
    assert(live.set_lot_styles(1, styles, 1, 250, 6000) == MFG_AP_OK);
    assert(!live.active_lot_styles(5999));
    assert(live.set_lot_styles(1, styles, 1, 250, 7000) == MFG_AP_OK);
    live.set_active(false);
    live.set_active(true);
    assert(!live.active_lot_styles(7001));
    assert(live.set_lot_styles(1, styles, 1, 250, 8000) == MFG_AP_OK);
    live.invalidate_rows();
    assert(!live.active_lot_styles(8001));
    assert(live.set_lot_styles(1, styles, 1, 250, 9000) == MFG_AP_OK);
    assert(live.install_rows({{100, 1, 1, 10}}));
    assert(!live.active_lot_styles(9001));
    std::cout << "AP cache ABI, identity, lifecycle, expiry and concurrency checks passed\n";
}
