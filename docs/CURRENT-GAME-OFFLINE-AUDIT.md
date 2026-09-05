# Offline executable and Cheat Engine reference audit

Read date: 2026-09-05. Only bytes/XML were read; no CE scripts or game code ran.

| Resource | SHA-256 |
|---|---|
| Installed eldenring.exe | d1a84083c6c7c7902162ff098f7d86812839aa6b3575959398857e539c488134 |
| eldenring_all-in-one_Hexinton-v6.0_ce7.5.ct | ebb1abd9e903543356869f8267c6615fa90c2a613266fc31bf6679b95752735d |

The source checker reports 28 unique matches out of 29 patterns, including every
pattern it classifies critical. Run tools/check_aobs.py against the same executable
to reproduce. This tests signatures, not calling conventions or structure layouts.

## Missing map destructor

src/goblin_map_timing.cpp's destructor signature embeds the old RIP-relative LEA
displacement B7 A3 16 02. The exact pattern finds no match. Wildcarding only those
four bytes finds **two** .text candidates:

| Function candidate RVA | Actual displacement | LEA target RVA |
|---|---|---|
| 0x379bc0 | E7 CB 6A 02 | 0x2a267d8 |
| 0x9c22d0 | E7 C1 16 02 | 0x2b2e4e8 |

Therefore wildcarding the displacement is not a safe port. The fast-map optimization
stays pass-through on this build because its required destructor hook is unavailable.
Identify the correct object via RTTI/callers and prove map-close lifecycle before
enabling deferred marker replay. Neither candidate is an accepted replacement.

## CE leads and limits

The table's [ Enable ] entry (ID 1337092247) provides these offline leads:

| Symbol | .text matches | Instruction RVA | RIP-relative global slot RVA |
|---|---:|---|---|
| WorldChrMan | 1 | 0x3ffbf6 | 0x3d69ff8 |
| FieldArea | 1 | 0x66ea92 | 0x3d6d248 |
| WorldMapMan | 1 | 0x61d7bd | 0x3d6e390 |
| GetParamBasePtr pattern | 86 | multiple | all target 0x3d85f58 |

These are slot addresses, not validated live pointer contents. The param pattern
is not a unique hook, and the table's cached accessors do not establish reload safety.

Player Coordinates CMP (ID 1337309251) has one match at RVA 0x465c52, but its script
identifies itself as executable 1.8.1.0 / January 2023. Its companion Coordinates
entry (ID 1337309155) has no match. Do not treat the coordinate feature as verified.

The worldMapCursorSelectRadius, worldMapCursorSpeed and worldMapCursorSnapRadius
entries are settings, not live cursor-position fields. No dedicated hover or map
destructor script was identified. Prefer the existing typed client API for player
position; use these references to test hypotheses, never to justify guessed offsets.
