#pragma once
namespace phoenix
{
// Native addresses are distinct from effect provenance and from control authority.
struct RuleActor
{
	EntityId entity = 0;
	std::string equipment, officer;
	bool operator==(const RuleActor &) const = default;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RuleActor, entity, equipment, officer)

struct RuleCapturedFact
{
	RuleFactBinding fact;
	state_tree::Value value;
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
struct NativeRuleContext
{
	RuleActor owner, source, instigator, subject, target;
	PayloadHitContext hit;
	std::string property_role;
	std::int64_t tick = 0, elapsed = 0, presented = 0, modified = 0, negated = 0, absorbed = 0, residual = 0,
	             strength = 0, inflicted = 0, output = 1000;
	std::vector<RuleCapturedFact> captured;
};

enum class RuleOwnerKind
{
	Equipment,
	Officer,
	Region,
	Effect,
	Shield
};
// These credits represent already consumed resources buying unfinished work.
// They cannot be spent as stock, transferred, or refunded on rule removal.
struct RuleWorkCredit
{
	EntityId payer = 0;
	std::string store, resource;
	std::int64_t prepaid = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RuleWorkCredit, payer, store, resource, prepaid)

struct RuleExecutionState
{
	std::string key, definition, binding, source_key;
	RuleOwnerKind owner_kind = RuleOwnerKind::Equipment;
	NativeRuleContext context;
	std::uint64_t generation = 0;
	std::int64_t started = 0, next = 0, last = -1;
	bool eligible = false, active = false, stopped = false;
	std::vector<RuleWorkCredit> credits;
	// Derived from this tick's frozen quote/allocation, never another stock ledger.
	std::int64_t output = 0;
	std::string blocker;
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
	Region
};
struct PendingRuleExecution
{
	std::uint64_t id = 0;
	std::int64_t due = 0;
	RuleEventPhase phase = RuleEventPhase::Payload;
	std::string rule, source_key;
	NativeRuleContext context;
};
inline constexpr std::size_t rule_instance_limit = 8192, rule_pending_limit = 8192;
} // namespace phoenix
