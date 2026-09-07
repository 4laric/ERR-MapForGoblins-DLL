#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace goblin::map_timing {
// Pure accounting. Caller serializes access. Time is monotonic milliseconds;
// original-call duration is nanoseconds. These are call windows, NOT frames.
struct ProfileBucket {
    uint64_t calls = 0, ns = 0, max_ns = 0;
    uint32_t first_thread = 0;
    bool mixed_threads = false;
};
struct ProfileReport {
    std::array<ProfileBucket, 4> buckets{};
    uint64_t elapsed_ms = 0, pending_calls = 0;
    bool final = false;
};
class ProfileWindow {
    std::optional<uint64_t> start_;
    uint64_t last_report_ = 0, pending_ = 0;
    bool accepting_ = true, done_ = false;
    std::array<ProfileBucket, 4> buckets_{};
public:
    static constexpr uint64_t duration_ms = 30000;
    bool begin(uint64_t now) {
        if (done_ || !accepting_) return false;
        if (!start_) { start_ = now; last_report_ = now; }
        if (now - *start_ >= duration_ms) { accepting_ = false; return false; }
        ++pending_;
        return true;
    }
    void finish(unsigned bucket, uint64_t ns, uint32_t thread) {
        --pending_;
        auto &b = buckets_.at(bucket);
        if (!b.calls) b.first_thread = thread;
        else if (b.first_thread != thread) b.mixed_threads = true;
        ++b.calls;
        b.ns += ns;
        if (ns > b.max_ns) b.max_ns = ns;
    }
    std::optional<ProfileReport> poll(uint64_t now) {
        if (!start_ || done_) return {};
        if (now - *start_ >= duration_ms) accepting_ = false;
        const bool final = !accepting_ && (pending_ == 0 || now - *start_ >= duration_ms + 5000);
        if (!final && now - last_report_ < 1000) return {};
        last_report_ = now;
        ProfileReport out{buckets_, now - *start_, pending_, final};
        // Reports are cumulative so slow calls crossing windows are not lost.
        if (final) done_ = true;
        return out;
    }
};
}
