module;
#include <flecs.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

export module phoenix.effects.preparation;
export import phoenix.effects.transactions;
export import phoenix.effects.native;
import phoenix.effects.grants;

export namespace phoenix
{
struct EffectPreparationCatalog
{
	const std::map<std::string, ShieldEffectDefinition> &shields;
	const std::map<std::string, ModifierEffectDefinition> &modifiers;
	const std::map<std::string, AbilityGrantDefinition> &abilities;
	const std::map<std::string, ProcessGrantDefinition> &process_grants;
	const std::map<std::string, PeriodicEffectDefinition> &periodic;
	const ProcessCatalog &processes;
	const PropertyCatalog &properties;
};
// Native policies and lifetime queries only. Generic effect dispatch, copies,
// stores and grant preparation are implemented by EffectPreparation.
struct EffectPreparationPorts
{
	std::function<flecs::entity(EntityId, bool ordnance)> find;
	std::function<bool(flecs::entity)> terminal;
	std::function<bool(const PayloadHitContext &)> hit_available;
	std::function<NativeOwnerDefinitions(EntityId)> definitions;
	std::function<std::optional<std::size_t>(EntityId, const std::string &)> compartment;
	std::function<void(PreparedEffectGroup &, const EffectGroupInvocation &, const EffectOperation &)> remove_owner;
	std::function<void(PreparedEffectGroup &, const EffectGroupInvocation &, const EffectOperation &)> native_outcome;
	std::function<bool(const NativeRuleContext &)> property_rate_owner_live;
	std::function<void(AbilityRuntime &, EntityId, const EffectOperation &, const std::string &)> ability;
	std::function<std::string(const CustomPropertyAddress &, const ProcessGrantDefinition &)> process_binding;
	std::function<bool(const std::vector<ActiveRuleEffect> *, const std::string &, EntityId)> active_exists;
	std::function<std::vector<ActiveRuleEffect>()> active_snapshot;
	std::function<bool(flecs::entity, const std::string &, const ModifierEffectDefinition &)> modifier_compatible;
};
struct ThermalEffectTarget
{
	EntityId entity = 0;
	ResourceIndex store = no_resource;
};
class EffectPreparation
{
public:
	EffectPreparation(EffectPreparationCatalog, EffectPreparationPorts, std::int64_t ticks_per_second);
	void stage(PreparedEffectGroup &, std::size_t invocation) const;
	flecs::entity require_target(EntityId id, bool ordnance = false) const;
	flecs::entity target(PreparedEffectGroup &, const EffectGroupInvocation &, EntityId id,
	                     bool uses_payload_outcome = true, bool ordnance = false) const;
	std::string equipment(const EffectGroupInvocation &, const std::string &binding) const;
	RuleActor property_actor(const EffectGroupInvocation &, const RuleActor &, const std::string &equipment,
	                         const std::string &officer, const std::string &compartment = {}) const;
	StockStore &stock(PreparedEffectGroup &, const EffectGroupInvocation &, EntityId id, const std::string &name,
	                  bool uses_payload_outcome = true) const;
	PropertyEffects &properties(PreparedEffectGroup &, flecs::entity) const;
	CustomPropertyState &custom_properties(PreparedEffectGroup &, const CustomPropertyAddress &) const;
	ProcessRuntime &processes(PreparedEffectGroup &, const CustomPropertyAddress &) const;
	std::vector<ActiveRuleEffect> &active(PreparedEffectGroup &) const;
	ThermalEffectTarget thermal_target(PreparedEffectGroup &, const EffectGroupInvocation &,
	                                   const EffectOperation &) const;
	void prepare_thermal_networks(PreparedEffectGroup &) const;
	void thermal(PreparedEffectGroup &, const EffectGroupInvocation &, const EffectOperation &) const;

private:
	void property(PreparedEffectGroup &, const EffectGroupInvocation &, const EffectOperation &, const RuleActor &,
	              std::size_t) const;
	const ProcessRuntime *process_runtime(const CustomPropertyAddress &) const;
	void periodic(std::vector<ActiveRuleEffect> &, std::uint64_t &, const EffectOperation &, const std::string &,
	              EntityId, const PayloadHitContext &, const std::string &, const std::string &, std::int64_t tick,
	              const NativeRuleContext *) const;
	void modifier(PreparedEffectGroup &, const EffectGroupInvocation &, flecs::entity, const EffectOperation &,
	              const std::string &) const;
	void application(flecs::entity, const EffectApplication &, ShieldComponent *, PropertyEffects *,
	                 std::int64_t tick) const;
	EffectPreparationCatalog catalog_;
	EffectPreparationPorts ports_;
	std::int64_t rate_;
};
} // namespace phoenix
