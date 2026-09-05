# Experimental source-built AP hover bridge

This fork's first AP milestone is a read-only diagnostic, not a released integrated
tracker. It adds two exports absent from the stock 2.1.3 DLL. The public source
baseline remains upstream a254433 (2.0.6); do not label its rebuilt output 2.1.3.

The C ABI is in include/mfg_ap_readonly_v1.h, mirrored in the AP client's
docs/include directory. The client explicitly requests it with !mfgprobe.
No DLL is automatically loaded and no AP completion or presentation is written.

## Publication and identity

The row builder copies each injected row's baked lot table and row ID into a
synchronized registry. The native focused-pin hook compares its existing row
pointer against opaque internal keys, then publishes a small value snapshot.
The API only copies that snapshot; it never dereferences game pointers, traverses
the live category vector, or performs game-memory writes on the caller's thread.

Upstream does not provide a guaranteed baked acquisition flag in its generated
entry. The bridge therefore exports original_flag=0 (unknown). It does not turn
textDisableFlagId1, a presentation condition, into an acquisition claim. Table
1 is ItemLotParam_map; table 2 is ItemLotParam_enemy. Loot identity remains from
the generated entry even if runtime lots or display labels change.

Handles are local positive integers, not pointers. Row replacement, native map
pin rebuild and enable/disable retire previous generations and hover. Required
hook failure or unavailable row registry withholds the capability. A stopped
hover heartbeat expires after 300 ms. Unsupported native pins report no matching
hover, while uninitialized/disabled bridges report unavailable.

Generation/state guards protect this copied interface; they do not establish the
safety of upstream's game hooks or memory layouts on a new executable.

## Checks and limits

Host test (no game files or Windows dependencies):

    g++ -std=c++20 -Wall -Wextra -Werror -pthread -fsanitize=address,undefined \
      tests/ap_cache_test.cpp src/goblin_ap_api.cpp -o /tmp/ap-cache-test
    /tmp/ap-cache-test

The test exercises actual exports, layout, undersized buffers, unknown identity,
missing hooks/rows, invalid batches, map rebuild/disable/replacement, stale
heartbeat, clock anomaly, and concurrent producer/readers. Disabling expiration
makes the test fail. CI runs the same host test.

A full Windows compile and real game test are separate gates. Do not infer runtime
compatibility from host tests or unique AOB matches. Test this fork as the only
map engine in an isolated loader profile; duplicate stock+fork detection and a
release installer are not implemented. Never hot-swap hooked DLLs.

Remaining work: current-game baseline acceptance, seed registry consumption in F6,
continuous opt-in polling, multi-check selection, focus/presentation commands,
review context, duplicate-engine prevention, packaging and performance testing.

For offline coverage against the actual regenerated profile, the inventory helper
links the generated entries directly rather than guessing their C++ text format:

    g++ -std=c++20 -Isrc/generated_vanilla tools/export_ap_marker_lots.cpp \
      src/generated_vanilla/goblin_map_data.cpp -o /tmp/mfg-marker-lots
    /tmp/mfg-marker-lots > /tmp/mfg-marker-lots.csv

Fingerprint the generated input with baseline_manifest.py and retain that profile
alongside the CSV. This inventories baked source identities; native config, actual
loaded rows and current-seed membership still need separate validation.
