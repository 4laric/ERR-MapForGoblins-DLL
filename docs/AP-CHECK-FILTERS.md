# AP map filters and progression emphasis

The Archipelago settings in the MFG overlay provide three independent filters:

- **AP checks only** (default on while AP map integration is active): show pins
  matched to checks in the connected seed.
- **Progression surface only**: show checks eligible to hold progression in that
  seed. This does not reveal the randomized item placed there.
- **In logic only**: use the client's existing tracker region-access state.
  Extra quest/puzzle conditions are not evaluated; unknown regions are excluded.

Combining the last two requires the same check to satisfy both conditions, even
when several checks share a lot. Unmapped pins are hidden in checks-only mode;
they are not thereby classified as definitively non-checks. The mapping currently
covers 3,847 of the full catalog's 4,925 checks before seed options narrow it.

Progression markers have a larger, thicker halo by default (1.5x, adjustable
from 1x to 3x in Settings). This enlarges the marker's visible footprint without
resizing the game's icon artwork. Existing orange progression/yellow hint colors
remain intact. A hinted progression marker also receives the larger halo.

Enable the existing AP client's map colors or follow-pins workflow, or select
**Enable map filters** in its optional map section. Filters
receive a complete current-seed state independently of the color list. Switching
seeds, disconnecting or disabling integration withdraws that state; a short lease
also restores ordinary MFG visibility if the client stops refreshing. Filtering
never makes a collected, manually hidden or game-gated pin visible. Category focus
cannot bypass AP filters.

## Gathering-node preset

For ordinary MFG category declutter as well:

```ini
[Loot]
show_material_nodes = false
show_crafting_materials = true

[Archipelago]
ap_checks_only = true
ap_progression_only = false
ap_in_logic_only = false
ap_progression_scale = 1.5
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
