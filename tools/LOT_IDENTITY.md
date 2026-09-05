# Explicit lot table identity

`items_database.json` now carries `lotSource: "map" | "enemy"` on base,
sub-lot and fallback records. Placement `source` is independent: an NPC may award
a map lot, and EMEVD award instructions address map lots.

The MASSEDIT writer requires explicit identity before writing any output. Older
item databases must be extracted again for their profile; the writer gives a
specific error instead of guessing from `source`. Existing compiled profiles are
unchanged by this source patch.

Validation includes numeric IDs occurring in both tables, NPC map bindings,
flagged sub-lots, actual extraction record constructors, missing rows and legacy
input rejection. The checked-in vanilla input tables contain seven overlapping
numeric IDs among 5,592 map rows and 5,135 enemy rows; this is a table overlap
measurement, not a count of wrong rendered pins. No full game-data extraction or
new profile build was performed in this audit. The patch must remain a draft
until that integration run has succeeded.
