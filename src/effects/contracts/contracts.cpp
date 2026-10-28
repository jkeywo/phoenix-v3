module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <charconv>
#include <variant>
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

module phoenix.effects.contracts;

namespace phoenix
{
std::string tree_effect_identity(const state_tree::Output &output, const std::string &scope)
{
	std::string receipt = scope + std::to_string(output.activation) + "/" + std::to_string(output.task);
	for (std::size_t i = 0; i < output.call_path.size(); ++i)
		receipt += "/" + std::to_string(output.call_path[i]) + ":" + std::to_string(output.call_activations.at(i));
	return receipt;
}

bool admit_effect(std::set<std::string> &receipts, const state_tree::Output &output, const std::string &scope)
{
	auto receipt = tree_effect_identity(output, scope);
	if (receipts.contains(receipt))
		return false;
	if (receipts.size() >= effect_receipt_limit)
		throw std::runtime_error("Tree effect journal exhausted");
	receipts.insert(std::move(receipt));
	return true;
}

void validate_effect_identity(const std::string &source, const std::string &equipment, bool requires_equipment)
{
	if (source.empty() || source.size() > 512 || equipment.size() > 256 || (requires_equipment && equipment.empty()))
		throw std::runtime_error("Invalid effect application identity");
}

std::string scenario_effect_source(std::uint64_t owner, const std::string &grant)
{
	if (grant.empty())
		throw std::runtime_error("Invalid effect source identity");
	auto source = "scenario." + std::to_string(owner) + "." + grant;
	validate_effect_identity(source, {}, false);
	return source;
}

std::int64_t effect_expiry(std::int64_t tick, std::int64_t duration)
{
	if (tick < 0 || tick > 9007199254740991LL || duration < 0 || duration > 1000000 ||
	    (duration > 0 && tick > 9007199254740991LL - duration))
		throw std::runtime_error("Effect expiry outside supported bounds");
	return duration > 0 ? tick + duration : -1;
}

std::int64_t refresh_effect_expiry(std::int64_t previous, std::int64_t requested)
{
	return previous < 0 || requested < 0 ? -1 : std::max(previous, requested);
}

void record_effect_result(std::map<std::uint64_t, std::string> &results, std::uint64_t id, std::string status)
{
	results[id] = std::move(status);
	while (results.size() > 1024)
		results.erase(results.begin());
}

std::string rule_key(const std::string &source, const std::string &binding, const RuleActor &subject)
{
	const auto value = source + ":" + std::to_string(binding.size()) + ":" + binding + ":" +
	                   std::to_string(subject.entity) + ":" + std::to_string(subject.ordnance) + ":" +
	                   std::to_string(static_cast<int>(subject.kind)) + ":" + subject.equipment + ":" +
	                   std::to_string(subject.compartment) + ":" + std::to_string(subject.contribution) + ":" +
	                   subject.contribution_name + ":" + subject.officer;
	return "rule." +
	       sha256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(value.data()), value.size()));
}

RuleActor rule_hit_actor(const PayloadHitContext &hit)
{
	RuleActor actor;
	actor.entity = hit.target;
	actor.equipment = hit.equipment;
	actor.ordnance = hit.ordnance;
	actor.kind = hit.kind;
	actor.compartment = hit.compartment;
	actor.contribution = hit.contribution;
	actor.contribution_name = hit.contribution_name;
	return actor;
}

PayloadHitContext rule_hit_context(const RuleActor &actor)
{
	return {actor.entity,  actor.kind, actor.compartment, actor.equipment, actor.contribution, actor.contribution_name,
	        actor.ordnance};
}

RuleActor rule_body_actor(RuleActor actor)
{
	actor.kind = PayloadOutcomeKind::Entity;
	actor.compartment = -1;
	actor.contribution = 0;
	actor.contribution_name.clear();
	actor.equipment.clear();
	actor.officer.clear();
	return actor;
}

RuleActor rule_actor(const NativeRuleContext &context, RuleRole role)
{
	switch (role)
	{
	case RuleRole::Owner:
		return context.owner;
	case RuleRole::Source:
		return context.source;
	case RuleRole::Instigator:
		return context.instigator;
	case RuleRole::Subject:
		return context.subject;
	case RuleRole::Target:
		return context.target;
	case RuleRole::Attacker:
		return rule_body_actor(context.instigator);
	case RuleRole::Protected:
		return rule_body_actor(context.subject);
	}
	throw std::runtime_error("Unknown native rule role");
}

RuleActor rule_actor(const NativeRuleContext &context, std::string_view role)
{
	if (role == "owner")
		return rule_actor(context, RuleRole::Owner);
	if (role == "source")
		return rule_actor(context, RuleRole::Source);
	if (role == "instigator")
		return rule_actor(context, RuleRole::Instigator);
	if (role == "subject")
		return rule_actor(context, RuleRole::Subject);
	if (role == "target")
		return rule_actor(context, RuleRole::Target);
	if (role == "attacker")
		return rule_actor(context, RuleRole::Attacker);
	if (role == "protected")
		return rule_actor(context, RuleRole::Protected);
	throw std::runtime_error("Unknown native rule role");
}

NativeRuleContext bind_rule_context(const RuleBinding &binding, NativeRuleContext context)
{
	context.target = rule_actor(context, binding.target);
	if (!binding.equipment.empty() && binding.equipment != "$target")
	{
		const auto equipment = binding.equipment == "$owner"    ? context.owner.equipment
		                       : binding.equipment == "$source" ? context.source.equipment
		                                                        : binding.equipment;
		context.target = rule_body_actor(context.target);
		context.target.equipment = equipment;
	}
	if (!binding.officer.empty())
	{
		context.target = rule_body_actor(context.target);
		context.target.officer = binding.officer;
	}
	return context;
}

bool same_payload_context(const PayloadHitContext &a, const PayloadHitContext &b)
{
	return std::tie(a.target, a.kind, a.compartment, a.equipment, a.contribution, a.contribution_name, a.ordnance) ==
	       std::tie(b.target, b.kind, b.compartment, b.equipment, b.contribution, b.contribution_name, b.ordnance);
}
CustomPropertyAddress custom_property_address(const RuleActor &actor)
{
	if (actor.ordnance || actor.kind == PayloadOutcomeKind::Shield ||
	    (!actor.equipment.empty() && !actor.officer.empty()))
		throw std::runtime_error("Invalid custom property owner");
	return {actor.entity, actor.equipment, actor.officer,
	        actor.equipment.empty() && actor.officer.empty() ? actor.compartment : -1};
}

RuleActor custom_property_actor(EntityId entity, const std::string &token)
{
	RuleActor actor{entity};
	if (token.starts_with("officer:"))
		actor.officer = token.substr(8);
	else if (token.starts_with("compartment:"))
	{
		actor.kind = PayloadOutcomeKind::Compartment;
		const auto text = std::string_view(token).substr(12);
		const auto parsed = std::from_chars(text.data(), text.data() + text.size(), actor.compartment);
		if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || actor.compartment < 0)
			throw std::runtime_error("Invalid custom compartment owner");
	}
	else
		actor.equipment = token;
	return actor;
}

std::string custom_modifier_target(const RuleActor &actor)
{
	if (!actor.officer.empty())
		return "officer:" + actor.officer;
	if (!actor.equipment.empty())
		return actor.equipment;
	if (actor.compartment >= 0)
		return "compartment:" + std::to_string(actor.compartment);
	return {};
}

state_tree::Value custom_property_scalar(const CustomPropertyValue &value)
{
	if (const auto *number = std::get_if<std::int64_t>(&value))
		return state_tree::Value::integer(*number);
	if (const auto *flag = std::get_if<bool>(&value))
		return state_tree::Value::boolean(*flag);
	return {state_tree::Type::Text, 0, std::get<std::string>(value)};
}

std::string native_property_rate_suffix(const NativeRuleContext &context)
{
	const auto key = std::to_string(context.owner.entity) + ":" +
	                 std::to_string(static_cast<int>(context.owner_effect_kind)) + ":" +
	                 std::to_string(context.owner_effect_id) + ":" + context.owner_grant;
	return ":owner." +
	       sha256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(key.data()), key.size()));
}

void to_json(Json &out, const RuleActor &value)
{
	out["entity"] = value.entity;
	out["equipment"] = value.equipment;
	out["officer"] = value.officer;
	out["ordnance"] = value.ordnance;
	out["kind"] = value.kind;
	out["compartment"] = value.compartment;
	out["contribution"] = value.contribution;
	out["contribution_name"] = value.contribution_name;
}
void from_json(const Json &in, RuleActor &value)
{
	in.at("entity").get_to(value.entity);
	in.at("equipment").get_to(value.equipment);
	in.at("officer").get_to(value.officer);
	in.at("ordnance").get_to(value.ordnance);
	in.at("kind").get_to(value.kind);
	in.at("compartment").get_to(value.compartment);
	in.at("contribution").get_to(value.contribution);
	in.at("contribution_name").get_to(value.contribution_name);
}

void to_json(Json &out, const RuleWorkCredit &value)
{
	out["payer"] = value.payer;
	out["store"] = value.store;
	out["resource"] = value.resource;
	out["prepaid"] = value.prepaid;
	out["property"] = value.property;
	out["equipment"] = value.equipment;
	out["officer"] = value.officer;
	out["compartment"] = value.compartment;
	out["unit_work"] = value.unit_work;
}
void from_json(const Json &in, RuleWorkCredit &value)
{
	in.at("payer").get_to(value.payer);
	in.at("store").get_to(value.store);
	in.at("resource").get_to(value.resource);
	in.at("prepaid").get_to(value.prepaid);
	in.at("property").get_to(value.property);
	in.at("equipment").get_to(value.equipment);
	in.at("officer").get_to(value.officer);
	in.at("compartment").get_to(value.compartment);
	in.at("unit_work").get_to(value.unit_work);
}

void to_json(Json &out, const ExecutionWorkReceipt &value)
{
	out["source"] = value.source;
	out["owner"] = value.owner;
	out["family"] = value.family;
	out["definition"] = value.definition;
	out["activation"] = value.activation;
	out["output"] = value.output;
	out["execution"] = value.execution;
	out["work"] = value.work;
	out["work_unit"] = value.work_unit;
	out["generation"] = value.generation;
}
void from_json(const Json &in, ExecutionWorkReceipt &value)
{
	in.at("source").get_to(value.source);
	in.at("owner").get_to(value.owner);
	in.at("family").get_to(value.family);
	in.at("definition").get_to(value.definition);
	in.at("activation").get_to(value.activation);
	in.at("output").get_to(value.output);
	in.at("execution").get_to(value.execution);
	in.at("work").get_to(value.work);
	in.at("work_unit").get_to(value.work_unit);
	in.at("generation").get_to(value.generation);
}

void to_json(Json &out, const PayloadHitContext &value)
{
	out["target"] = value.target;
	out["kind"] = value.kind;
	out["compartment"] = value.compartment;
	out["equipment"] = value.equipment;
	out["contribution"] = value.contribution;
	out["contribution_name"] = value.contribution_name;
	out["ordnance"] = value.ordnance;
}
void from_json(const Json &in, PayloadHitContext &value)
{
	in.at("target").get_to(value.target);
	in.at("kind").get_to(value.kind);
	in.at("compartment").get_to(value.compartment);
	in.at("equipment").get_to(value.equipment);
	in.at("contribution").get_to(value.contribution);
	in.at("contribution_name").get_to(value.contribution_name);
	in.at("ordnance").get_to(value.ordnance);
}

void to_json(Json &out, const NativeRuleContext &value)
{
	out["owner"] = value.owner;
	out["source"] = value.source;
	out["instigator"] = value.instigator;
	out["subject"] = value.subject;
	out["target"] = value.target;
	out["hit"] = value.hit;
	out["property_role"] = value.property_role;
	out["property_filter"] = value.property_filter;
	out["region"] = value.region;
	out["owner_grant"] = value.owner_grant;
	out["owner_effect_kind"] = value.owner_effect_kind;
	out["owner_effect_id"] = value.owner_effect_id;
	out["capture_owner"] = value.capture_owner;
	out["tick"] = value.tick;
	out["elapsed"] = value.elapsed;
	out["presented"] = value.presented;
	out["modified"] = value.modified;
	out["negated"] = value.negated;
	out["absorbed"] = value.absorbed;
	out["residual"] = value.residual;
	out["strength"] = value.strength;
	out["inflicted"] = value.inflicted;
	out["output"] = value.output;
	out["captured"] = value.captured;
}
void from_json(const Json &in, NativeRuleContext &value)
{
	in.at("owner").get_to(value.owner);
	in.at("source").get_to(value.source);
	in.at("instigator").get_to(value.instigator);
	in.at("subject").get_to(value.subject);
	in.at("target").get_to(value.target);
	in.at("hit").get_to(value.hit);
	in.at("property_role").get_to(value.property_role);
	in.at("property_filter").get_to(value.property_filter);
	in.at("region").get_to(value.region);
	in.at("owner_grant").get_to(value.owner_grant);
	in.at("owner_effect_kind").get_to(value.owner_effect_kind);
	in.at("owner_effect_id").get_to(value.owner_effect_id);
	in.at("capture_owner").get_to(value.capture_owner);
	in.at("tick").get_to(value.tick);
	in.at("elapsed").get_to(value.elapsed);
	in.at("presented").get_to(value.presented);
	in.at("modified").get_to(value.modified);
	in.at("negated").get_to(value.negated);
	in.at("absorbed").get_to(value.absorbed);
	in.at("residual").get_to(value.residual);
	in.at("strength").get_to(value.strength);
	in.at("inflicted").get_to(value.inflicted);
	in.at("output").get_to(value.output);
	in.at("captured").get_to(value.captured);
}

void to_json(Json &out, const RuleCapturedFact &capture)
{
	out = {{"role", capture.fact.role},        {"kind", capture.fact.kind},      {"key", capture.fact.key},
	       {"aux", capture.fact.aux},          {"unit", capture.fact.unit},      {"type", capture.fact.type},
	       {"value_type", capture.value.type}, {"number", capture.value.number}, {"text", capture.value.text}};
}

void from_json(const Json &in, RuleCapturedFact &capture)
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
} // namespace phoenix
