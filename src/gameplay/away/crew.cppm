module;
#include <nlohmann/json.hpp>
#include <string>

export module phoenix.gameplay.away.crew;
export import phoenix.gameplay.personnel.state;
export import phoenix.content.combat.types;
export import phoenix.content.controllers.types;
export import phoenix.world.state;

export namespace phoenix
{
bool away_navigator_qualified(const PersonnelDefinition &, const PersonnelState &, const ControllableLayout *,
                              const CompiledFittings *, const std::string &console, const std::string &system);
bool away_crew_healthy(const PersonnelDefinition &, const PersonnelState &, const std::string &asset,
                       bool team_available, const PersonnelDuty *team_duty);
bool away_crew_slot_accepts(const AwaySlot &, const std::string &asset, const PersonnelDefinition &,
                            const TeamDefinition *, bool team_duty);
} // namespace phoenix
