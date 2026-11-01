module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <span>
#include <map>
#include <limits>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

module phoenix.control.decisions;
import phoenix.knowledge;

namespace phoenix
{
std::vector<std::map<std::string, state_tree::Value>>
controller_evidence_candidates(const ConsoleControllerRuntime &controller, const ControllerObservationSources &input,
                               const ControllerObjectiveBinding &binding)
{
	using state_tree::Value;
	using Facts = std::map<std::string, Value>;
	const auto key = input.definitions.schema(binding.report_schema);
	const auto &schema = input.definitions.schemas.at(key - 1);
	KnowledgeStreamSelection streams;
	std::set<std::string> payload_reads;
	for (const auto &field : controller.brain->program.registry.observations)
		if (field.name.starts_with("report_payload." + binding.id + "."))
			payload_reads.insert(field.name);
	std::vector<Facts> result;
	KnowledgeTick route_tick = 0;
	bool route_sent = false;
	const auto prefix = "report/" + binding.report_template + "/";
	for (const auto &[receipt_key, receipt] : controller.report_receipts)
		if (receipt_key.starts_with(prefix))
		{
			route_sent = true;
			route_tick = std::max(route_tick, receipt.sent_tick);
		}

	for (const auto *records : {&input.board.records, &input.board.shared})
		for (auto row = records->rbegin(); row != records->rend(); ++row)
		{
			const auto &p = row->value.provenance;
			if (row->value.schema != key || !streams.take_latest(*row))
				continue;
			Facts facts;
			const auto number = [&](const char *name, std::int64_t value)
			{
				facts[std::string("report_candidate.") + name] = Value::integer(value);
			};
			const auto text = [&](const char *name, const std::string &value)
			{
				facts[std::string("report_candidate.") + name] = Value::string(value);
			};
			const auto flag = [&](const char *name, bool value)
			{
				facts[std::string("report_candidate.") + name] = Value::boolean(value);
			};
			const auto field = [&](const char *name) -> const KnowledgeValue *
			{
				for (const auto &definition : schema.fields)
					if (definition.name == name)
					{
						const auto found = row->value.fields.find(definition.key);
						return found == row->value.fields.end() ? nullptr : &found->second;
					}
				return nullptr;
			};
			auto target = static_cast<std::int64_t>(row->value.subject);
			if (const auto ship = field("ship"); ship && std::get_if<std::int64_t>(ship))
				target = std::get<std::int64_t>(*ship);
			for (const auto *name : {"system", "compartment", "mission"})
			{
				const auto value = field(name);
				text(name, value && std::get_if<std::string>(value) ? std::get<std::string>(*value) : "");
			}
			const auto receipt = controller.report_receipts.find(controller_report_key(binding.report_template, *row));
			const bool sent = receipt != controller.report_receipts.end();
			const bool changed = !sent || receipt->second.observed_tick != p.observed_tick ||
			                     receipt->second.revision != p.revision ||
			                     receipt->second.fingerprint != controller_report_fingerprint(*row);
			bool complete = true;
			for (const auto &definition : schema.fields)
			{
				const auto found = row->value.fields.find(definition.key);
				const auto name = "report_payload." + binding.id + "." + definition.name;
				const bool available = found != row->value.fields.end();
				complete &= available || (!binding.report_fields.contains(definition.name) &&
				                          (!definition.required || row->value.withdrawn));
				if (payload_reads.contains(name + ".available"))
					facts[name + ".available"] = Value::boolean(available);
				if (!payload_reads.contains(name))
					continue;
				if (available)
					std::visit(
					    [&](const auto &value)
					    {
						    using T = std::decay_t<decltype(value)>;
						    if constexpr (std::is_same_v<T, std::string>)
							    facts[name] = Value::string(value);
						    else if constexpr (std::is_same_v<T, bool>)
							    facts[name] = Value::boolean(value);
						    else
							    facts[name] = Value::integer(value);
					    },
					    found->second);
				else
					facts[name] = definition.type == KnowledgeType::Text      ? Value::string("")
					              : definition.type == KnowledgeType::Boolean ? Value::boolean(false)
					                                                          : Value::integer(0);
			}
			const bool future = p.observed_tick > input.tick;
			const bool stale = input.stale(*row, input.tick);
			constexpr KnowledgeTick safe_tick = 9007199254740991ULL;
			auto remaining = safe_tick;
			if (row->expires_tick)
				remaining = row->expires_tick > input.tick ? std::min(safe_tick, row->expires_tick - input.tick) : 0;
			if (schema.stale_after && !future)
			{
				const auto age = input.tick - p.observed_tick;
				remaining = std::min(remaining, age > schema.stale_after
				                                    ? KnowledgeTick{0}
				                                    : std::min(safe_tick - 1, schema.stale_after - age) + 1);
			}
			const auto required = binding.report_delay_ticks >= safe_tick ? safe_tick : binding.report_delay_ticks + 1;
			const bool can_deliver =
			    remaining > required && (binding.report_expiry_ticks == 0 || binding.report_expiry_ticks > required) &&
			    binding.report_delay_ticks < std::numeric_limits<KnowledgeTick>::max() - input.tick;
			number("remaining_validity", remaining);
			number("required_validity", required);
			number("record", row->id);
			number("subject", row->value.subject);
			number("target", target);
			number("source", p.source);
			number("revision", p.revision);
			number("observed_tick", p.observed_tick);
			number("age", future ? 0 : std::min<KnowledgeTick>(1000000000, input.tick - p.observed_tick));
			number("uncertainty", p.uncertainty);
			number("since_sent", !sent ? 1000000000
			                     : input.tick < receipt->second.sent_tick
			                         ? 0
			                         : std::min<KnowledgeTick>(1000000000, input.tick - receipt->second.sent_tick));
			number("since_route_sent", !route_sent ? 1000000000
			                           : input.tick < route_tick
			                               ? 0
			                               : std::min<KnowledgeTick>(1000000000, input.tick - route_tick));
			flag("available", !future && !stale && complete && can_deliver);
			flag("changed", changed);
			flag("withdrawn", row->value.withdrawn);
			text("kind", p.kind == EvidenceKind::Fact ? "fact" : p.kind == EvidenceKind::Claim ? "claim" : "intent");
			text("schema", binding.report_schema);
			text("message", binding.report_template);
			text("reason", !complete      ? "report_fields_unavailable"
			               : future       ? "future_report_evidence"
			               : stale        ? "stale_report_evidence"
			               : !can_deliver ? "report_expires_before_decision"
			               : !changed     ? "unchanged_report_evidence"
			                              : "permitted_report_evidence");
			result.push_back(std::move(facts));
		}
	if (result.empty())
	{
		Facts empty;
		for (const auto &field : controller.brain->program.registry.observations)
			if (field.name.starts_with("report_candidate.") ||
			    field.name.starts_with("report_payload." + binding.id + "."))
				empty[field.name] = field.type == state_tree::Type::Text      ? Value::string("")
				                    : field.type == state_tree::Type::Boolean ? Value::boolean(false)
				                                                              : Value::integer(0);
		empty["report_candidate.reason"] = Value::string("no_permitted_report_evidence");
		result.push_back(std::move(empty));
	}
	return result;
}
} // namespace phoenix
