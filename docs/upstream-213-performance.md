# Upstream 2.1.3 performance reconstruction

Investigated 2026-09-11. **Partial port, not performance parity.** The supplied
release contains a new native marker manager, not just a faster version of our
old layout hook. This branch ports exact projection caching into our existing
focus-highlight path. General map FPS, first-open time, and upstream's viewport
renderer remain unverified/unported. No upstream DLL was loaded or executed.

## Inputs and reproducibility

* Supplied archive: `Vanilla - MapForGoblins - DLL - v2.1.3.zip`.
* DLL: 6,843,392 bytes, AMD64, preferred image base `0x180000000`.
* DLL SHA-256: `ed984d5bb3ee49e304ab02e5ac1bc1bfc3a6368c2bc8743f85edefe2a73f2ea3`.
* Fork baseline: `c015cb3` on `4laric/ERR-MapForGoblins-DLL`'s `master`.
* Public upstream `master`: `a254433` (2.0.6). Its other `main` head is
  `95cee42`; neither branch supplies the 2.1.3 native renderer source.
* The archive's license retains VirusAlex's and Gacsam's MIT-style notices.
  The fork's existing `LICENSE.txt` is retained.

All addresses below are **RVAs in this exact DLL**, not game addresses, portable
hook signatures, or function names supplied by debug symbols. The exception
directory contains 5,325 unwind regions; optimized functions can span multiple
regions. A log string identifies an investigation lead, not runtime proof.

Reproduce the cited disassembly without extracting or running the DLL:

```powershell
python -m pip install pefile==2024.8.26 capstone==5.0.7
python tools/analyze_mfg_release.py "path/to/release.zip" --output scratch/evidence-213 `
  --rva 8f200 --rva 8f390 --rva 97820 --rva a3b8d --rva 91190 `
  --rva 90b80 --rva 90d50 --rva d0910
```

The tool writes the DLL hash, dependency versions, direct RIP-relative string
references and selected annotated disassembly. It is not a decompiler and does
not discover every indirect reference. Keep the archive and generated evidence
local; no game data or release binary is committed.

## What the binary actually does

| Mechanism | Instruction evidence | Consequence for this fork |
| --- | --- | --- |
| Suppress stock map refresh | `0x8f212` and `0x8f225..0x8f234` compare the caller return address with two resolved gates. The matching path ends in zero return at `0x8f377..0x8f383`; the unmatched path jumps to the original at `0x8f2e4..0x8f2fe`. | This is not the old delayed replay queue. Bypassing our stock layout without the replacement renderer would omit required work. Do not transplant this hook alone. |
| Native marker lifecycle | The map-tick region starts at `0x97820`, references the native-tick labels at `0x97890/0x9789c`, and drives native worker routines. The close wrapper at `0x8f390` calls cleanup at `0x8f3b7` before the original at `0x8f3c6`, then resets manager state. | Upstream owns additional lifetime-sensitive state absent from our fork. Its allocation, parent ownership and teardown need reconstruction together. |
| Viewport-based detach | `0xa3c03..0xa3c32` establishes capacities and zoom-scaled extents. `0xa3d7f..0xa3da5` compares absolute point-to-view-center deltas against both extents. Out-of-window/disabled records enter candidate lists; `0xa3d4a` caps the list at `0x400`. `0xa3ef4` calls the child-index helper. | Upstream reduces the live display tree, rather than merely making offscreen icons transparent. Our existing build-time category pruning does not implement this. |
| Efficient child lookup for detach | `0x911c2..0x911cf` reads the parent's child array/count. `0x91218..0x91248` binary-searches each child in a candidate pointer list and writes matching indices at `0x9124a`. | Sorting candidates and walking the live child list avoids a full linear candidate search per child. Correct removal order and ownership still require validation. |
| Change-driven emphasis | `0x97c89..0x97cd1` hashes a map/config-derived value and four floats. `0x97cd7..0x97cde` skips invalidation when the hash matches; otherwise `0x97cf2..0x97cf9` marks dirty work and resets its cursor. | The renderer avoids redoing unchanged emphasis state. This fork does not have that native emphasis implementation to optimize. |
| Exact projection memoization | `0xd0c5d..0xd0c89` hashes area, grid and coordinate bits. `0xd0cb2..0xd0cd4` compares the full key. A hit copies two output floats at `0xd0cf6..0xd0d04`; a miss can call the converter wrapper at `0xd0da0` and insert its result at `0xd0e07`. | Transferable to repeated focus-highlight dungeon projections. Implemented here, with bounded storage and explicit generation invalidation. |
| Inferred tile-origin fallback | `0xd0e27..0xd0e3b` derives an origin from a successful projection; later results are checked at `0xd0e44..0xd0e8a`. `0xd0ff0..0xd1008` uses an accepted origin if the live converter is unavailable. | Not ported: this changes failure behavior and assumes a tile-wide affine transform. Exact copied results are sufficient for the cache port; no inferred positions are returned. |
| Memory-region cache | `0x90d9a..0x90db4` accesses thread-local storage. `0x90dd2..0x90df0` scans eight cached regions. `0x90dfe..0x90e0a` tests a 5,000-ms age, and a miss calls `0x90b80`, whose `0x90bde` invokes `VirtualQuery`. Range checking includes overflow and a four-region traversal limit. | This supports upstream's new native memory layer. Our projection path uses SEH, not per-point VirtualQuery; adding this cache there would add machinery rather than remove an existing cost. |

**Interpretation:** moving marker work out of the stock widget path, keeping fewer
native children attached, and updating only dirty state plausibly account for
much of the general improvement. The instructions establish those mechanisms,
not their contribution in milliseconds or proof that every branch is active
in the user's session. Only an A/B capture can attribute the gains.

## Implemented port and its limits

The exact cache stores `(packed map ID, x bits, z bits) -> (u, v)`. It retains no
game pointers, does not cache failures/nonfinite results, and caps storage at
16,384 points. Overflow remains a normal converter miss. Pan and zoom still use
the live `MapView`; screen coordinates are never cached.

Our build hook invalidates projections before native pin construction, covering
layer rebuilds and address reuse. A changed captured VM or stale heartbeat also
advances the epoch. Context publication and snapshotting share a short mutex;
the engine's steady-state converter hook does not take that mutex. Results
computed across an epoch change are discarded. Older cache readers cannot rewind
the cache or populate a newer generation. If the build hook has never run,
caching stays disabled.

The existing 500-ms heartbeat and SEH-protected converter fallback remain. They
are **not proof of VM ownership**: a cache miss can still call the engine through
the pre-existing borrowed pointer. This change does not claim to solve that
larger lifetime problem. Likewise, game playtests must confirm invalidation for
all actual map transitions; the pure cache tests cannot establish game lifetime.

This helps the region/category **focus-highlight** path for underground/dungeon
points. Areas 60/61 already use direct affine math and bypass it. With focus
highlighting inactive, this cache is not queried. It does not restore the removed
AP progression rings, skip stock layout, or change AP visibility rules.

## Validation

* MSVC `/std:c++20 /EHsc /W4 /WX /O2`: projection-cache tests, existing
  map-profile tests, and existing live-label tests pass.
* Cache tests cover exact keys, coordinate/map separation, capacity exhaustion,
  nonfinite output rejection, rebuild invalidation, late old-generation writes,
  and concurrent readers/writers.
* Synthetic all-dungeon replay: 6,900 distinct points, 120 frames, generation
  change after frame 60. All 828,000 results match the oracle; converter calls
  fall to 13,800 (one per point per generation). This is an operation-count test,
  **not a 60x FPS claim** and not a representative distribution of vanilla pins.
* Existing baseline-manifest Python tests: 3 pass.
* Full Windows Release DLL compiles/links against the public ERR tables.
* Full Windows Release DLL also compiles/links against the pinned vanilla input
  archive (`mfg-vanilla-transfer-20260905`), verified SHA-256
  `0a7b8fa931609fe0d778b356fff5259e150d667a0ee2c8daeed38c4285fddc39`.
* Live vanilla/AP frame time and visual behavior: **not tested**.

## Remaining renderer work

The main gains remain a renderer port, not a small patch. Before enabling the
stock-refresh bypass, reconstruct the native child's ownership and creation
contract, the parent anchoring/re-anchoring rules, close/warp/title cleanup, and
the actual game-function signatures. Implement these behind a complete fallback
to the stock renderer; a partial native build must not leave the stock pins
suppressed. The 2.1.3 native menu/overlay changes are also separate from the old
fork's overlay, so a whole-DLL comparison would otherwise conflate costs.

Then port viewport reconciliation and dirty-state updates while preserving AP
check filters, collection changes, manual hides, boss checkmarks, fragment gates,
native hover and labels. Test with the same character/settings at fixed zoom:
first open, stationary map, pan/zoom, layer switches, rapid close/reopen, warp,
return to title, and character switch. Record CPU frame-time percentiles and
open latency separately for stock fork, this branch and supplied upstream, plus
live child counts and allocation/memory-check counters. This report is a map of
the implementation work; it does not mark those gates complete.
