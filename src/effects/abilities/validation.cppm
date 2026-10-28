module;
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

export module phoenix.effects.ability_validation;
export import phoenix.effects.abilities;

export namespace phoenix
{
struct AbilityValidationIntent
{
	bool request_semantics = false;
	std::int64_t value = 0;
	std::uint64_t request_id = 0;
	bool accepted = false;
};
struct AbilityValidationPorts
{
	std::function<bool(const std::string &source, const std::string &owner)> source_exists;
	std::function<bool(EntityId)> target_exists;
	std::function<void(const RuleWorkCredit &)> credit;
	std::function<const AbilityControlDefinition *(const AbilityGrantState &)> control;
	std::function<AbilityValidationIntent(const AbilityControlDefinition &)> intent;
};
void validate_ability_runtime(EntityId ship, std::uint64_t tick, const AbilityRuntime &state,
                              const AbilityRuntime &initial, const AbilityDefinitionSet &,
                              const std::map<std::string, AbilityGrantDefinition> &, const AbilityValidationPorts &,
                              std::int64_t coordinate_unit);
} // namespace phoenix
