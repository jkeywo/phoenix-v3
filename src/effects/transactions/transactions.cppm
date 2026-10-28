module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

export module phoenix.effects.transactions;
export import phoenix.effects.properties;
export import phoenix.effects.abilities;
export import phoenix.effects.processes;
export import phoenix.effects.rule_state;
export import phoenix.gameplay.combat.state;
export import phoenix.effects.events;
import phoenix.resources;
import phoenix.gameplay.combat.shields;
import phoenix.gameplay.combat.outcomes;
import phoenix.gameplay.personnel.outcomes;

export namespace phoenix
{
enum class EffectApplicationKind
{
	Shield,
	Modifier
};
struct EffectApplication
{
	EffectApplicationKind kind;
	EffectApplicationOperation operation;
	std::string source, equipment, definition;
};

struct EffectApplicationCatalog
{
	const std::map<std::string, ShieldEffectDefinition> &shields;
	const std::map<std::string, ModifierEffectDefinition> &modifiers;
	const PropertyCatalog &properties;
};
struct EffectApplicationTarget
{
	bool available = false, properties = false, shields = false;
	std::function<bool(const std::string &, const ModifierEffectDefinition &)> modifier_compatible;
};
void prepare_effect_application(const EffectApplication &, ShieldComponent *, PropertyEffects *, std::int64_t tick,
                                const EffectApplicationCatalog &, const EffectApplicationTarget &);

// A quote owns its native copies and the addresses staged by StockTransaction.
// Moving a std::map preserves those addresses; copying this aggregate would not.
struct PreparedEffectPayment
{
	ResourceNetwork network;
	std::uint64_t revision = 0;
	std::vector<std::uint64_t> stores, coolant;
	// A native allocation may own the final network. Preparation still mutates
	// only this copy; the existing allocation commits after this transaction.
	ResourceNetwork *destination = nullptr;
	explicit PreparedEffectPayment(const ResourceNetwork &original, ResourceNetwork *prepared_destination = nullptr)
	    : network(original), revision(original.revision()), destination(prepared_destination)
	{
		for (const auto &store : original.stores())
			stores.push_back(store.stock.revision());
		for (const auto &source : original.coolant_sources())
			coolant.push_back(source.stock.revision());
	}
};
struct EffectGroupInvocation
{
	EffectGroupDefinition execution;
	EntityId protected_ship = 0, attacker = 0;
	std::string source, original_equipment;
	std::optional<PayloadHitContext> hit_context;
	std::optional<NativeRuleContext> rule_context;
	std::int64_t output_permille = 1000;
	bool maintained = false;
};
struct PreparedEffectDamage
{
	EntityId target = 0;
	std::int64_t amount = 0;
	bool uses_payload_outcome = false;
	PayloadHitContext context;
	std::size_t invocation = 0;
};
struct PreparedEffectTarget
{
	EntityId target = 0;
	bool uses_payload_outcome = false, ordnance = false;
};
struct PreparedEffectGroup
{
	std::vector<EffectGroupInvocation> invocations;
	std::vector<std::size_t> invocation_roots;
	std::size_t failed_invocation = static_cast<std::size_t>(-1);
	std::string failure;
	std::int64_t tick = 0;
	std::uint64_t epoch = 0, next_active = 0, original_next_active = 0;
	bool consumed = false, defer_damage = false;
	bool pre_generation = false;
	std::map<EntityId, NativeStructureOutcome> structures, original_structures;
	std::map<std::pair<EntityId, std::size_t>, NativePatientOutcome> patients, original_patients;
	std::map<std::pair<EntityId, std::string>, NativeTeamOutcome> clinical_teams, original_clinical_teams;
	std::map<EntityId, ShieldComponent> shields, original_shields;
	std::map<EntityId, PropertyEffects> properties, original_properties;
	std::map<CustomPropertyAddress, CustomPropertyState> custom_properties, original_custom_properties;
	std::map<EntityId, AbilityRuntime> abilities, original_abilities;
	std::map<CustomPropertyAddress, ProcessRuntime> processes, original_processes;
	std::set<std::pair<CustomPropertyAddress, std::string>> process_stops;
	std::set<std::pair<CustomPropertyAddress, std::string>> prepaid_process_stops;
	std::set<std::pair<CustomPropertyAddress, std::string>> prepaid_process_exits;
	std::vector<std::string> effect_failures;
	std::map<EntityId, PreparedEffectPayment> payments;
	std::set<EntityId> changed_inventories;
	std::vector<PreparedEffectTarget> targets;
	std::vector<std::pair<EntityId, std::string>> equipment;
	std::vector<PreparedEffectDamage> damages;
	std::optional<std::vector<ActiveRuleEffect>> active_effects, original_active_effects;
	StockTransaction stocks;
	PreparedEffectGroup() = default;
	PreparedEffectGroup(const PreparedEffectGroup &) = delete;
	PreparedEffectGroup &operator=(const PreparedEffectGroup &) = delete;
	PreparedEffectGroup(PreparedEffectGroup &&) = default;
	PreparedEffectGroup &operator=(PreparedEffectGroup &&) = default;
};

struct NativeOutcomeTarget
{
	bool terminal = true;
	const CombatStructure *structure = nullptr;
};
struct NativeOutcomePreparationPort
{
	std::function<NativeOutcomeTarget(EntityId)> target;
	std::function<NativeStructureOutcome &(EntityId)> structure;
	std::function<std::optional<std::size_t>(EntityId, const std::string &)> officer;
	std::function<NativePatientOutcome &(EntityId, std::size_t)> patient;
	std::function<bool(EntityId, const std::string &)> team;
	std::function<NativeTeamOutcome &(EntityId, const std::string &)> clinical_team;
};
void prepare_native_outcome(const EffectGroupInvocation &, const EffectOperation &,
                            const NativeOutcomePreparationPort &, std::int64_t ticks_per_second);
struct ModifierPreparationPort
{
	std::function<bool(const ModifierEffectDefinition &)> compatible;
	std::function<PropertyEffects &()> prepared;
};
void prepare_group_modifier(PreparedEffectGroup &, const EffectGroupInvocation &, const EffectOperation &,
                            const std::string &equipment, const ModifierEffectDefinition &, const PropertyCatalog &,
                            const ModifierPreparationPort &);
bool same_effect_properties(const PropertyEffects &, const PropertyEffects &);
bool same_effect_shields(const ShieldComponent &, const ShieldComponent &);
bool same_effect_abilities(const AbilityRuntime &, const AbilityRuntime &);
bool same_effect_active_rules(const std::vector<ActiveRuleEffect> &, const std::vector<ActiveRuleEffect> &);
bool effect_payment_stale(const PreparedEffectPayment &, const ResourceNetwork &);
NativeRuleContext effect_invocation_context(const EffectGroupInvocation &, std::int64_t tick);
RuleActor effect_group_actor(const EffectGroupInvocation &, const std::string &role);
// Native ports validate real target/context constraints and install prepared state.
// Shared commit owns failure ordering, stock commitment, consumption and epoch.
struct EffectCommitCallbacks
{
	std::function<bool(const PreparedEffectGroup &)> stale;
	std::function<std::string(const PreparedEffectGroup &)> origin_failure;
	std::function<std::string(const PreparedEffectGroup &)> damage_failure;
	std::function<void(PreparedEffectGroup &)> apply;
};
class EffectTransactionCommitter
{
public:
	std::uint64_t epoch() const;
	void invalidate();
	std::string commit(PreparedEffectGroup &, RuleEventQueue &, const EffectCommitCallbacks &);

private:
	std::uint64_t epoch_ = 1;
};
} // namespace phoenix
