# AP map filters and progression emphasis

The Archipelago settings in the MFG overlay provide three independent filters:

- **AP checks only** (default on while AP map integration is active): show pins
  matched to checks in the connected seed.
- **Progression surface only**: show the client's progression targets for that
  seed. The paired AP client redirects sweep-granted slots to their granting
  boss. This does not reveal the randomized item placed there.
- **In logic only** (default on since 2026-09-07): use the client's existing
  tracker region-access state. Extra quest/puzzle conditions are not evaluated;
  unknown regions are excluded.

With the two defaults together (`ap_checks_only = true`, `ap_in_logic_only =
true`) the map shows only pins matched to a check in your seed **that the
tracker currently reports as reachable**: both conditions must hold for the same
check. Turn `ap_in_logic_only` off to see the whole matched-check surface again.

Combining the last two requires the same check to satisfy both conditions, even
when several checks share a lot. Unmapped pins are hidden in checks-only mode;
they are not thereby classified as definitively non-checks. The mapping currently
covers 3,847 of the full catalog's 4,925 checks before seed options narrow it.

Pin colouring (the orange progression / yellow hint rings and the
`ap_progression_scale` halo) was removed on 2026-09-07 - see
`docs/AP-PIN-COLORS.md`. Progression is surfaced by **Progression surface only**
and by the F6 tracker.

## Filtered pins are pruned, not just dimmed

Since 2026-09-07 a row that these filters (or a category toggle, a collection, a
manual hide, or an active focus) hide is stripped of its `dispMask` bits for the
duration of the map's pin build, so the engine never creates a pin or a widget
tree for it at all. The masks are restored the moment the build returns, so
nothing else sees a mutated row. The INI key `prune_hidden_pins_at_build`
(default `true`) turns this off for A/B testing.

The one behavioural consequence: making a hidden marker VISIBLE again - enabling
a category, clearing a focus, relaxing an AP filter, unhiding a marker - now
takes effect on the **next map open**, not instantly on the open map. Hiding is
still instant (it still runs through the live text-enable-flag path), as is a
marker disappearing when you collect it.

Enable the existing AP client's map colors or follow-pins workflow, or select
**Enable map filters** in its optional map section. Filters
receive a complete current-seed state independently of the color list. Switching
seeds, disconnecting or disabling integration withdraws that state; a short lease
also restores ordinary MFG visibility if the client stops refreshing. Filtering
never makes a collected, manually hidden or game-gated pin visible. Category focus
cannot bypass AP filters.

## Boss marker identity

Capability `MFG_AP_CAP_BOSS_CHECK_STATES_V1` (8) extends the existing check-state
snapshot: `lot_table=3` means `MFG_AP_BOSS_DEFEAT_FLAG`, and `lot_row` carries
the exact nonzero defeat flag. Ordinary map/enemy lots remain kinds 1/2.
The older style and hover APIs do not accept kind 3.

Native boss pins without a lot use their captured original cleared flag (or
original first-line disable flag if the cleared flag is absent), so changing
visibility cannot change their identity. This covers the vanilla profile's 207
boss pins, including Godrick (10000800) and Limgrave Tree Sentinel (1042360800).
Filters and progression halos use this same identity. A map lot numbered
10000800 remains distinct from Godrick's defeat flag.

## Gathering-node preset

For ordinary MFG category declutter as well:

```ini
[Loot]
show_material_nodes = false
show_crafting_materials = true

[Archipelago]
ap_checks_only = true
ap_progression_only = false
ap_in_logic_only = true
```

The supplied vanilla profile has 1,488 gathering-node pins, including 229 Trina's
Lily nodes. Keeping crafting materials enabled preserves three Trina's Lily
**treasure checks** (Earthbore Cave, Jarburg and Waypoint Ruins) and other valid
crafting-item treasure checks. Category hiding alone is not a seed membership
filter and can be bypassed by ordinary category focus; the AP predicate applies
after focus.

To preserve unrelated settings when preparing a configuration:

```sh
python tools/make_ap_ini.py /path/to/MapForGoblins.ini /path/to/MapForGoblins-AP.ini
```

The output is separate from the input. Apply it with the matching newly built
engine and client after exiting the game. These source changes do not modify a
running game or its configuration. Filtering hides rows; it does not remove them
from the injected parameter table or promise a frame-rate improvement.
