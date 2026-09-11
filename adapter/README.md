# Upstream 2.1.3 AP adapter — experimental

This package uses VirusAlex's **unmodified** 2.1.3 native renderer, with a small
companion retaining our existing AP client ABI. It replaces the old source-built
renderer for this configuration; the old renderer must not be loaded alongside it.
It is not an independently reconstructed implementation of upstream's renderer.

## Files and loading

* `MapForGoblins.dll`: our adapter; keep the existing me3 native entry pointing here.
* `MapForGoblins.upstream.dll`: the exact released vanilla 2.1.3 DLL, renamed only.
  The adapter loads this automatically. **Do not add a second me3 entry for it.**
* `MapForGoblins.ini`: upstream's configuration and native menu settings.
* `MapForGoblins.AP.ini`: AP restrictions, reloaded about once per second.
* `MapForGoblins.AP.log`: created at runtime with initialization diagnostics.

Keep both DLLs together. Start with a separate offline test profile and copied
save. The paired package has not been installed over the user's existing release.
Retain that release for rollback: restore its original single DLL and INI together.
No Rust client changes are required for the four existing AP exports.

The adapter accepts only upstream SHA-256
`ed984d5bb3ee49e304ab02e5ac1bc1bfc3a6368c2bc8743f85edefe2a73f2ea3`.
A different DLL is rejected **before loading**, with an explanation in the log.
Updating upstream requires auditing the new binary and rebuilding the adapter.
This vanilla adapter does not support the separate ERR release.

## AP behavior

The three switches under `[AP]` use `0`/`1`: `checks_only`, `progression_only`,
and `in_logic_only`. Defaults match the source fork: checks and in-logic enabled,
progression-only disabled. These switches are presently **INI controls**, not
entries in upstream's native menu. The fork's old overlay is not loaded.

Restrictions intersect upstream visibility and never reveal markers hidden by
upstream. Completion badges inherit the parent marker's AP restriction. Exact
baked MAP/ENEMY lots and boss defeat flags retain the existing lease semantics;
an active empty snapshot filters all unmatched mod markers, while a missing,
cleared, or expired snapshot restores upstream's ordinary visibility. Stock game
markers remain upstream's responsibility. Combined progression + logic requires
the same-check conjunction bit, not two unrelated checks.

Hover publishes upstream's final selected row through the existing copied cache;
handles are original numeric marker IDs, never pointers. Build and close callbacks
advance the hover generation. Original acquisition flags stay unknown, matching
the source fork. Lot-style snapshots are accepted but draw nothing, matching the
fork's removal of AP rings. Upstream retains its own labels, native menu, focus,
viewport management, caches, and marker ownership.

## Build and package

From the repository with VS2022 C++ tools and CMake 3.31.6:

```powershell
cmake -S adapter -B builds/adapter -A x64
cmake --build builds/adapter --config Release
ctest --test-dir builds/adapter -C Release --output-on-failure
python tools/package_upstream_adapter.py --upstream "path/to/Vanilla - MapForGoblins - DLL - v2.1.3.zip" --adapter builds/adapter/Release/MapForGoblins.dll --minhook-license builds/adapter/_deps/minhook-src/LICENSE.txt --output MapForGoblins-AP-upstream213-experimental.zip
```

The packager verifies the upstream hash, preserves notices, writes per-file hashes,
and refuses to overwrite an existing package. It does not copy files into a game
installation. It never includes the diagnostic client.

## Integration boundaries

Pinned upstream function RVAs: settings predicate `43a10`, native point snapshot
`444b0`, final hover callback `cdfb0`, map build `cc390`, close `8f390`. The snapshot
contains 40-byte entries with a visibility byte at `1c`; bit 63 of a completion
badge ID is stripped solely for AP identity matching. Static baked entries begin
at `1d7850`, stride 296, count 7039; metadata is copied from that image. Catalogue
rows have stride 112 at the vector rooted at `66ff48`; the original ID is at `18`.
The hover row global `68e120` is read after upstream's callback completes.

Only the upstream-owned snapshot visibility byte is additionally restricted.
The adapter never allocates, attaches, frees, or retains native marker children,
never reimplements game projection, and never writes event flags. Client ABI calls
copy data into a synchronized cache; they do not access game memory. The renderer
thread takes an immutable leased snapshot for a visibility pass. General speed
gains come from running upstream's renderer; no comparative FPS claim is made.

## Diagnostic client

`-DMFG_BUILD_SMOKE_CLIENT=ON` builds `adapter_smoke_client.dll`. For offline tests
only, add it to a separate me3 profile **without the real AP client**. Its sibling
`smoke.ini` contains `[Test] mode=0` (clear), `1` (active empty), `2` (one entry using
`table`, `lot`, `flags`), or `3` (stop renewal and await expiry). It records the
public query/hover/snapshot results to `smoke.log`. Never distribute or load this
test client in an ordinary AP session: it replaces the snapshot being tested.

## Validation status

Windows Release builds with warnings as errors. Model tests cover exact identity
decoding, truncation/duplicate rejection, badge inheritance, unknown identities,
active-empty filtering, and same-check progression/logic semantics. A live offline
run on game 1.17.1 loaded both DLLs, matched all 7039 rows, and displayed the map.
The diagnostic API client observed valid MAP-lot hover identities, with fresh
generation after repeated map close/reopen and underground/back transitions.

Controlled tests in a fixed Liurnia view, using only the public snapshot API:

| State | Attached mod children | Stock children |
|---|---:|---:|
| No active snapshot | 162 | 383 |
| Active empty snapshot, checks-only | 0 | 383 |
| MAP lot 34110120, CHECK | 1 | 383 |
| Combined progression + logic, flags 7 (no same-check bit) | 0 | 383 |
| Same lot, flags 15 (same-check bit present) | 1 | 383 |
| Stop renewal; lease expires | 162 | 383 |

All snapshot submissions returned OK. Counts came from read-only observations of
upstream's parent list and native records; this is not a frame-time benchmark.
The rejected-release test loads the adapter in a separate process with a bogus
upstream file and verifies that upstream is never loaded and the API remains
unavailable. The original AP save hash stayed unchanged throughout live testing.

Full AP-server play, warp/character switching, every layer/focus/category
combination, boss-badge filtering in game, and comparative frame-time benchmarks
remain unverified. Keep this package experimental until those gates are complete.
The final build additionally skips identity lookups and the restriction pass when
no AP restriction is active; this equivalent fast path was compiled/model-tested
after the live run.
