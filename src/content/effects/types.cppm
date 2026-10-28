module;
#include <set>
#include <cstdint>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <variant>

export module phoenix.content.effects.types;
export import phoenix.data;
export import phoenix.contracts;
export import phoenix.resources;
export import phoenix.state_tree;

export namespace phoenix
{
struct CustomPropertyDefinition;
using PropertyCatalog = std::map<std::string, CustomPropertyDefinition>;
using SharedPropertyCatalog = std::shared_ptr<const PropertyCatalog>;
struct AbilityCatalog;
struct ProcessDefinition;
using ProcessCatalog = std::map<std::string, ProcessDefinition>;
using SharedProcessCatalog = std::shared_ptr<const ProcessCatalog>;
struct RuleDefinition;
struct RuleBinding;
using RulesCatalog = std::map<std::string, RuleDefinition>;
using SharedAbilityCatalog = std::shared_ptr<const AbilityCatalog>;
enum class CustomPropertyKind
{
	Quantity,
	Attribute,
	Flag,
	Enum
};
using CustomPropertyValue = std::variant<std::int64_t, bool, std::string>;
struct CustomPropertyDefinition
{
	std::string id, unit, label;
	CustomPropertyKind kind = CustomPropertyKind::Quantity;
	std::int64_t minimum = 0, maximum = resource_limit, display_scale = 1;
	CustomPropertyValue initial = std::int64_t{0};
	std::map<std::string, std::string> choices;
};
struct PsychicPowerDefinition
{
	std::string id, kind, label, apparent_class = "Unidentified contact";
	std::int64_t strength = 700, ghosts = 3;
	std::int64_t offset = 20000, spacing = 8000, speed = 10000, acceleration = 2000, turn_rate = 10000;
	std::int64_t radius = 2000, scan_ticks = 100, fade_ticks = 20;
};
struct ModifierContributionDefinition
{
	std::string name, role, property;
	std::int64_t delta_permille = 0;
	std::optional<CustomPropertyValue> value;
	std::int64_t priority = 0;
};
struct ModifierEffectDefinition
{
	std::string id;
	std::int64_t duration_ticks = 0;
	std::vector<ModifierContributionDefinition> contributions;
};
enum class PropertyKey : std::size_t
{
	propulsion,
	speed,
	turn,
	sensors,
	cycling,
	traverse,
	reload,
	processing,
	recharge,
	off_recharge,
	sustain,
	discharge,
	protection,
	repair,
	medical,
	security,
	comms,
	generation,
	cooling,
	count
};
struct ModifierPropertyDescriptor
{
	PropertyKey key;
	std::string id, unit;
	std::vector<OperatingSystem> owners;
	std::int64_t maximum = 1000000000;
};
enum class EffectOperationKind
{
	Damage,
	Apply,
	Remove,
	StoreAdd,
	StoreConsume,
	StoreSet,
	StoreFill,
	StoreTransfer,
	PropertySet,
	PropertyAdd,
	PropertySubtract,
	PropertyRate,
	HeatAdd,
	HeatRemove,
	StructuralRepair,
	FaultRepair,
	Treat,
	Stabilise,
	Recover
};
struct EffectOperation
{
	EffectOperationKind kind = EffectOperationKind::Damage;
	std::string target = "attacker", effect, grant;
	std::string equipment;
	std::int64_t damage = 0;
	std::string store, resource, from_store;
	std::int64_t amount = 0;
	bool all_sources = false;
	std::string officer;
	std::optional<std::int64_t> delta_permille;
	std::string role;
	std::string scope = "local";
	std::string property;
	std::string compartment, fault;
	CustomPropertyValue value = std::int64_t{0};
	bool saturate = false;
};
struct EffectCostDefinition
{
	std::string payer = "target", equipment, store, resource;
	std::int64_t amount = 0;
	std::string property;
};
struct EffectGroupDefinition
{
	std::string payer;
	std::int64_t energy = 0;
	std::vector<EffectOperation> operations;
	std::vector<EffectCostDefinition> costs;
};
// Rules compile authored values once. Runtime adapters freeze the indexed facts before evaluation.
enum class RulePhase
{
	Instant,
	Periodic,
	Maintained
};
enum class RuleTrigger
{
	Activate,
	Deactivate,
	Tick,
	Enter,
	Exit,
	Occupancy,
	Maintain,
	PreShieldDamage,
	PostShieldDamage,
	PreCompartmentDamage,
	PostCompartmentDamage,
	PreSystemDamage,
	PostSystemDamage,
	PostEntityDamage,
	Destroy,
	PropertyChanged
};
enum class RuleRole
{
	Owner,
	Source,
	Instigator,
	Subject,
	Target,
	Attacker,
	Protected
};
enum class RuleFactKind
{
	Tick,
	Elapsed,
	Entity,
	Exists,
	Enabled,
	Eligible,
	Heat,
	ResourceQuantity,
	Property,
	PresentedDamage,
	ModifiedDamage,
	NegatedDamage,
	AbsorbedDamage,
	ResidualDamage,
	Strength,
	InflictedDamage,
	Output,
	CompletedWork,
	PropertyBefore,
	PropertyAfter,
	ProcessWork,
	ProcessActivity,
	OfficerPresent,
	OfficerHealthy,
	OfficerOnDuty
};
struct RuleFactBinding
{
	RuleRole role = RuleRole::Subject;
	RuleFactKind kind = RuleFactKind::Entity;
	std::string key, aux, unit;
	state_tree::Type type = state_tree::Type::Integer;
	bool operator==(const RuleFactBinding &) const = default;
};
enum class RuleExpressionOp
{
	Literal,
	Fact,
	All,
	Any,
	Not,
	Equal,
	NotEqual,
	Less,
	LessEqual,
	Greater,
	GreaterEqual,
	Present,
	Add,
	Subtract,
	Multiply,
	Divide,
	Ratio,
	Min,
	Max,
	Clamp
};
struct RuleExpressionNode
{
	RuleExpressionOp op = RuleExpressionOp::Literal;
	state_tree::Value literal;
	std::uint32_t fact = 0;
	std::vector<std::uint32_t> args;
};
struct RuleExpression
{
	std::vector<RuleExpressionNode> nodes;
	std::uint32_t root = 0;
	state_tree::Type type = state_tree::Type::Missing;
	std::string unit;
};
struct RuleOperationExpressions
{
	std::optional<RuleExpression> damage, amount, delta_permille, value;
};
struct RuleCostDefinition
{
	RuleRole payer = RuleRole::Target;
	std::string equipment, store, resource;
	RuleExpression amount;
	std::string property;
};
struct RuleFundingDefinition
{
	std::string shortage = "pause";
	std::vector<RuleCostDefinition> costs;
};
struct RuleDefinition
{
	std::string id;
	std::string label;
	RuleTrigger trigger = RuleTrigger::Activate;
	RulePhase phase = RulePhase::Instant;
	std::vector<RuleFactBinding> facts;
	std::optional<RuleExpression> condition, damage_permille, energy;
	EffectGroupDefinition effects;
	std::map<std::size_t, RuleOperationExpressions> values;
	std::vector<RuleCostDefinition> costs;
	std::int64_t period_ticks = 1;
	std::optional<RuleFundingDefinition> funding;
	bool scalable = false;
	std::string work_unit;
};
struct RuleBinding
{
	std::string id, rule;
	RuleRole target = RuleRole::Subject;
	std::string equipment, officer;
	std::vector<std::string> tags;
	bool lifetime = false;
};
struct RuleContext
{
	std::vector<state_tree::Value> facts;
};
struct SourceRuleDefinitionSet
{
	std::vector<RuleBinding> entity;
	std::map<std::string, std::vector<RuleBinding>> equipment, officers, officer_lifetime;
};
struct PreparedRuleQuote
{
	EffectGroupDefinition effects;
	std::int64_t damage_permille = 1000;
	std::vector<EffectCostDefinition> upkeep;
};
enum class RuleEvaluationStatus
{
	Ready,
	Unmatched,
	Invalid
};
struct RuleEvaluation
{
	RuleEvaluationStatus status = RuleEvaluationStatus::Invalid;
	PreparedRuleQuote quote;
};
inline constexpr std::size_t rule_expression_node_limit = 256;
inline constexpr std::size_t rule_binding_limit = 64;
struct AbilityExclusionDefinition
{
	std::string id, scope = "source";
};
enum class NativeProcessKind
{
	Effects,
	Generation,
	Propulsion,
	Cooling,
	Processor,
	Repair,
	Medical,
	Recovery,
	ServicingEnergy,
	ServicingAmmunition,
	Ftl
};
struct ProcessDefinition
{
	std::string id, label, family, shortage = "pause", work_unit = "work";
	NativeProcessKind native = NativeProcessKind::Effects;
	bool requested = true;
	std::int64_t work_divisor = 1000, required_work = 0;
	RuleDefinition evaluation;
	std::optional<RuleExpression> rate, upkeep_scale, work_scale;
	std::vector<EffectCostDefinition> entry_costs, upkeep_costs, work_costs;
	std::optional<EffectGroupDefinition> start_effect, pause_effect, resume_effect, stop_effect, completion_effect;
	std::vector<AbilityExclusionDefinition> exclusions;
	std::vector<std::string> observations;
};
struct ProcessBinding
{
	std::string id, process, native_id;
	bool requested = true;
};
struct ProcessControlDefinition
{
	std::string id, source = "equipment", owner, process;
	std::size_t controller = 0, enabled = 0;
};
using ProcessNativeBindings = std::map<NativeProcessKind, std::set<std::string>>;
struct ProcessDefinitionSet
{
	SharedProcessCatalog catalog;
	std::map<std::string, ProcessNativeBindings> native_bindings;
	std::vector<ProcessBinding> entity;
	std::map<std::string, std::vector<ProcessBinding>> equipment, officers, compartments;
	std::vector<ProcessControlDefinition> controls;
};
struct ProcessGrantDefinition
{
	std::string process, native_id;
	std::int64_t duration_ticks = 0;
};
struct AbilityDefinition
{
	AudioClip activation_audio;
	std::string id, label, target = "self", recovery_on = "activation", interruption = "cancel", shortage = "pause",
	                       psychic_effect, apparatus_recovery_key;
	bool sustained = false, require_healthy = true;
	std::int64_t preparation_ticks = 0, recovery_ticks = 0, energy_per_second = 0, preparation_energy_per_second = 0,
	             apparatus_recovery_ticks = 0, interruption_recovery_ticks = 0;
	std::optional<EffectGroupDefinition> start_effect, activation_effect;
	std::optional<RuleDefinition> requirements;
	std::string requirement_failure = "end_retain_intent", family;
	std::vector<std::string> observations;
	std::vector<EffectCostDefinition> costs;
	std::vector<AbilityExclusionDefinition> exclusions;
};
inline constexpr std::size_t ability_no_channel = static_cast<std::size_t>(-1);
inline constexpr std::size_t ability_grant_limit = 4096;
inline constexpr std::size_t ability_recovery_limit = 32768;
inline constexpr std::uint64_t ability_generation_limit = 1000000000ULL;
struct AbilityControlDefinition
{
	std::string id, source = "equipment", owner, ability, apparatus;
	std::size_t controller = 0, enabled = 0, target = ability_no_channel;
	std::optional<std::size_t> planner;
	std::size_t planning_enabled = ability_no_channel, planning_target = ability_no_channel;
};
struct AbilityDefinitionSet
{
	SharedAbilityCatalog catalog;
	std::vector<AbilityControlDefinition> controls;
	std::map<std::string, std::vector<std::string>> equipment_grants;
	std::map<std::string, std::int64_t> apparatus_recovery_bounds;
};
struct AbilityCatalog
{
	std::map<std::string, AbilityDefinition> definitions;
	std::map<std::string, PsychicPowerDefinition> psychic_profiles;
};
struct AbilityGrantDefinition
{
	std::string ability;
	std::int64_t duration_ticks = 0;
};
enum class PayloadOutcomeKind
{
	Shield,
	Compartment,
	System,
	Entity
};
struct PayloadDefinition
{
	std::string id;
	std::vector<RuleBinding> rules;
};
struct PeriodicEffectDefinition
{
	std::string id;
	std::int64_t duration_ticks = 1;
	std::vector<RuleBinding> rules;
};
const std::vector<ModifierPropertyDescriptor> &modifier_properties();
PropertyKey property_key(std::string_view);
const ModifierPropertyDescriptor &modifier_property(PropertyKey);
bool known_modifier_property(std::string_view);
bool property_compatible(std::string_view, OperatingSystem);
std::string modifier_property_role(std::string_view);
CustomPropertyValue read_custom_property_value(const CustomPropertyDefinition &, const Json &, const std::string &);
Json custom_property_json(const CustomPropertyValue &);
std::string resolve_process_native_binding(const ProcessDefinition &, const ProcessNativeBindings &,
                                           const std::string &requested_id = {});
} // namespace phoenix
