#pragma once
#include <bit>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace goblin::worldmap_probe
{
// Exact successful converter results only; no borrowed VM/row pointers and no
// inferred tile transform. See docs/upstream-213-performance.md for binary RVAs.
struct ProjectionKey
{
    uint32_t packed, x, z;
    static ProjectionKey make(uint32_t packed, float x, float z)
    {
        return {packed, std::bit_cast<uint32_t>(x), std::bit_cast<uint32_t>(z)};
    }
    bool operator==(const ProjectionKey&) const = default;
};
struct ProjectionHash
{
    size_t operator()(const ProjectionKey& k) const
    {
        uint64_t h = k.packed;
        h = (h ^ k.x) * 1099511628211ull;
        h = (h ^ k.z) * 1099511628211ull;
        return static_cast<size_t>(h);
    }
};
struct Projection { float u, v; };

class ProjectionCache
{
public:
    explicit ProjectionCache(size_t capacity = 16384) : capacity_(capacity) {}

    bool find(uint64_t epoch, ProjectionKey key, Projection& out)
    {
        std::lock_guard lock(mutex_);
        if (!advance(epoch)) return false;
        const auto it = values_.find(key);
        if (it == values_.end()) return false;
        out = it->second;
        return true;
    }

    void remember(uint64_t epoch, ProjectionKey key, Projection value)
    {
        if (!std::isfinite(value.u) || !std::isfinite(value.v)) return;
        std::lock_guard lock(mutex_);
        // A converter call that finishes after invalidation cannot repopulate
        // the new generation. Capacity exhaustion leaves misses uncached.
        if (epoch != epoch_ || values_.size() >= capacity_) return;
        values_.insert_or_assign(key, value);
    }

private:
    bool advance(uint64_t epoch)
    {
        if (epoch < epoch_) return false;
        if (epoch != epoch_) { values_.clear(); epoch_ = epoch; }
        return true;
    }
    std::mutex mutex_;
    std::unordered_map<ProjectionKey, Projection, ProjectionHash> values_;
    uint64_t epoch_ = 0;
    size_t capacity_;
};
}
