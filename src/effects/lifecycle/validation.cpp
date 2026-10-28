module;
#include <algorithm>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

module phoenix.effects.continuation_validation;

namespace phoenix
{
namespace
{
static void rule_saved_fields(const Json &value, std::initializer_list<const char *> fields)
{
	if (!value.is_object() || value.size() != fields.size())
		throw std::runtime_error("rules: invalid saved object layout");
	for (const auto field : fields)
		if (!value.contains(field))
			throw std::runtime_error("rules: missing saved field");
}

static std::int64_t rule_saved_integer(const Json &value, std::int64_t minimum = 0,
                                       std::int64_t maximum = 9007199254740991LL)
{
	return checked_integer(value, minimum, maximum, "rules.saved");
}

static void rule_saved_text(const Json &value, std::size_t maximum = 4096, bool empty = true)
{
	if (!value.is_string() || value.get_ref<const std::string &>().size() > maximum ||
	    (!empty && value.get_ref<const std::string &>().empty()))
		throw std::runtime_error("rules: invalid saved identity");
}

static void rule_saved_bool(const Json &value)
{
	if (!value.is_boolean())
		throw std::runtime_error("rules: invalid saved boolean");
}
} // namespace
RuleContinuationValidator::RuleContinuationValidator(RuleValidationDefinitions definitions, RuleValidationPorts ports,
                                                     std::int64_t tick)
    : definitions_(definitions), ports_(std::move(ports)), tick_(tick)
{
}
void RuleContinuationValidator::actor(const Json &value) const
{
	rule_saved_fields(value, {"entity", "equipment", "officer", "ordnance", "kind", "compartment", "contribution",
	                          "contribution_name"});
	rule_saved_integer(value.at("entity"));
	rule_saved_integer(value.at("kind"), 0, static_cast<int>(PayloadOutcomeKind::Entity));
	rule_saved_integer(value.at("compartment"), -1);
	rule_saved_integer(value.at("contribution"));
	rule_saved_text(value.at("equipment"), 1024);
	rule_saved_text(value.at("officer"), 256);
	rule_saved_text(value.at("contribution_name"), 256);
	rule_saved_bool(value.at("ordnance"));
	const auto actor = value.get<RuleActor>();
	if ((!actor.entity && (!actor.equipment.empty() || !actor.officer.empty() || actor.ordnance)) ||
	    (actor.ordnance && (actor.kind != PayloadOutcomeKind::Entity || !actor.officer.empty())) ||
	    ((actor.kind == PayloadOutcomeKind::Compartment || actor.kind == PayloadOutcomeKind::System) !=
	     (actor.compartment >= 0)) ||
	    (actor.kind == PayloadOutcomeKind::Shield ? !actor.contribution || actor.contribution_name.empty()
	                                              : actor.contribution || !actor.contribution_name.empty()) ||
	    (actor.kind == PayloadOutcomeKind::System && actor.equipment.empty()))
		throw std::runtime_error("rules: malformed saved native address");
	ports_.actor_identity(actor);
}

void RuleContinuationValidator::context(const Json &value) const
{
	rule_saved_fields(value, {"owner",
	                          "source",
	                          "instigator",
	                          "subject",
	                          "target",
	                          "hit",
	                          "property_role",
	                          "property_filter",
	                          "region",
	                          "owner_grant",
	                          "owner_effect_kind",
	                          "owner_effect_id",
	                          "capture_owner",
	                          "tick",
	                          "elapsed",
	                          "presented",
	                          "modified",
	                          "negated",
	                          "absorbed",
	                          "residual",
	                          "strength",
	                          "inflicted",
	                          "output",
	                          "captured"});
	for (const auto role : {"owner", "source", "instigator", "subject", "target"})
		this->actor(value.at(role));
	const auto &hit = value.at("hit");
	rule_saved_fields(hit,
	                  {"target", "kind", "compartment", "equipment", "contribution", "contribution_name", "ordnance"});
	Json actor = hit;
	actor["entity"] = actor.at("target");
	actor.erase("target");
	actor["officer"] = "";
	this->actor(actor);
	for (const auto field : {"property_role", "property_filter", "region"})
		rule_saved_text(value.at(field), 256);
	rule_saved_text(value.at("owner_grant"), 1024);
	rule_saved_integer(value.at("owner_effect_kind"), 0, static_cast<int>(NativeEffectKind::Process));
	rule_saved_integer(value.at("owner_effect_id"));
	rule_saved_bool(value.at("capture_owner"));
	for (const auto field :
	     {"tick", "elapsed", "presented", "modified", "negated", "absorbed", "residual", "strength", "inflicted"})
		rule_saved_integer(value.at(field));
	rule_saved_integer(value.at("output"), 0, 1000);
	const auto &captures = value.at("captured");
	if (!captures.is_array() || captures.size() > rule_expression_node_limit)
		throw std::runtime_error("rules: invalid saved captured layout");
	std::vector<RuleFactBinding> seen;
	for (const auto &saved : captures)
	{
		rule_saved_fields(saved, {"role", "kind", "key", "aux", "unit", "type", "value_type", "number", "text"});
		rule_saved_integer(saved.at("role"), 0, static_cast<int>(RuleRole::Protected));
		rule_saved_integer(saved.at("kind"), 0, static_cast<int>(RuleFactKind::OfficerOnDuty));
		rule_saved_integer(saved.at("type"), 0, static_cast<int>(state_tree::Type::Message));
		rule_saved_integer(saved.at("value_type"), 0, static_cast<int>(state_tree::Type::Message));
		rule_saved_integer(saved.at("number"), -9007199254740991LL);
		for (const auto field : {"key", "aux", "unit", "text"})
			rule_saved_text(saved.at(field));
		const auto capture = saved.get<RuleCapturedFact>();
		const bool declared = std::any_of(definitions_.rules.begin(), definitions_.rules.end(),
		                                  [&](const auto &rule)
		                                  {
			                                  return std::find(rule.second.facts.begin(), rule.second.facts.end(),
			                                                   capture.fact) != rule.second.facts.end();
		                                  });
		if (!declared ||
		    (capture.fact.role != RuleRole::Owner && capture.fact.role != RuleRole::Source &&
		     capture.fact.role != RuleRole::Instigator && capture.fact.kind != RuleFactKind::PropertyBefore &&
		     capture.fact.kind != RuleFactKind::PropertyAfter) ||
		    std::find(seen.begin(), seen.end(), capture.fact) != seen.end() ||
		    (capture.value.type != state_tree::Type::Missing && capture.value.type != capture.fact.type) ||
		    (capture.value.type == state_tree::Type::Boolean && capture.value.number != 0 &&
		     capture.value.number != 1) ||
		    (capture.value.type != state_tree::Type::Text && !capture.value.text.empty()))
			throw std::runtime_error("rules: captured fact layout/type mismatch");
		if (capture.value.type != state_tree::Type::Missing &&
		    (capture.fact.kind == RuleFactKind::Property || capture.fact.kind == RuleFactKind::PropertyBefore ||
		     capture.fact.kind == RuleFactKind::PropertyAfter) &&
		    definitions_.properties.contains(capture.fact.key))
		{
			if (capture.value.type == state_tree::Type::Text && capture.value.number != 0)
				throw std::runtime_error("rules: invalid captured custom enum scalar");
			const auto &definition = definitions_.properties.at(capture.fact.key);
			const Json scalar = capture.value.type == state_tree::Type::Text      ? Json(capture.value.text)
			                    : capture.value.type == state_tree::Type::Boolean ? Json(capture.value.number != 0)
			                                                                      : Json(capture.value.number);
			(void)read_custom_property_value(definition, scalar, "rules.capture." + capture.fact.key);
		}
		seen.push_back(capture.fact);
	}
	const auto context = value.get<NativeRuleContext>();
	if ((context.owner_effect_kind == NativeEffectKind::None) != (context.owner_effect_id == 0) ||
	    (context.owner_effect_id && context.owner_grant.empty()))
		throw std::runtime_error("rules: malformed saved native owner identity");
	if (context.tick > tick_)
		throw std::runtime_error("rules: saved context is from a future phase");
	if (!context.region.empty() && !definitions_.regions.contains(context.region))
		throw std::runtime_error("rules: unknown saved region");
	const auto &properties = modifier_properties();
	if (!context.property_filter.empty() && !definitions_.properties.contains(context.property_filter) &&
	    std::none_of(properties.begin(), properties.end(),
	                 [&](const auto &property)
	                 {
		                 return property.id == context.property_filter;
	                 }))
		throw std::runtime_error("rules: unknown saved property filter");
}

void RuleContinuationValidator::credit(const Json &credit) const
{
	rule_saved_fields(credit, {"payer", "store", "resource", "prepaid", "property", "equipment", "officer",
	                           "compartment", "unit_work"});
	rule_saved_integer(credit.at("payer"), 1);
	rule_saved_integer(credit.at("prepaid"));
	rule_saved_integer(credit.at("compartment"), -1);
	const auto denomination = rule_saved_integer(credit.at("unit_work"), resource_work_unit, 1000000000);
	if (denomination % resource_work_unit)
		throw std::runtime_error("rules: invalid work credit denomination");
	for (const auto field : {"store", "resource", "property", "equipment", "officer"})
		rule_saved_text(credit.at(field), 1024);
	const auto entry = credit.get<RuleWorkCredit>();
	if (!entry.property.empty())
	{
		if (!entry.store.empty() || !entry.resource.empty() || !definitions_.properties.contains(entry.property) ||
		    definitions_.properties.at(entry.property).kind != CustomPropertyKind::Quantity)
			throw std::runtime_error("rules: invalid saved quantity credit");
		const CustomPropertyAddress address{entry.payer, entry.equipment, entry.officer, entry.compartment};
		const RuleActor actor{entry.payer,      entry.equipment, entry.officer, false, PayloadOutcomeKind::Entity,
		                      entry.compartment};
		if (custom_property_address(actor) != address)
			throw std::runtime_error("rules: noncanonical saved quantity owner");
		if (ports_.physical_exists(entry.payer))
		{
			if (!ports_.quantity_compatible(address, entry.property))
				throw std::runtime_error("rules: incompatible saved quantity owner");
		}
	}
	else
	{
		if (entry.store.empty() || entry.resource.empty() || !entry.equipment.empty() || !entry.officer.empty() ||
		    entry.compartment != -1)
			throw std::runtime_error("rules: invalid saved stock credit");
		(void)definitions_.resources.at(entry.resource);
		if (ports_.physical_exists(entry.payer) && !ports_.stock_accepts(entry.payer, entry.store, entry.resource))
			throw std::runtime_error("rules: incompatible saved credit store");
	}
}

void RuleContinuationValidator::state(const Json &value, std::int64_t now) const
{
	rule_saved_fields(value, {"key",     "definition",      "binding",        "source_key", "owner_kind",
	                          "context", "applied_context", "generation",     "started",    "next",
	                          "last",    "lease_tick",      "lease_permille", "stop_tick",  "eligible",
	                          "active",  "stopped",         "credits",        "output",     "blocker"});
	for (const auto field : {"key", "definition", "binding", "source_key"})
		rule_saved_text(value.at(field), 1024, false);
	rule_saved_text(value.at("blocker"), 4096);
	rule_saved_integer(value.at("owner_kind"), 0, static_cast<int>(RuleOwnerKind::Process));
	rule_saved_integer(value.at("generation"), 1);
	rule_saved_integer(value.at("started"), 0, now);
	rule_saved_integer(value.at("next"));
	rule_saved_integer(value.at("last"), -1, now);
	rule_saved_integer(value.at("lease_tick"), -1, now + 1);
	rule_saved_integer(value.at("stop_tick"), -1, now + 1);
	rule_saved_integer(value.at("lease_permille"), 0, 1000);
	rule_saved_integer(value.at("output"), 0, 1000);
	for (const auto field : {"eligible", "active", "stopped"})
		rule_saved_bool(value.at(field));
	this->context(value.at("context"));
	this->context(value.at("applied_context"));
	const auto &credits = value.at("credits");
	if (!credits.is_array() || credits.size() > 4096)
		throw std::runtime_error("rules: saved credit capacity exceeded");
	std::set<std::tuple<EntityId, std::string, std::string, std::string, std::string, std::string, std::int64_t>> seen;
	for (const auto &credit : credits)
	{
		this->credit(credit);
		const auto entry = credit.get<RuleWorkCredit>();
		if (!seen.emplace(entry.payer, entry.store, entry.resource, entry.property, entry.equipment, entry.officer,
		                  entry.compartment)
		         .second)
			throw std::runtime_error("rules: duplicate prepaid work credit");
	}
	const auto state = value.get<RuleExecutionState>();
	if (!definitions_.rules.contains(state.definition) || state.context.tick > now || state.next < state.started ||
	    (!state.eligible && (state.active || state.output || state.lease_tick != -1 || !state.credits.empty())) ||
	    (state.stopped && state.output) || (state.lease_tick == -1 && state.lease_permille))
		throw std::runtime_error("rules: invalid saved clock frontier");
	if (state.next > now + definitions_.rules.at(state.definition).period_ticks)
		throw std::runtime_error("rules: saved clock exceeds its native cadence frontier");
	const bool applied = state.active && definitions_.rules.at(state.definition).phase == RulePhase::Maintained;
	if (applied ? state.applied_context.owner != state.context.owner || !state.applied_context.captured.empty()
	            : state.applied_context != NativeRuleContext{})
		throw std::runtime_error("rules: invalid saved applied-context authority");
}
} // namespace phoenix
