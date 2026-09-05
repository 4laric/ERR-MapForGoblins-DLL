#include "goblin_map_timing.hpp"

#include "goblin_config.hpp"
#include "goblin_map_profile.hpp"
#include <chrono>
#include <cstring>
#include "modutils.hpp"

#include <spdlog/spdlog.h>

#include <atomic>
#include <mutex>
#include <vector>

#include <intrin.h> // _ReturnAddress

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// World-map open optimization (config::fastMapOpen). With ~7000 markers the game's
// per-marker widget relayout on every map open takes a long time. We wrap that
// relayout (refresh fn) and the child-list passes (ce390) at the map's per-marker
// dispatcher call site, gated by return address so other UI is left as-is, and:
//   - reuse the existing layout on RE-open (after the first build), since it is
//     unchanged;
//   - on the FIRST open, defer the relayout and run it a budget per frame so the
//     map opens smoothly (ce390 runs in-line - deferring it left the map a blank
//     frame).
// Every call site is resolved by pattern (resilient to game updates). The
// WorldMapDialog destructor is wrapped only to clear the replay queue on close.
// Full notes: docs/research_worldmap_internals.md.
namespace
{
    using Fn = void *(void *, void *, void *, void *);
    using DtorFn = void *(void *);
    Fn *o_refresh = nullptr, *o_ce390 = nullptr;
    DtorFn *o_wmd_dtor = nullptr;

    // Startup-only opt-in. It overrides the legacy optimizer, including after
    // collection ends: every original call runs, without queued game pointers.
    bool g_profile_mode = false;
    std::atomic<bool> g_profile_done{false};
    std::mutex g_profile_mutex;
    goblin::map_timing::ProfileWindow g_profile;
    using Clock = std::chrono::steady_clock;
    uint64_t now_ms() {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now().time_since_epoch()).count());
    }
    void *profile_call(unsigned bucket, Fn *original, void *a, void *b, void *c, void *d)
    {
        if (g_profile_done.load(std::memory_order_relaxed)) return original(a, b, c, d);
        bool sample;
        {
            std::lock_guard<std::mutex> lock(g_profile_mutex);
            sample = g_profile.begin(now_ms());
        }
        if (!sample) return original(a, b, c, d);
        const auto start = Clock::now();
        void *result = original(a, b, c, d);
        const auto ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - start).count());
        {
            std::lock_guard<std::mutex> lock(g_profile_mutex);
            g_profile.finish(bucket, ns, GetCurrentThreadId());
        }
        return result;
    }

    uintptr_t g_map_callsite = 0;       // ret addr of the map's per-marker refresh call
    uintptr_t g_ce_gate[3] = {0, 0, 0}; // ret addrs of the 3 ce390 calls
    // Partial hook installation must remain a pass-through. In particular,
    // never queue marker pointers before the close hook can retire them.
    std::atomic<bool> g_ready{false};
    std::atomic<bool> g_built{false};
    std::atomic<int> g_refresh_n{0};    // refreshes that ran; latches g_built after the first build

    // Amortize: queue the first open's relayout calls, replay a budget per frame.
    // Build, replay and close all run on the render thread; the mutex only guards a
    // concurrent reader and keeps the queue consistent.
    struct RefreshArgs { void *a, *b, *c, *d; };
    std::mutex g_amrt_mtx;
    std::vector<RefreshArgs> g_amrt_queue;
    size_t g_amrt_pos = 0;
    constexpr size_t AMRT_PER_FRAME = 500;

    void amortize_pump()
    {
        if (!o_refresh) return;
        RefreshArgs batch[AMRT_PER_FRAME];
        size_t n = 0;
        {
            std::lock_guard<std::mutex> lk(g_amrt_mtx);
            while (n < AMRT_PER_FRAME && g_amrt_pos < g_amrt_queue.size())
                batch[n++] = g_amrt_queue[g_amrt_pos++];
            if (g_amrt_pos >= g_amrt_queue.size() && !g_amrt_queue.empty())
            {
                g_amrt_queue.clear();
                g_amrt_pos = 0;
            }
        }
        for (size_t i = 0; i < n; ++i)
            o_refresh(batch[i].a, batch[i].b, batch[i].c, batch[i].d);
    }

    // Latch g_built after the first map build (a burst of build-site refreshes) so
    // later opens skip the redundant relayout. Driven from the ce390 detour (game UI thread).
    void latch_built()
    {
        if (!g_built.load(std::memory_order_relaxed) &&
            g_refresh_n.load(std::memory_order_relaxed) > 1000)
        {
            g_built.store(true, std::memory_order_relaxed);
            spdlog::info("[fastmap] first map build complete; reopen reuses layout");
        }
    }

    void *refresh_detour(void *a, void *b, void *c, void *d)
    {
        if (!g_ready.load(std::memory_order_acquire)) return o_refresh(a, b, c, d);
        uintptr_t ret = (uintptr_t)_ReturnAddress();
        if (g_profile_mode)
            return ret == g_map_callsite ? profile_call(0, o_refresh, a, b, c, d)
                                        : o_refresh(a, b, c, d);
        if (ret == g_map_callsite) // the map's per-marker build call (other UI left as-is)
        {
            // RE-open: reuse the existing layout, no relayout needed (fastest).
            if (goblin::config::fastMapOpen && g_built.load(std::memory_order_relaxed))
                return nullptr;
            // FIRST open: defer + replay over frames (smooth open). The WMD destructor
            // wrapper clears the queue on close so we never replay into freed markers.
            if (goblin::config::fastMapOpen)
            {
                std::lock_guard<std::mutex> lk(g_amrt_mtx);
                g_amrt_queue.push_back({a, b, c, d});
                return nullptr;
            }
        }
        void *r = o_refresh(a, b, c, d);
        g_refresh_n.fetch_add(1, std::memory_order_relaxed); // latch counter
        return r;
    }

    void *ce390_detour(void *a, void *b, void *c, void *d)
    {
        if (!g_ready.load(std::memory_order_acquire)) return o_ce390(a, b, c, d);
        uintptr_t ret = (uintptr_t)_ReturnAddress();
        if (g_profile_mode) {
            for (unsigned i = 0; i < 3; ++i)
                if (ret == g_ce_gate[i]) return profile_call(i + 1, o_ce390, a, b, c, d);
            return o_ce390(a, b, c, d);
        }
        bool map_site = (ret == g_ce_gate[0] || ret == g_ce_gate[1] || ret == g_ce_gate[2]);
        // Drive the first-open amortize replay + the build latch HERE, on the game's
        // UI thread, once per map-layout pass. The overlay used to call on_present()
        // from its swapchain Present hook every frame, but the overlay now renders in
        // a separate window and never touches the game thread; this map dispatcher
        // call (gate[0]) is the closest game-thread per-frame signal while the map is
        // open, so the deferred relayout still drains across frames + the layout never
        // runs on a foreign thread. Gate on the first ce390 site so it fires once per
        // pass, not 3x. (latch only flips after the first build's refresh burst.)
        if (map_site && ret == g_ce_gate[0])
        {
            amortize_pump();
            latch_built();
        }
        // Reuse on RE-open (structure already built); runs in-line on first open.
        if (goblin::config::fastMapOpen && g_built.load(std::memory_order_relaxed) && map_site)
            return nullptr;
        return o_ce390(a, b, c, d);
    }

    void *wmd_dtor_detour(void *self)
    {
        // Map closing: drop any pending amortize replay so we never lay out freed markers.
        {
            std::lock_guard<std::mutex> lk(g_amrt_mtx);
            g_amrt_queue.clear();
            g_amrt_pos = 0;
        }
        return o_wmd_dtor(self);
    }
}

void goblin::map_timing::setup()
{
    char profile_env[8]{};
    g_profile_mode = GetEnvironmentVariableA("MFG_FASTMAP_PROFILE", profile_env, sizeof(profile_env)) == 1
                     && profile_env[0] == '1';
    if (g_profile_mode) {
        try {
            auto *m = static_cast<unsigned char *>(modutils::scan_unique(
                "48 8B 89 18 01 00 00 E8 ?? ?? ?? ?? 33 D2 48 8B CF E8 ?? ?? ?? ?? "
                "BA 01 00 00 00 48 8B CF E8 ?? ?? ?? ?? BA 03 00 00 00 48 8B CF "
                "E8 ?? ?? ?? ?? 48 8B 5C 24 30 B0 01"));
            auto target = [m](size_t offset) -> void * {
                int32_t displacement;
                std::memcpy(&displacement, m + offset + 1, sizeof(displacement));
                return m + offset + 5 + displacement;
            };
            auto *refresh = modutils::scan_unique(
                "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B 41 20 "
                "48 8B D9 48 8B 50 10 48 8B");
            auto *children = modutils::scan_unique(
                "40 53 48 83 EC 50 33 C0 89 54 24 48 4C 8D 81 C8 00 00 00 89");
            if (target(7) != refresh || target(17) != children ||
                target(30) != children || target(43) != children)
                throw std::runtime_error("map call targets do not match validated functions");
            g_map_callsite = reinterpret_cast<uintptr_t>(m + 12);
            g_ce_gate[0] = reinterpret_cast<uintptr_t>(m + 22);
            g_ce_gate[1] = reinterpret_cast<uintptr_t>(m + 35);
            g_ce_gate[2] = reinterpret_cast<uintptr_t>(m + 48);
            modutils::hook<Fn>({.address = refresh}, refresh_detour, o_refresh);
            modutils::hook<Fn>({.address = children}, ce390_detour, o_ce390);
            g_ready.store(true, std::memory_order_release);
            spdlog::info("[fastmap-profile] armed: 30 seconds from first map call; "
                         "cumulative original-call timings, NOT frame times; optimizer bypassed");
        } catch (const std::exception &e) {
            spdlog::warn("[fastmap-profile] disabled: {}; optimizer not attempted", e.what());
        }
        return; // Never install the destructor or enter the legacy optimizer.
    }
    if (!goblin::config::fastMapOpen) return;

    // Resolve the per-marker call sites by byte pattern (resilient to game updates):
    // the reuse/amortize gate keys on the return address of the refresh + 3 ce390
    // calls in the map's per-marker dispatcher. The call TARGETS (E8 rel32) are
    // wildcarded so a game update that moves functions doesn't break the match; the
    // return addresses are fixed offsets from the match. On a miss, g_map_callsite
    // stays 0 -> safe no-op.
    try
    {
        uintptr_t m = reinterpret_cast<uintptr_t>(modutils::scan<void>(
            {.aob = "48 8B 89 18 01 00 00 E8 ?? ?? ?? ?? 33 D2 48 8B CF E8 ?? ?? ?? ?? "
                    "BA 01 00 00 00 48 8B CF E8 ?? ?? ?? ?? BA 03 00 00 00 48 8B CF "
                    "E8 ?? ?? ?? ?? 48 8B 5C 24 30 B0 01"}));
        g_map_callsite = m + 0x0C; // ret of `call refresh`
        g_ce_gate[0] = m + 0x16;   // ret of `call ce390` (edx=0)
        g_ce_gate[1] = m + 0x23;   // ret of `call ce390` (edx=1)
        g_ce_gate[2] = m + 0x30;   // ret of `call ce390` (edx=3)
        spdlog::info("[fastmap] resolved map call sites @ 0x{:X}", m);
    }
    catch (const std::exception &e)
    {
        spdlog::warn("[fastmap] map call site not found ({}); fast map open off", e.what());
        return;
    }

    try
    {
        modutils::hook<Fn>(
            {.aob = "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B 41 20 "
                    "48 8B D9 48 8B 50 10 48 8B"},
            refresh_detour, o_refresh);
        modutils::hook<Fn>(
            {.aob = "40 53 48 83 EC 50 33 C0 89 54 24 48 4C 8D 81 C8 00 00 00 89"},
            ce390_detour, o_ce390);
        // Clear the amortize queue on map close (never replay into freed markers).
        modutils::hook<DtorFn>(
            {.aob = "48 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 30 "
                    "48 C7 45 F0 FE FF FF FF 48 89 9C 24 88 00 00 00 48 8B F1 48 8D 05 "
                    "B7 A3 16 02"},
            wmd_dtor_detour, o_wmd_dtor);
        g_ready.store(true, std::memory_order_release);
        spdlog::info("[fastmap] fast map open ready");
    }
    catch (const std::exception &e)
    {
        spdlog::warn("[fastmap] setup failed: {}", e.what());
    }
}


// Called by the existing 100 ms / 2 s worker, independent of overlay/hotkeys.
// No game pointers or game APIs are touched here; logs never run per marker.
void goblin::map_timing::poll_profile()
{
    if (!g_profile_mode || !g_ready.load(std::memory_order_acquire) ||
        g_profile_done.load(std::memory_order_relaxed)) return;
    std::optional<ProfileReport> report;
    {
        std::lock_guard<std::mutex> lock(g_profile_mutex);
        report = g_profile.poll(now_ms());
    }
    if (!report) return;
    if (report->final) g_profile_done.store(true, std::memory_order_relaxed);
    for (unsigned i = 0; i < report->buckets.size(); ++i) {
        const auto &b = report->buckets[i];
        spdlog::info("[fastmap-profile] elapsed_ms={} final={} pending_calls={} bucket={} calls={} "
                     "original_us={} max_us={} first_thread={} mixed_threads={}",
                     report->elapsed_ms, report->final, report->pending_calls, i, b.calls, b.ns / 1000,
                     b.max_ns / 1000, b.first_thread, b.mixed_threads);
    }
}
