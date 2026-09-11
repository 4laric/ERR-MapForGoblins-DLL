#include "../src/goblin_projection_cache.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

int main()
{
    using namespace goblin::worldmap_probe;
    ProjectionCache cache;
    Projection out{};
    const auto k = ProjectionKey::make(0x0c010000, 12.5f, -7.0f);
    assert(!cache.find(1, k, out));
    cache.remember(1, k, {100, 200});
    assert(cache.find(1, k, out) && out.u == 100 && out.v == 200);
    // Same point on a different map, and neighboring coordinates, are distinct.
    assert(!cache.find(1, ProjectionKey::make(0x0c020000, 12.5f, -7), out));
    assert(!cache.find(1, ProjectionKey::make(0x0c010000, 12.5001f, -7), out));
    assert(!cache.find(1, ProjectionKey::make(0x0c010000, 12.5f, -7.001f), out));
    assert(!cache.find(2, k, out));
    cache.remember(1, k, {900, 900}); // old in-flight converter result
    assert(!cache.find(2, k, out));
    cache.remember(2, k, {300, 400});
    assert(!cache.find(1, k, out)); // old reader cannot rewind the epoch
    assert(cache.find(2, k, out) && out.u == 300 && out.v == 400);

    ProjectionCache bounded(1);
    assert(!bounded.find(1, k, out));
    bounded.remember(1, k, {std::numeric_limits<float>::infinity(), 1});
    assert(!bounded.find(1, k, out));
    bounded.remember(1, k, {1, 2});
    const auto k2 = ProjectionKey::make(12, 1, 2);
    bounded.remember(1, k2, {3, 4});
    assert(!bounded.find(1, k2, out));
    assert(bounded.find(1, k, out));
    assert(!bounded.find(2, k2, out));
    bounded.remember(2, k2, {3, 4});
    assert(bounded.find(2, k2, out));

    // Replay a map-sized workload and compare every cached answer to the oracle.
    // This is a call-count benchmark, not a claim about live frame times.
    ProjectionCache replay;
    unsigned calls = 0;
    for (unsigned frame = 0; frame < 120; ++frame)
        for (unsigned marker = 0; marker < 6900; ++marker)
        {
            const auto key = ProjectionKey::make(0x0c010000, float(marker), -float(marker));
            const uint64_t epoch = frame < 60 ? 1 : 2;
            const Projection expected{float(marker) + float(epoch), float(marker) * 2};
            if (!replay.find(epoch, key, out))
            {
                ++calls;
                out = expected;
                replay.remember(epoch, key, out);
            }
            assert(out.u == expected.u && out.v == expected.v);
        }
    assert(calls == 13800);
    std::cout << "projection replay: 828000 requests, " << calls << " converter calls, exact outputs\n";

    // Readers finishing in an old generation cannot corrupt a new snapshot.
    ProjectionCache concurrent;
    assert(!concurrent.find(2, k, out));
    concurrent.remember(2, k, {3, 4});
    std::vector<std::thread> workers;
    for (unsigned t = 0; t < 4; ++t)
        workers.emplace_back([&] {
            for (unsigned i = 0; i < 10000; ++i) {
                Projection result{};
                concurrent.remember(1, k, {9, 9});
                assert(!concurrent.find(1, k, result));
                assert(concurrent.find(2, k, result) && result.u == 3 && result.v == 4);
            }
        });
    for (auto& worker : workers) worker.join();
}
