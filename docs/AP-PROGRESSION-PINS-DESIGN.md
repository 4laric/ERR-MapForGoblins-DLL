# Progression aura: showing progression checks without per-frame cost

Status: implemented 2026-09-08 (`ap_progression_aura`, default on). Replaces the
removed ring overlay (`docs/AP-PIN-COLORS.md`).

## What ships

Every pin stays on the map exactly as the filters leave it. A pin matched to a
progression check additionally sits on a gold ring, drawn by the engine as
part of the pin's own icon frame. There is no overlay, no per-frame work, and
no game-flag read:

- `tools/generate_map_icons.py` emits one extra bitmap, a procedural gold ring
  (`MAP_AURA_TAG`), alongside the icon bitmaps.
- At worldmap load `goblin_gfx_probe` appends, for every injected icon, a
  second "aura twin" frame: RemoveObject2(1) + RemoveObject2(2) + place ring
  at depth 1 + place icon at depth 2. `injected_aura_iconid(iconId)` maps a
  marker's current injected frame to its twin in O(1).
- Inside the `buildMarkers` window, the same pass that prunes hidden rows
  swaps the `iconId` of each shown progression row to its twin, and the
  existing scope guard restores it when the build returns. Nothing outside the
  build ever sees the mutated row.
- Which rows count as progression: with `ap_in_logic_only` on, the wire bit
  `MFG_AP_PROGRESSION_IN_LOGIC`; with it off, `MFG_AP_PROGRESSION`. The DLL
  never rebuilds bit 8 from bits 2 and 4 (`check_is_progression`, unit
  tested in `tests/ap_cache_test.cpp`).
- The `[prune]` log line reports `prog=N` (rows swapped) and
  `prog_no_twin=N` (progression rows whose icon has no twin: vanilla icons
  1-348, or the aura bitmap failed to register this load).
- Like every build-time change, a new snapshot lands on the next map open.
- **Hints** (`ap_hint_aura`, default on, 2026-09-08): a second twin per icon
  with a blue ring, chosen for pins the client leases as hinted through the
  lot-style API (`MFG_AP_STYLE_YELLOW`). A hint wins over progression on the
  same pin. `[prune]` reports `hinted=N`. Boss defeat-flag pins cannot be
  hinted this way because the style API only names map/enemy lots.

Below is the original design note, kept for the reasoning.

## What was actually expensive

The rings removed on 2026-09-07 were not slow because of drawing. Thirty-two
segment circles into ImGui's one foreground draw list is a single batched
draw call for the whole map. The cost was in three other places:

1. **The producer.** `ap_style_points()` rebuilt its output every 100 ms with
   two full passes over all 9,201 injected rows, rebuilding two hash maps and
   regrowing a vector each time.
2. **Live game-flag reads.** Every styled row cost up to ~25 `flag_is_set`
   calls (event flag, group-2 gate, hidden probe, eight enable flags, eight
   text ids). On a real seed most rows were styled.
3. **Forced overlay presentation.** The rings lived in the topmost transparent
   ImGui window, and a client merely holding a style lease OR'd into
   `projecting`, so that window was shown and presented every frame while the
   map was open, even with the F6 menu closed.

Plus the design failure: the seed's progression surface is most of the map,
so decorating it lifted nothing.

Since `a03cbf2` there is no per-frame marker draw of ours at all. Pins are
native engine pins built once per map open from `WorldMapPointParam` rows,
and hidden rows are pruned inside the `buildMarkers` window by clearing
`dispMask` bits under a scope guard. The only per-marker visual channel that
survives is `iconId`, which is already injection-plumbed (runtime-appended gfx
frames, `baked_icon` restore, `gfx_probe::injected_iconid`).

That is the whole design constraint: any progression signal must ride the
native pin build through `iconId`, and must cost nothing per frame.

## What to show

Only checks whose wire state carries `MFG_AP_PROGRESSION_IN_LOGIC` (bit 8):
progression targets for this seed that the client's tracker currently reports
reachable. Not the whole progression surface (bit 2), and not the in-logic
set (bit 4), which the filters already handle.

Bit 8 is computed by the client per check, so lots shared by several checks
cannot fabricate it by OR-ing bits 2 and 4. The DLL must never reconstruct it.

Under the default filters (`ap_checks_only`, `ap_in_logic_only`) the map
already shows only reachable checks. The progression icon then answers the one
remaining question, "which of these is worth walking to first", on a set that
is by construction small. If that set is ever most of the visible pins, the
problem is upstream in the surface definition, not here.

## Mechanism: icon swap at build time

1. **One new injected gfx frame**, "progression pin": the ordinary check pin
   sprite with a gold ring or dot baked into the artwork. Appended at runtime
   like the other custom icons, so it is a valid `iconId` with or without our
   gfx (see `research_no_gfx_icons.md`). One frame for all lot kinds,
   including boss defeat-flag pins (`lot_table=3`).
2. **Snapshot, not scan.** When `MFG_AP_SET_CHECK_STATES_V1` accepts a
   snapshot, build an `unordered_set<(lot_table,lot_row)>` of entries with bit
   8 once, at accept time, and store it next to the existing filter state.
   `VisibilitySnapshot` gains a pointer to (or copy of) that set, taken under
   the same single lock as today. Lease expiry drops it the way it drops the
   filters.
3. **Apply inside `buildMarkers`.** In the same pass as
   `prune_hidden_pins_for_build()`, for every row that survives the prune, do
   one hash lookup; on a hit, write the progression frame into `iconId` with
   the same SEH-wrapped write the loot-icon path uses. The scope guard that
   restores `dispMask` restores `iconId` too (the `baked_icon` /
   `live_icon_override` bookkeeping already exists). Nothing outside the build
   window ever sees the mutated row.
4. **Ordering.** Run after `apply_loot_settings` rewrites loot icons, so the
   progression frame wins; a progression check that is also a hidden, gated,
   collected, or manually hidden row never reaches this step because the prune
   removed it first.

Per-frame cost: zero. Build-time cost: one hash lookup per surviving row,
no flag reads at all, because reachability and progression both arrive from
the client. The overlay window is never touched.

## Refresh semantics

Same as the filters: the icon lands on the **next map open**. The tracker
changes reachability rarely compared with how often players open the map, so
this is acceptable for v1 and matches the documented filter behaviour.

Phase 2, only if players ask: when the bit-8 set changes while the map is
open, request one pin rebuild. That needs research into whether `o_build` can
be re-entered safely on an open map; do not attempt it in the first cut.

## F6 tracker companion

Cheap, on demand, and covers the "next map open" gap:

- A line in the AP section: `N progression checks in logic`, from the same
  set, recomputed only when a snapshot is accepted.
- Optional: a list of those N with a "focus" action that reuses the existing
  `draw_map_highlights()` ring for exactly one pin. That path is
  user-triggered, draws one circle, and is already accepted as fine.

## Settings and packaging

- `ap_progression_icon` (bool, default `true`): kill switch, same pattern as
  `prune_hidden_pins_at_build`. With it off the frame is never written.
- No new client work: bit 8 is already published (client commit `da45dcd`).
- `tools/make_ap_ini.py` and the world's `tools/package_mfg.py` `PRESET` must
  both learn the key or `mfg_pin.py --check` fails. This lands with the pin
  bump that is currently on hold, not before it.
- `docs/AP-CHECK-FILTERS.md` and `docs/AP-PIN-COLORS.md` get a pointer here.

## Non-goals

- No coloured rings, halos, scale changes, or any ImGui foreground drawing per
  marker.
- No hint (yellow) icon in v1. The mechanism supports a second frame later,
  but only if the hinted set stays selective.
- No item identity anywhere in the path. Styles and states carry none by
  construction; keep it that way.

## Acceptance

Measure with the existing FastMap timing capture (`docs/fastmap-profile.md`,
`goblin_map_timing.cpp`), same seed, same save, map opened at Limgrave and
Altus, `ap_progression_icon` on vs off:

- Pin build time delta within noise of the prune-only baseline.
- Frame time with the map open and F6 closed: identical to baseline (the
  overlay window must show as not presented in the log).
- The `[prune]` log line gains a `prog=N` count so the swap can be checked
  against the client's own count without a debugger.
