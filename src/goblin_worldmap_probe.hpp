#pragma once
// Fold a marker's raw area-local coordinates to world-map space using the game's OWN
// per-map converter (the same routine the engine calls to place every pin). Overworld
// (area 60) and DLC-overworld (61) are a plain affine (see goblin_mapproject); every
// other area (underground 12, legacy dungeons/caves, DLC-legacy) is translated onto the
// overworld-numeric frame by a regulation-driven fold, so it adapts to any profile
// (vanilla / ERR / overhauls) with no hardcoded per-area tables.
#include <cstdint>

namespace goblin::worldmap_probe
{
    // Arm a passive hook on the converter routine to capture the live WorldMapViewModel.
    // Call once at DLL init, before modutils::enable_hooks().
    void setup();

    // Called by the existing map-build hook, before the original builder.
    // Invalidates copied projections even when the engine reuses a VM address.
    void begin_build();

    // Fold (area, grid, area-local pos) -> map-space (u,v) via the captured converter.
    // Returns false if the view-model has not been captured yet (the world map has not
    // been opened this session), if the captured view-model has gone stale (the engine
    // stopped driving the converter, i.e. the map data is being torn down - the VM is the
    // engine's and must not be touched once it can have been freed), or if the point is
    // not placeable on any map layer.
    bool project(uint8_t area, uint16_t gx, uint16_t gz, float px, float pz,
                 float &map_u, float &map_v);
}
