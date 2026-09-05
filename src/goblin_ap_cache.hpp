#pragma once
#include "../include/mfg_ap_readonly_v1.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace goblin::ap
{
struct MarkerIdentity
{
    // Internal pointer value is used ONLY as an opaque lookup key. Never dereferenced.
    uintptr_t row_key;
    uint64_t handle;
    uint32_t lot_table, lot_row;
};

// Owns copies only. The row builder and native hover hook publish; exported calls
// never inspect game memory or upstream's unsynchronized row containers.
class HoverCache
{
public:
    void invalidate_rows()
    {
        std::lock_guard lock(mutex_);
        advance_generation();
        initialized_ = false;
        rows_.clear();
        clear_hover();
    }

    bool install_rows(const std::vector<MarkerIdentity>& rows)
    {
        std::unordered_map<uintptr_t, MarkerIdentity> next;
        std::unordered_map<uint64_t, bool> handles;
        for (const auto& row : rows)
        {
            if (!row.row_key || !row.handle ||
                row.lot_table > MFG_AP_LOT_ENEMY ||
                ((row.lot_table == MFG_AP_LOT_UNKNOWN) != (row.lot_row == 0)) ||
                !next.emplace(row.row_key, row).second ||
                !handles.emplace(row.handle, true).second)
            {
                invalidate_rows();
                return false;
            }
        }
        std::lock_guard lock(mutex_);
        advance_generation();
        rows_ = std::move(next);
        initialized_ = !rows_.empty();
        clear_hover();
        return initialized_;
    }

    void set_hooks_ready(bool ready)
    {
        std::lock_guard lock(mutex_);
        hooks_ready_ = ready;
        clear_hover();
    }

    void set_active(bool active)
    {
        std::lock_guard lock(mutex_);
        active_ = active;
        advance_generation();
        clear_hover();
    }

    // Called before native map pin construction: old handles cannot survive it.
    void map_rebuild()
    {
        std::lock_guard lock(mutex_);
        advance_generation();
        clear_hover();
    }

    void observe(uintptr_t row_key, uint64_t now_ms)
    {
        std::lock_guard lock(mutex_);
        clear_hover();
        if (!ready()) return;
        auto it = rows_.find(row_key);
        if (it == rows_.end()) return;
        const auto& row = it->second;
        hover_.status = MFG_AP_HOVER;
        hover_.handle = row.handle;
        // Upstream only bakes lot identity. A presentation disable flag is NOT
        // necessarily an acquisition flag, so leave original_flag unknown.
        hover_.lot_table = row.lot_table;
        hover_.lot_row = row.lot_row;
        observed_ms_ = now_ms;
    }

    uint32_t query(uint32_t abi, MFG_AP_InfoV1* out, uint32_t capacity)
    {
        if (abi != MFG_AP_ABI_V1) return MFG_AP_UNSUPPORTED_ABI;
        if (!out || capacity < sizeof(*out)) return MFG_AP_BAD_ARGUMENT;
        std::lock_guard lock(mutex_);
        *out = {MFG_AP_ABI_V1, sizeof(*out),
                ready() ? MFG_AP_CAP_HOVER_V1 : 0u, sizeof(MFG_AP_HoverV1)};
        return MFG_AP_OK;
    }

    uint32_t copy(MFG_AP_HoverV1* out, uint32_t capacity, uint64_t now_ms)
    {
        if (!out || capacity < sizeof(*out)) return MFG_AP_BAD_ARGUMENT;
        std::lock_guard lock(mutex_);
        if (!ready()) return MFG_AP_UNAVAILABLE;
        *out = hover_;
        out->struct_size = sizeof(*out);
        out->generation = generation_;
        if (out->status == MFG_AP_HOVER)
        {
            const auto age = now_ms >= observed_ms_ ? now_ms - observed_ms_ :
                             std::numeric_limits<uint64_t>::max();
            out->age_ms = static_cast<uint32_t>(std::min<uint64_t>(
                age, std::numeric_limits<uint32_t>::max()));
            if (age > 300)
            {
                // Closed maps have no further heartbeat. Expire the copied value,
                // retaining no identity that a caller could mistake for current hover.
                *out = {sizeof(*out), MFG_AP_NO_HOVER, generation_, 0, 0, 0, 0, 0};
            }
        }
        return MFG_AP_OK;
    }

private:
    bool ready() const { return initialized_ && active_ && hooks_ready_ && generation_ != 0 && !exhausted_; }
    void clear_hover() { hover_ = {}; observed_ms_ = 0; }
    void advance_generation()
    {
        // Wraparound must fail closed, not resurrect a handle from epoch one.
        if (generation_ == std::numeric_limits<uint64_t>::max() || exhausted_)
        {
            exhausted_ = true;
            generation_ = 0;
        }
        else ++generation_;
    }
    std::mutex mutex_;
    std::unordered_map<uintptr_t, MarkerIdentity> rows_;
    MFG_AP_HoverV1 hover_{};
    uint64_t generation_ = 0, observed_ms_ = 0;
    bool initialized_ = false, active_ = false, hooks_ready_ = false, exhausted_ = false;
};

HoverCache& cache();
}
