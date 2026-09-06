# Optional AP hint / progression-surface rings

The companion AP client publishes presentation styles for baked table-qualified
lot identities. Style 1 is orange (seed progression eligibility), style 2 yellow
(known hint). The client handles exact current-seed matching and unanimous
candidate classification. The engine does not inspect randomized item placement.

Enable **Color map pins (this session)** in F6 with the matching updated client.
This is independent of following/recording hovers and of fast-map profiling.
The native engine draws rings in its existing passive, click-through overlay;
it does not change icon textures, flags, row visibility or focus.

Only injected pins with one native row for their table/lot are decorated.
Duplicate native identities are withheld rather than guessing a site. The renderer
requires active injection, enabled category/current focus, discovery/event gates,
text enable gates, no hide/collection condition and a known current map layer.
Unknown layers or failed/non-finite projections draw nothing.

The bounded snapshot API is declared in include/mfg_ap_readonly_v1.h. ABI1 layouts
and hover exports are unchanged; capability bit 2 announces
MFG_AP_SET_LOT_STYLES_V1. The setter copies up to 8192 unique identities, validates
the entire batch, and replaces it atomically. A null/zero/zero call clears it.
Nonempty snapshots require a 250..10000 ms lease; the client uses 3000 ms refreshed
once per second. Disable, row retirement, clock reversal and expiry clear styles.

The render path keeps only copied positions between frames. Visibility is refreshed
at most every 100 ms, with cheap style membership before flag checks; projection
and viewport/layer culling happen per frame. Collection/category changes can take
up to 100 ms to remove a ring. No work over rows occurs without an active snapshot.
No claim of lower frame cost is made.

Validation: actual exported setter and copied-cache tests cover malformed and
duplicate batches, bounds, ownership, invalid-batch atomicity, clear, expiry,
clock reversal, disable and row replacement under ASAN/UBSAN. A full Windows
compile is a separate gate.

Live acceptance remains necessary: compare orange pins to F6 surface checks, obtain
a hint and verify yellow priority, collect/hide a pin, pan/zoom across all map layers,
check discovery/story-gated pins, disable/reconnect, and compare dense-map frame cost.
The existing map projection is not independently validated by these automated tests.
