"""Table-qualified item-lot identity, independent of placement/display source.

Older items_database.json files omitted lotSource. Regenerate extraction before
writing live-loot linkage; source='enemy' identifies a placement, not a param
table (NpcParam may reference either table).
"""

LOT_TYPES = {'map': 1, 'enemy': 2}


def resolve_lot_source(record, map_lots, enemy_lots):
    """Resolve the table actually supplying a row; never cross an explicit binding.

    EMEVD records without a declared table can use unique table membership.
    A colliding numeric ID needs an explicit binding, not an enemy-first guess.
    Missing rows return None so the extractor's existing no-lot accounting works.
    """
    lot_id = record['itemLotId']
    declared = record.get('lotSource')
    tables = {'map': map_lots, 'enemy': enemy_lots}
    if declared is not None:
        if declared not in tables:
            raise ValueError(f"Invalid lotSource {declared!r} for lot {lot_id}")
        return declared if lot_id in tables[declared] else None
    matches = [name for name, rows in tables.items() if lot_id in rows]
    if len(matches) > 1:
        raise ValueError(
            f"Ambiguous lot {lot_id}: present in map and enemy tables; "
            "record the awarding instruction's explicit lotSource"
        )
    return matches[0] if matches else None


def linkage_lot_type(record):
    """Require explicit table provenance; legacy source strings are insufficient."""
    table = record.get('lotSource')
    if table not in LOT_TYPES:
        raise ValueError(
            f"Lot {record.get('itemLotId')} lacks a valid explicit lotSource; "
            "rerun extract_all_items.py (and fallback enrichment) for this profile"
        )
    return LOT_TYPES[table]
