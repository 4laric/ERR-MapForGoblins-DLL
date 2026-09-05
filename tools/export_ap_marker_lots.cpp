// Offline only: compile against a selected generated profile, without game hooks.
// Output is development data, not independent corroboration or a runtime snapshot.
#include "goblin_map_data.hpp"
#include <iostream>
#include <unordered_set>

int main()
{
    using namespace goblin::generated;
    if (MAP_ENTRY_COUNT == 0)
    {
        std::cerr << "No generated markers; refusing empty inventory\n";
        return 1;
    }
    std::unordered_set<uint64_t> ids;
    for (size_t i = 0; i < MAP_ENTRY_COUNT; ++i)
    {
        const auto& row = MAP_ENTRIES[i];
        if (!ids.insert(row.row_id).second || row.lotType > 2 ||
            ((row.lotType == 0) != (row.lotId == 0)))
        {
            std::cerr << "Invalid/duplicate generated marker identity\n";
            return 1;
        }
    }
    std::cout << "marker_row_id,lot_table,lot_row\n";
    for (size_t i = 0; i < MAP_ENTRY_COUNT; ++i)
    {
        const auto& row = MAP_ENTRIES[i];
        std::cout << row.row_id << ',' << unsigned(row.lotType) << ',' << row.lotId << '\n';
    }
}
