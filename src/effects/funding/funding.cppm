module;
#include <flecs.h>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

export module phoenix.effects.funding;
export import phoenix.effects.transactions;
export import phoenix.effects.native;

export namespace phoenix
{
struct PreparedEffectCost
{
	EntityId payer = 0;
	std::string store, resource;
	std::int64_t amount = 0;
	ResourceIndex energy_store = no_resource;
	std::optional<CustomPropertyAddress> property_owner;
	std::string property;
};
struct RuleUpkeepInput
{
	EntityId payer = 0;
	std::string store, resource, request;
	std::int64_t rate = 0, credit = 0;
	ResourceIndex electrical = no_resource;
	bool network = false;
	std::string property;
	CustomPropertyAddress property_owner;
	std::int64_t unit_work = resource_work_unit;
};
struct ProcessFeeGroup
{
	std::optional<std::pair<CustomPropertyAddress, std::string>> stop;
	std::vector<PreparedEffectCost> costs;
};
struct RuleUpkeepPlan
{
	NativeRuleContext context;
	std::vector<EffectCostDefinition> costs;
	std::vector<RuleWorkCredit> credits;
	std::vector<RuleUpkeepInput> inputs;
	RuleWorkPayment payment;
	std::string shortage, failure;
	bool rejected = false;
	// Raw reservations are provisional until electrical allocation is feasible.
	bool provisional_shortage = false;
	bool dependency_suspended = false;
	std::int64_t maximum_output = 1000, requested_output = 1000;
	EntityId ability_ship = 0;
	std::string ability;
	std::optional<CustomPropertyAddress> process_owner;
	std::string process;
	bool process_work = false;
	std::int64_t unit_work = resource_work_unit;
	std::string depends_on;
	std::int64_t quoted_work = -1, quoted_scale = 1000;
	bool producer = false;
	std::vector<ProcessFeeGroup> extra_fees;
	bool resume_fees = false;
	std::uint64_t process_generation = 0, process_run = 0;
};

struct RuleFundingPorts
{
	std::function<flecs::entity(EntityId)> target;
	std::function<std::string(const EffectGroupInvocation &, const std::string &)> equipment;
	std::function<bool(EntityId, const std::string &)> equipment_available;
	std::function<const CustomPropertyState *(const CustomPropertyAddress &)> property;
	std::function<NamedStock(flecs::entity, const std::string &)> stock;
};
RuleUpkeepPlan make_rule_upkeep_plan(const RuleExecutionState &, std::span<const EffectCostDefinition>,
                                     const std::string &shortage);
RuleUpkeepPlan make_ability_upkeep_plan(EntityId, const NativeRuleContext &, const AbilityGrantState &,
                                        const AbilityDefinition &, std::span<const EffectCostDefinition>);
RuleUpkeepPlan make_process_upkeep_plan(const CustomPropertyAddress &, const NativeRuleContext &, const ProcessState &,
                                        const ProcessDefinition &, bool work, std::int64_t actual_work,
                                        std::int64_t scale, const RuleUpkeepPlan *previous);
std::int64_t rule_upkeep_request_rate(const RuleUpkeepInput &, std::int64_t output);
RuleWorkPayment quote_raw_upkeep(RuleUpkeepPlan &, std::span<const RuleWorkInput>,
                                 std::optional<std::int64_t> dependency_output);
bool quote_funded_upkeep(RuleUpkeepPlan &, std::span<const RuleWorkInput>, bool blocked,
                         std::optional<std::int64_t> dependency_output);
std::int64_t process_cost_product(std::int64_t, std::int64_t);
bool same_rule_work_input(const RuleWorkCredit &, const RuleUpkeepInput &);
void apply_rule_work_credits(std::vector<RuleWorkCredit> &, const RuleUpkeepPlan &);
std::int64_t rule_upkeep_credit(const RuleUpkeepPlan &, const RuleUpkeepInput &);
void reject_rule_upkeep(RuleUpkeepPlan &, const std::string &reason);
void resolve_rule_upkeep(const std::string &key, RuleUpkeepPlan &, const PropertyCatalog &, const RuleFundingPorts &,
                         std::set<std::pair<CustomPropertyAddress, std::string>> *paid_stops = nullptr);
} // namespace phoenix
