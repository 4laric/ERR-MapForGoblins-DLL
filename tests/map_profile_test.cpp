#include "../src/goblin_map_profile.hpp"
#include <cassert>
using goblin::map_timing::ProfileWindow;
int main() {
    ProfileWindow p;
    assert(!p.poll(100000)); // Idle time before map use does not spend the budget.
    assert(p.begin(100001));
    p.finish(0, 2500, 12);
    assert(!p.poll(100999));
    auto r = p.poll(101001);
    assert(r && !r->final && r->buckets[0].calls == 1);
    assert(p.begin(101002));
    p.finish(0, 9000, 13);
    r = p.poll(102001);
    assert(r->buckets[0].calls == 2 && r->buckets[0].ns == 11500);
    assert(r->buckets[0].max_ns == 9000 && r->buckets[0].mixed_threads);
    assert(p.begin(130000)); // In-flight work straddles the deadline.
    assert(!p.begin(130001));
    r = p.poll(130001);
    assert(r && !r->final);
    p.finish(3, 1500000, 12);
    r = p.poll(130002);
    assert(r && r->final && r->buckets[3].calls == 1);
    assert(r->buckets[1].calls == 0 && r->buckets[2].calls == 0);
    assert(!p.begin(140000) && !p.poll(140000));
    ProfileWindow stalled;
    assert(stalled.begin(1));
    r = stalled.poll(35001);
    assert(r && r->final && r->pending_calls == 1);
    assert(!stalled.poll(36001)); // A stuck original call cannot produce endless logs.
    stalled.finish(0, 36000000000ULL, 1);
    assert(!stalled.poll(37001));
}
