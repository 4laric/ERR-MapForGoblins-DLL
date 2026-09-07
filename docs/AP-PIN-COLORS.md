# AP pin colouring (removed)

The on-map ring overlay that coloured pins orange (seed progression surface) and
yellow (known hint) was **removed on 2026-09-07**. Nothing is drawn for lot
styles any more, and the config keys `ap_progression_rings` and
`ap_progression_scale` no longer exist.

Why it went:

- On a real seed the progression surface is most of the map, so the rings buried
  the pins they were meant to lift rather than highlighting anything.
- Producing them cost up to ~25 live game-flag reads per styled row, ten times a
  second, on top of a full pass over all 9,201 injected rows.
- The rings are drawn in the separate topmost overlay window, so an AP client
  merely holding a style lease forced that window to be shown and presented
  every frame while the map was open, even with the F6 menu closed.

What replaces it: the filters in `docs/AP-CHECK-FILTERS.md`. `ap_checks_only`
and `ap_in_logic_only` (both on by default) leave only the reachable checks on
the map, which is the same information the orange rings were approximating, and
those pins are now pruned at map-build time instead of merely dimmed.
Progression is still surfaced by `ap_progression_only` and by the F6 tracker.

## The client-facing API is unchanged

`MFG_AP_SET_LOT_STYLES_V1` and capability bit `MFG_AP_CAP_LOT_STYLE_OVERLAY_V1`
(2) are still exported and still validate exactly as before, so an existing
client that publishes a style snapshot keeps receiving `MFG_AP_OK`. The accepted
snapshot is simply never rendered. See `include/mfg_ap_readonly_v1.h`. Clients
should stop publishing styles when convenient; there is no need to rush.

The focus-highlight rings from the region-progress tab are a different feature
and are untouched.
