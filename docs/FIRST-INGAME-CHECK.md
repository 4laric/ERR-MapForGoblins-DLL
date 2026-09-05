# First in-game check: experimental map engine

This is a validation build, not a finished F6 map tracker. The code and data can
be built and tested offline; map behavior still needs observation in the game.

Use a separate offline test profile and test character. Load only this source-built
MapForGoblins.dll as the map engine; do not also load stock MapForGoblins. Keep the
existing working profile available. Keep this DLL and its fresh settings in their
own folder; do not reuse the newer stock DLL's settings file. For the AP diagnostic,
set anonymous_loot=true in the generated settings so randomized rewards stay unnamed.
Restart to change engines; never hot-swap a DLL.

Record the engine/client commits, DLL hashes, executable hash, loader and other mods
for the run. The current build is tied to the executable hash in
CURRENT-GAME-OFFLINE-AUDIT.md. A signature match is not runtime approval.

## Map-only baseline

Before involving AP, exercise startup, map opening/closing, surface/underground/DLC
layers, panning and zooming, pickup hiding, manual hide/restore, quit/reload and
controller input. Record hangs, wrong layers, missing icons or flicker. The log
should explain that fast-map optimization is unavailable on this executable;
ordinary map behavior should still work. Do not treat a missing log as success.

## Read-only hover witness

Use the client from PR628, or a later build containing its new read-only interface.
With the game map open, hover the Lordsworn's Greatsword pickup near Gatefront and
run the existing client command !mfgprobe. Use a character where that pickup has not
been collected or sweep-granted. If it is unavailable, record another visible pin's
lot identity and resolve that instead; do not reset gameplay flags just for this test.
The expected copied identity is:

- lot-table=1 (map)
- lot-row=942370070
- original-flag=0 (unknown in the bridge, deliberately)
- nonzero generation and handle

The native generated row is 2400160; it is NOT the runtime handle. The offline
registry maps this baked lot to AP check 7772822 / original flag 1042377070.
This is the expected identity, not a prior live confirmation.

Move off the pin and repeat: no hovered marker. Close the map, wait at least one
second, and repeat: no stale identity. Reopen/switch a map layer and re-hover:
the generation must change after the native rebuild. Disable map injection and
repeat: unavailable. Re-enable and wait for a fresh observation.

If interacting with the client prevents the native map from updating, capture that
as a failed/blocked witness rather than extending the freshness window to hide it.
A dedicated continuous F6 panel is later work.

## What this does not validate

A successful hover does not prove acquisition, AP completion, exact coordinates,
spoiler filtering, highlighting or player corroboration. No reports are submitted.
The next stage joins the current seed, labels shared candidates, and adds opt-in
F6 presentation after this baseline and lifecycle witness pass.
