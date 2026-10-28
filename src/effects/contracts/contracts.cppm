module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
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
#include <variant>
#include <vector>

export module phoenix.effects.contracts;
export import phoenix.content.effects.types;
export import phoenix.contracts;
export import phoenix.state_tree;
import phoenix.resources.stocks;

export namespace phoenix
{
enum class EffectApplicationOperation
{
	Apply,
	Refresh,
	Remove
};
// Captured native outcome. Source identity is diagnostic/lifetime identity, never authority.
struct PayloadHitContext
{
	EntityId target = 0;
	PayloadOutcomeKind kind = PayloadOutcomeKind::Entity;
	std::int64_t compartment = -1;
	std::string equipment;
	std::uint64_t contribution = 0;
	std::string contribution_name;
	bool ordnance = false;
	bool operator==(const PayloadHitContext &) const = default;
};

inline constexpr std::size_t custom_property_rate_limit = 8192;
// Values live on the real native owner; addresses are temporary boundary views.
struct CustomPropertyState
{
	std::map<std::string, CustomPropertyValue> bases;
	std::map<std::string, std::int64_t> remainders;
	bool operator==(const CustomPropertyState &) const = default;
};
struct CustomPropertyAddress
{
	EntityId entity = 0;
	std::string equipment, officer;
	std::int64_t compartment = -1;
	bool operator==(const CustomPropertyAddress &) const = default;
	bool operator<(const CustomPropertyAddress &other) const
	{
		return std::tie(entity, equipment, officer, compartment) <
		       std::tie(other.entity, other.equipment, other.officer, other.compartment);
	}
};

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
void to_json(Json &, const RuleActor &);
void from_json(const Json &, RuleActor &);

struct RuleCapturedFact
{
	RuleFactBinding fact;
	state_tree::Value value;
	bool operator==(const RuleCapturedFact &) const = default;
};
void to_json(Json &out, const RuleCapturedFact &capture);
void from_json(const Json &in, RuleCapturedFact &capture);

// A producer freezes event facts here. Only a projectile launch captures origin
// properties; ordinary defensive and maintained rules read their current host.
enum class NativeEffectKind
{
	None,
	Shield,
	Modifier,
	Ability,
	Periodic,
	Process
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
	Shield,
	Process
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
// Derived scratch shared by native/custom modifier reads, never persisted.
using RuleSourceAvailabilityCache = std::map<EntityId, std::map<std::string, bool>>;
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
	std::int64_t unit_work = resource_work_unit;
	bool operator==(const RuleWorkCredit &) const = default;
};
void to_json(Json &, const RuleWorkCredit &);
void from_json(const Json &, RuleWorkCredit &);

// Both abilities and processes publish one frozen receipt after native work.
// Output is normalized activity; work is measured in the declared native unit.
struct ExecutionWorkReceipt
{
	std::string source, owner, family, definition;
	std::uint64_t activation = 0;
	std::int64_t output = 0;
	std::string execution;
	std::int64_t work = 0;
	std::string work_unit = "permille";
	std::uint64_t generation = 0;
	bool operator==(const ExecutionWorkReceipt &) const = default;
};
void to_json(Json &, const ExecutionWorkReceipt &);
void from_json(const Json &, ExecutionWorkReceipt &);

struct NativeCompartmentOutcome
{
	std::int64_t hp = 0, work = 0;
	bool disabled = false;
	bool operator==(const NativeCompartmentOutcome &) const = default;
};
struct NativeFaultOutcome
{
	bool active = false, diagnosed = false;
	std::int64_t work = 0;
	bool operator==(const NativeFaultOutcome &) const = default;
};
struct NativeStructureOutcome
{
	std::vector<NativeCompartmentOutcome> compartments;
	std::map<std::string, std::vector<NativeFaultOutcome>> faults;
	bool operator==(const NativeStructureOutcome &) const = default;
};
struct NativePatientOutcome
{
	std::string condition, facility;
	bool stabilised = false;
	std::int64_t treatment = 0, recovery = 0;
	bool operator==(const NativePatientOutcome &) const = default;
};
struct NativeTeamOutcome
{
	std::string condition;
	std::int64_t treatment = 0;
	std::uint64_t revision = 0;
	bool operator==(const NativeTeamOutcome &) const = default;
};
inline constexpr std::size_t rule_instance_limit = 8192, rule_pending_limit = 8192;

void to_json(Json &, const PayloadHitContext &);
void from_json(const Json &, PayloadHitContext &);

void to_json(Json &, const NativeRuleContext &);
void from_json(const Json &, NativeRuleContext &);

inline constexpr std::size_t effect_receipt_limit = 65536;
std::string tree_effect_identity(const state_tree::Output &, const std::string &scope = {});
bool admit_effect(std::set<std::string> &, const state_tree::Output &, const std::string &scope = {});
void validate_effect_identity(const std::string &, const std::string &, bool requires_equipment);
std::string scenario_effect_source(std::uint64_t owner, const std::string &grant);
std::int64_t effect_expiry(std::int64_t tick, std::int64_t duration);
std::int64_t refresh_effect_expiry(std::int64_t previous, std::int64_t requested);
void record_effect_result(std::map<std::uint64_t, std::string> &, std::uint64_t id, std::string status);
std::string rule_key(const std::string &source, const std::string &binding, const RuleActor &subject);
RuleActor rule_hit_actor(const PayloadHitContext &);
PayloadHitContext rule_hit_context(const RuleActor &);
RuleActor rule_body_actor(RuleActor);
RuleActor rule_actor(const NativeRuleContext &, RuleRole);
RuleActor rule_actor(const NativeRuleContext &, std::string_view);
NativeRuleContext bind_rule_context(const RuleBinding &, NativeRuleContext);
bool same_payload_context(const PayloadHitContext &, const PayloadHitContext &);
CustomPropertyAddress custom_property_address(const RuleActor &);
RuleActor custom_property_actor(EntityId entity, const std::string &token);
std::string custom_modifier_target(const RuleActor &);
state_tree::Value custom_property_scalar(const CustomPropertyValue &);
std::string native_property_rate_suffix(const NativeRuleContext &);
} // namespace phoenix
