#pragma once
#include "../include/mfg_ap_readonly_v1.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
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

struct LotStyle
{
    uint32_t lot_table, lot_row, style;
};

struct LotStyleSnapshot
{
    uint64_t generation;
    std::vector<LotStyle> entries;
};

struct CheckStateSnapshot
{
    uint64_t generation;
    std::unordered_map<uint64_t, uint32_t> flags;
};

inline uint64_t check_key(uint32_t table, uint32_t row)
{
    return (static_cast<uint64_t>(table) << 32) | row;
}

// A snapshot is an additional visibility restriction, never a reveal request.
inline bool check_filter_allows(const CheckStateSnapshot* snapshot, uint32_t table,
                                uint32_t row, bool checks_only, bool progression_only,
                                bool in_logic_only)
{
    if (!snapshot || (!checks_only && !progression_only && !in_logic_only)) return true;
    const auto it = snapshot->flags.find(check_key(table, row));
    const uint32_t flags = it == snapshot->flags.end() ? 0u : it->second;
    if (!(flags & MFG_AP_CHECK)) return false;
    if (progression_only && in_logic_only) return (flags & MFG_AP_PROGRESSION_IN_LOGIC) != 0;
    if (progression_only && !(flags & MFG_AP_PROGRESSION)) return false;
    if (in_logic_only && !(flags & MFG_AP_IN_LOGIC)) return false;
    return true;
}

// A progression classification applies to every visible representation of its
// lot. A hint style alone remains ambiguous when several native pins share it.
inline uint32_t check_marker_style(uint32_t requested_style, uint32_t flags, size_t multiplicity)
{
    const bool progression = (flags & (MFG_AP_CHECK | MFG_AP_PROGRESSION)) ==
                             (MFG_AP_CHECK | MFG_AP_PROGRESSION);
    if (multiplicity != 1)
        return progression ? MFG_AP_STYLE_ORANGE : MFG_AP_STYLE_NORMAL;
    return requested_style ? requested_style :
           (progression ? MFG_AP_STYLE_ORANGE : MFG_AP_STYLE_NORMAL);
}

// Owner-thread apply acknowledgement. A partially failed write remains dirty,
// even if the requested settings later return to the last successful signature.
class CheckFilterRefresh
{
public:
    bool due(uint64_t generation, unsigned options, uint64_t now_ms) const
    {
        if (dirty_ && now_ms >= last_failure_ms_ && now_ms - last_failure_ms_ < 100)
            return false;
        return !initialized_ || dirty_ || generation != generation_ || options != options_;
    }
    void complete(uint64_t generation, unsigned options, uint64_t now_ms, bool success)
    {
        if (!success)
        {
            dirty_ = true;
            last_failure_ms_ = now_ms;
            return;
        }
        generation_ = generation;
        options_ = options;
        initialized_ = true;
        dirty_ = false;
    }
private:
    uint64_t generation_ = 0, last_failure_ms_ = 0;
    unsigned options_ = 0;
    bool initialized_ = false, dirty_ = false;
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
        clear_lot_styles();
        clear_check_states();
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
        clear_lot_styles();
        clear_check_states();
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
        if (!active) { clear_lot_styles(); clear_check_states(); }
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

    // Replaces the complete client-owned presentation snapshot. All validation
    // and copying happen before publishing; caller memory is never retained.
    uint32_t set_lot_styles(uint32_t abi, const MFG_AP_LotStyleV1* entries,
                            uint32_t count, uint32_t lease_ms, uint64_t now_ms)
    {
        if (abi != MFG_AP_ABI_V1) return MFG_AP_UNSUPPORTED_ABI;
        if (count == 0)
        {
            if (entries || lease_ms != 0) return MFG_AP_BAD_ARGUMENT;
            std::lock_guard lock(mutex_);
            clear_lot_styles();
            return MFG_AP_OK;
        }
        if (!entries || count > MFG_AP_LOT_STYLE_MAX_ENTRIES ||
            lease_ms < MFG_AP_LOT_STYLE_MIN_LEASE_MS ||
            lease_ms > MFG_AP_LOT_STYLE_MAX_LEASE_MS ||
            now_ms > std::numeric_limits<uint64_t>::max() - lease_ms)
            return MFG_AP_BAD_ARGUMENT;

        std::vector<LotStyle> next;
        next.reserve(count);
        std::unordered_set<uint64_t> identities;
        identities.reserve(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto& e = entries[i];
            const uint64_t identity = lot_key(e.lot_table, e.lot_row);
            if ((e.lot_table != MFG_AP_LOT_MAP && e.lot_table != MFG_AP_LOT_ENEMY) ||
                e.lot_row == 0 ||
                (e.style != MFG_AP_STYLE_ORANGE && e.style != MFG_AP_STYLE_YELLOW) ||
                !identities.emplace(identity).second)
                return MFG_AP_BAD_ARGUMENT;
            next.push_back({e.lot_table, e.lot_row, e.style});
        }

        std::lock_guard lock(mutex_);
        if (style_generation_ == std::numeric_limits<uint64_t>::max())
        {
            // Do not let a wrapped generation make an old cached snapshot look current.
            clear_lot_styles();
            return MFG_AP_UNAVAILABLE;
        }
        ++style_generation_;
        lot_styles_ = std::make_shared<LotStyleSnapshot>(
            LotStyleSnapshot{style_generation_, std::move(next)});
        styles_published_ms_ = now_ms;
        styles_expiry_ms_ = now_ms + lease_ms;
        return MFG_AP_OK;
    }

    // Immutable shared ownership lets the render path take one lock per frame,
    // not one lock per marker. Clock reversal and expiry both fail closed.
    std::shared_ptr<const LotStyleSnapshot> active_lot_styles(uint64_t now_ms)
    {
        std::lock_guard lock(mutex_);
        if (!active_ || !lot_styles_) return {};
        if (now_ms < styles_published_ms_ || now_ms >= styles_expiry_ms_)
        {
            clear_lot_styles();
            return {};
        }
        return lot_styles_;
    }

    uint32_t set_check_states(uint32_t abi, const MFG_AP_CheckStateV1* entries,
                              uint32_t count, uint32_t lease_ms, uint64_t now_ms)
    {
        if (abi != MFG_AP_ABI_V1) return MFG_AP_UNSUPPORTED_ABI;
        if (!count && !lease_ms)
        {
            if (entries) return MFG_AP_BAD_ARGUMENT;
            std::lock_guard lock(mutex_);
            clear_check_states();
            return MFG_AP_OK;
        }
        if ((count && !entries) || count > MFG_AP_LOT_STYLE_MAX_ENTRIES ||
            lease_ms < MFG_AP_LOT_STYLE_MIN_LEASE_MS ||
            lease_ms > MFG_AP_LOT_STYLE_MAX_LEASE_MS ||
            now_ms > std::numeric_limits<uint64_t>::max() - lease_ms)
            return MFG_AP_BAD_ARGUMENT;
        auto next = std::make_shared<CheckStateSnapshot>();
        next->flags.reserve(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto& e = entries[i];
            if ((e.lot_table != MFG_AP_LOT_MAP && e.lot_table != MFG_AP_LOT_ENEMY) ||
                !e.lot_row || !(e.flags & MFG_AP_CHECK) || (e.flags & ~15u) ||
                ((e.flags & MFG_AP_PROGRESSION_IN_LOGIC) &&
                 (e.flags & (MFG_AP_PROGRESSION | MFG_AP_IN_LOGIC)) !=
                    (MFG_AP_PROGRESSION | MFG_AP_IN_LOGIC)) ||
                !next->flags.emplace(check_key(e.lot_table, e.lot_row), e.flags).second)
                return MFG_AP_BAD_ARGUMENT;
        }
        std::lock_guard lock(mutex_);
        if (check_states_ && now_ms >= checks_published_ms_ && now_ms < checks_expiry_ms_ &&
            check_states_->flags == next->flags)
        {
            // Heartbeats renew the lease without rebuilding unchanged native visibility.
            checks_published_ms_ = now_ms;
            checks_expiry_ms_ = now_ms + lease_ms;
            return MFG_AP_OK;
        }
        if (check_generation_ == std::numeric_limits<uint64_t>::max())
        {
            clear_check_states();
            return MFG_AP_UNAVAILABLE;
        }
        next->generation = ++check_generation_;
        check_states_ = std::move(next);
        checks_published_ms_ = now_ms;
        checks_expiry_ms_ = now_ms + lease_ms;
        return MFG_AP_OK;
    }

    std::shared_ptr<const CheckStateSnapshot> active_check_states(uint64_t now_ms)
    {
        std::lock_guard lock(mutex_);
        if (!active_ || !check_states_) return {};
        if (now_ms < checks_published_ms_ || now_ms >= checks_expiry_ms_)
        {
            clear_check_states();
            return {};
        }
        return check_states_;
    }

    uint32_t query(uint32_t abi, MFG_AP_InfoV1* out, uint32_t capacity)
    {
        if (abi != MFG_AP_ABI_V1) return MFG_AP_UNSUPPORTED_ABI;
        if (!out || capacity < sizeof(*out)) return MFG_AP_BAD_ARGUMENT;
        std::lock_guard lock(mutex_);
        const uint32_t capabilities = MFG_AP_CAP_LOT_STYLE_OVERLAY_V1 | MFG_AP_CAP_CHECK_STATES_V1 |
            (ready() ? MFG_AP_CAP_HOVER_V1 : 0u);
        *out = {MFG_AP_ABI_V1, sizeof(*out), capabilities, sizeof(MFG_AP_HoverV1)};
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
    static uint64_t lot_key(uint32_t table, uint32_t row)
    {
        return (static_cast<uint64_t>(table) << 32) | row;
    }
    bool ready() const { return initialized_ && active_ && hooks_ready_ && generation_ != 0 && !exhausted_; }
    void clear_hover() { hover_ = {}; observed_ms_ = 0; }
    void clear_lot_styles()
    {
        lot_styles_.reset();
        styles_published_ms_ = 0;
        styles_expiry_ms_ = 0;
    }
    void clear_check_states()
    {
        check_states_.reset();
        checks_published_ms_ = checks_expiry_ms_ = 0;
    }
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
    std::shared_ptr<const LotStyleSnapshot> lot_styles_;
    std::shared_ptr<const CheckStateSnapshot> check_states_;
    uint64_t check_generation_ = 0, checks_published_ms_ = 0, checks_expiry_ms_ = 0;
    uint64_t generation_ = 0, observed_ms_ = 0;
    uint64_t style_generation_ = 0, styles_published_ms_ = 0, styles_expiry_ms_ = 0;
    bool initialized_ = false, active_ = false, hooks_ready_ = false, exhausted_ = false;
};

HoverCache& cache();
}
