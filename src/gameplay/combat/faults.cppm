module;
#include <cstdint>
#include <string>

export module phoenix.gameplay.combat.faults;
export import phoenix.gameplay.combat.state;

export namespace phoenix
{
bool equipment_destroyed(const CombatStructure *, const std::string &equipment);
bool hard_fault_disabled(const CombatStructure *, const std::string &equipment);
std::int64_t fault_effectiveness(const CombatStructure *, const std::string &equipment);
bool equipment_structurally_disabled(const CombatStructure *, const std::string &equipment, bool mobile_team,
                                     bool unavailable);
} // namespace phoenix
