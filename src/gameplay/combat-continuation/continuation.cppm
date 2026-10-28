module;
#include <cstdint>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>
export module phoenix.gameplay.combat.continuation;
export import phoenix.gameplay.directed;
export import phoenix.gameplay.combat.shields;
export namespace phoenix
{
using CombatMechanismDefinitions = std::map<std::string, CombatSystemDefinition>;
const TeamOrderDefinition *find_combat_team_order(const CombatMechanism &, const std::string &);
Json save_combat_structure(const CombatStructure &);
Json save_combat_mechanism(const CombatMechanism &);
void restore_combat_structure(CombatStructure &, const Json &, bool destroyed);
void restore_combat_mechanism(CombatMechanism &, const CombatDefinition &, const Json &);
Json save_shield_continuation(const ShieldComponent &);
ShieldComponent restore_shield_continuation(const Json &, const CombatMechanismDefinitions &,
                                            const std::map<std::string, ShieldEffectDefinition> &templates);
struct OrdnanceRestoreCatalogue
{
	std::set<std::string> visuals, payloads;
	std::map<EntityId, std::set<std::string>> decks;
};
struct ProjectileContinuation
{
	Projectile projectile;
	Destructible native;
};
struct ProjectileContinuationRoster
{
	std::uint64_t next_projectile = 1;
	std::vector<ProjectileContinuation> values;
};
Json save_projectile_continuation(const Projectile &, const Destructible &);
ProjectileContinuationRoster restore_projectile_continuations(const Json &, std::int64_t tick,
                                                              const OrdnanceRestoreCatalogue &);
} // namespace phoenix
