#pragma once
namespace phoenix
{
// Native addresses are distinct from effect provenance and from control authority.
struct RuleActor
{
	EntityId entity = 0;
	std::string equipment, officer;
	bool ordnance = false;
	PayloadOutcomeKind kind = PayloadOutcomeKind::Entity;
	std::int64_t compartment = -1;
	std::uint64_t contribution = 0;
	std::string contribution_name;
	bool operator==(const RuleActor &) const = default;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RuleActor, entity, equipment, officer, ordnance, kind, compartment, contribution,
                                   contribution_name)

struct RuleCapturedFact
{
	RuleFactBinding fact;
	state_tree::Value value;
	bool operator==(const RuleCapturedFact &) const = default;
};
inline void to_json(Json &out, const RuleCapturedFact &capture)
{
	out = {{"role", capture.fact.role},        {"kind", capture.fact.kind},      {"key", capture.fact.key},
	       {"aux", capture.fact.aux},          {"unit", capture.fact.unit},      {"type", capture.fact.type},
	       {"value_type", capture.value.type}, {"number", capture.value.number}, {"text", capture.value.text}};
}
inline void from_json(const Json &in, RuleCapturedFact &capture)
{
	if (!in.is_object() || in.size() != 9 || !in.at("number").is_number_integer() || in.at("number").is_boolean())
		throw std::runtime_error("Invalid captured rule value");
	in.at("role").get_to(capture.fact.role);
	in.at("kind").get_to(capture.fact.kind);
	in.at("key").get_to(capture.fact.key);
	in.at("aux").get_to(capture.fact.aux);
	in.at("unit").get_to(capture.fact.unit);
	in.at("type").get_to(capture.fact.type);
	in.at("value_type").get_to(capture.value.type);
	in.at("number").get_to(capture.value.number);
	in.at("text").get_to(capture.value.text);
}

// A producer freezes event facts here. Only a projectile launch captures origin
// properties; ordinary defensive and maintained rules read their current host.
enum class NativeEffectKind
{
	None,
	Shield,
	Modifier,
	Ability,
	Periodic
};
struct NativeRuleContext
{
	RuleActor owner, source, instigator, subject, target;
	PayloadHitContext hit;
	std::string property_role, property_filter, region, owner_grant;
	NativeEffectKind owner_effect_kind = NativeEffectKind::None;
	std::uint64_t owner_effect_id = 0;
	bool capture_owner = false;
	std::int64_t tick = 0, elapsed = 0, presented = 0, modified = 0, negated = 0, absorbed = 0, residual = 0,
	             strength = 0, inflicted = 0, output = 1000;
	std::vector<RuleCapturedFact> captured;
	bool operator==(const NativeRuleContext &) const = default;
};

enum class RuleOwnerKind
{
	Equipment,
	Officer,
	Region,
	Effect,
	Shield
};
// Ephemeral adapter view. The pointed-to bindings belong to the immutable
// compiled catalogue; the native object remains the lifetime authority.
struct NativeRuleSource
{
	RuleOwnerKind kind = RuleOwnerKind::Equipment;
	std::string key;
	RuleActor owner;
	NativeRuleContext context;
	const std::vector<RuleBinding> *bindings = nullptr;
	bool eligible = false;
	std::uint64_t active_effect = 0;
};
// These credits represent already consumed resources buying unfinished work.
// They cannot be spent as stock, transferred, or refunded on rule removal.
struct RuleWorkCredit
{
	EntityId payer = 0;
	std::string store, resource;
	std::int64_t prepaid = 0;
	// A quantity input addresses the native owner directly, without a stock proxy.
	std::string property, equipment, officer;
	std::int64_t compartment = -1;
	bool operator==(const RuleWorkCredit &) const = default;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RuleWorkCredit, payer, store, resource, prepaid, property, equipment, officer,
                                   compartment)

struct RuleExecutionState
{
	std::string key, definition, binding, source_key;
	RuleOwnerKind owner_kind = RuleOwnerKind::Equipment;
	NativeRuleContext context, applied_context;
	std::uint64_t generation = 0;
	std::int64_t started = 0, next = 0, last = -1;
	// Paid work leases the next native phase, so prices/rates cannot change
	// between resource quotation and physical actuation.
	std::int64_t lease_tick = -1, lease_permille = 0;
	std::int64_t stop_tick = -1;
	bool eligible = false, active = false, stopped = false;
	std::vector<RuleWorkCredit> credits;
	// Last applied native-phase output, retained across a phase-boundary save.
	std::int64_t output = 0;
	std::string blocker;
	bool operator==(const RuleExecutionState &) const = default;
};
struct ActiveRuleEffect
{
	std::uint64_t id = 0;
	std::string source, definition, group_source;
	PayloadHitContext context;
	NativeRuleContext origin;
	std::int64_t expiry = 0;
	std::map<std::string, RuleExecutionState> clocks;
	bool operator==(const ActiveRuleEffect &) const = default;
};
struct ActiveRuleTarget
{
	flecs::entity body;
};
struct RuleRuntime
{
	std::uint64_t next_generation = 1;
	std::map<std::string, RuleExecutionState> executions;
	std::set<std::string> regions;
	bool membership_initialised = false;
};

enum class RuleEventPhase
{
	Payload,
	Shield,
	Region,
	Source,
	Periodic
};
struct PendingRuleExecution
{
	std::uint64_t id = 0;
	std::int64_t due = 0;
	RuleEventPhase phase = RuleEventPhase::Payload;
	std::string rule, source_key;
	std::int64_t damage = 0;
	NativeRuleContext context;
};
inline constexpr std::size_t rule_instance_limit = 8192, rule_pending_limit = 8192;
} // namespace phoenix
