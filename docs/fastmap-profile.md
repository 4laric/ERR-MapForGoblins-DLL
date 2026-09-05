# FastMap measurement build

This is an opt-in diagnostic, not a speedup or a re-enabled layout cache.
Set MFG_FASTMAP_PROFILE=1 in the environment inherited by the game at launch.
The choice is captured once at DLL startup. Profile mode bypasses fast_map_open
even if that INI setting is true. Without the environment variable, existing
behavior is unchanged.

## What it measures

Thirty seconds from the first map-dispatch call, across four buckets:
0 = marker refresh; 1/2/3 = the child-list calls with modes 0/1/3.
The existing background worker logs cumulative completed-call count, inclusive
original-call microseconds, maximum call duration, first thread ID and whether
multiple thread IDs were seen. Reporting is approximately every 1–2 seconds,
independent of the overlay and hotkey options.

These are NOT frame times, map-open latency, exclusive CPU costs, or per-dialog
generations. Nested costs can overlap; do not add buckets into an FPS estimate.
The timer excludes accounting and logging, but instrumentation still adds
overhead. A changing cumulative count while the map is stationary identifies
repeated work; it does not prove the work is redundant.

New samples stop at 30 seconds. In-flight calls may finish for up to five more
seconds before the next worker poll publishes the final report. pending_calls
on that report explicitly identifies incomplete samples. A stuck original
cannot produce unlimited report lines.

Every original call runs exactly once with unchanged arguments and return value.
No raw game pointers are retained, no calls are deferred, and no destructor hook
is installed. Missing/ambiguous signatures or mismatched call targets disable
the diagnostic; partial hook installation remains pass-through.

## Static evidence and why the old optimization is not enabled

Current executable SHA-256:
d1a84083c6c7c7902162ff098f7d86812839aa6b3575959398857e539c488134

The dispatcher pattern is unique at RVA 0x10e1b45. Its four calls resolve to
0x10db5b0 (refresh) and 0x10d0190 three times (child list), matching the unique
function patterns. The runtime repeats uniqueness and target-identity checks.

Earlier RTTI inspection distinguished the actual WorldMapDialogBase destructor
from a similar CSWorldAiManagerImp destructor. That does not establish deferred
marker lifetime. The legacy optimizer also retains its built latch across
destruction and skips live visibility reevaluation. This diagnostic deliberately
does not repair just the signature and activate that algorithm.

## Test

Use the same character, map position, zoom and settings for each run. Restart
the game between captures to rearm the diagnostic.

1. Launch with the profile environment variable. Check the log for
   "[fastmap-profile] armed"; a disabled message is a failed preflight, not a
   successful measurement.
2. Open the map, leave it stationary for about ten seconds, pan/zoom for ten,
   then close and reopen it for the remainder. Record those approximate times.
3. Confirm labels and hover still work, and a collected pickup still disappears.
   Reopen several times, including a rapid close/reopen.
4. Save the MapForGoblins log after the final report.
5. Repeat with show_material_nodes=false, then restore true and repeat (A/B/A).
   Keep all other settings fixed and avoid collections between the comparison runs.

Disabling material visibility does not remove the injected rows. Compare the
measured results; do not assume an object-count reduction or promise a speedup.

## Next decision

If refresh dominates, investigate a per-dialog dirty-layout strategy with
verified lifetime and invalidation. If child-list work dominates, preserve
visibility semantics and investigate reducing eligible marker work. If neither
dominates, measure the surrounding dispatcher/rendering path before changing
either. Frame/open/close timing and dialog-lifetime probes remain separate work.

Accounting tests cover idle-before-map, cumulative totals, thread changes,
deadline rejection, in-flight completion, and bounded reporting for stuck calls.
Windows compilation and live behavior are separate validation steps.
