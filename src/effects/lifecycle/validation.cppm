module;
#include <algorithm>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

export module phoenix.effects.continuation_validation;
export import phoenix.effects.rule_state;
import phoenix.data;
import phoenix.resources.stocks;

export namespace phoenix
{
struct RuleValidationDefinitions
{
	const RulesCatalog &rules;
	const PropertyCatalog &properties;
	const ResourceCatalogue &resources;
	const std::map<std::string, std::vector<RuleBinding>> &regions;
};
struct RuleValidationPorts
{
	std::function<void(const RuleActor &)> actor_identity;
	std::function<bool(EntityId)> physical_exists;
	std::function<bool(const CustomPropertyAddress &, const std::string &property)> quantity_compatible;
	std::function<bool(EntityId, const std::string &store, const std::string &resource)> stock_accepts;
};
class RuleContinuationValidator
{
public:
	RuleContinuationValidator(RuleValidationDefinitions, RuleValidationPorts, std::int64_t tick);
	void actor(const Json &) const;
	void context(const Json &) const;
	void credit(const Json &) const;
	void state(const Json &, std::int64_t now) const;

private:
	RuleValidationDefinitions definitions_;
	RuleValidationPorts ports_;
	std::int64_t tick_;
};
} // namespace phoenix
