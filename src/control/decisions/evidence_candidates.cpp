module;
#include <optional>
#include <array>
#include <utility>
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
#include "candidate_adapters.hpp"
std::vector<CandidateFacts> controller_evidence_candidates(const ConsoleControllerRuntime &controller,
                                                           const ControllerObservationSources &input,
                                                           const ControllerObjectiveBinding &binding)
{
	using state_tree::Value;
	using Facts = CandidateFacts;
	const auto &compiled = compiled_objective_binding(controller, binding);
	const auto key = compiled.report_schema;
	KnowledgeStreamSelection streams;
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
			const auto number = [&](CandidateField name, std::int64_t value)
			{
				facts[name] = Value::integer(value);
			};
			const auto text = [&](CandidateField name, const std::string &value)
			{
				facts[name] = Value::string(value);
			};
			const auto flag = [&](CandidateField name, bool value)
			{
				facts[name] = Value::boolean(value);
			};
			const auto field = [&](std::size_t scope) -> const KnowledgeValue *
			{
				const auto found = row->value.fields.find(compiled.report_scope.at(scope));
				return found == row->value.fields.end() ? nullptr : &found->second;
			};
			auto target = static_cast<std::int64_t>(row->value.subject);
			if (const auto ship = field(0); ship && std::get_if<std::int64_t>(ship))
				target = std::get<std::int64_t>(*ship);
			constexpr std::array<CandidateField, 3> scopes{CandidateField::report_candidate_system,
			                                               CandidateField::report_candidate_compartment,
			                                               CandidateField::report_candidate_mission};
			for (std::size_t i = 0; i < scopes.size(); ++i)
			{
				const auto value = field(i + 1);
				text(scopes[i], value && std::get_if<std::string>(value) ? std::get<std::string>(*value) : "");
			}
			const auto receipt = controller.report_receipts.find(controller_report_key(binding.report_template, *row));
			const bool sent = receipt != controller.report_receipts.end();
			const bool changed = !sent || receipt->second.observed_tick != p.observed_tick ||
			                     receipt->second.revision != p.revision ||
			                     receipt->second.fingerprint != controller_report_fingerprint(*row);
			bool complete = true;
			for (const auto &definition : compiled.report_fields)
			{
				const auto found = row->value.fields.find(definition.key);
				const bool available = found != row->value.fields.end();
				complete &= available || (!definition.selected && (!definition.required || row->value.withdrawn));
				if (definition.available_slot)
					facts.set_bound(*definition.available_slot, Value::boolean(available));
				if (!definition.value_slot)
					continue;
				auto value = definition.type == KnowledgeType::Text      ? Value::string("")
				             : definition.type == KnowledgeType::Boolean ? Value::boolean(false)
				                                                         : Value::integer(0);
				if (available)
					std::visit(
					    [&](const auto &item)
					    {
						    using T = std::decay_t<decltype(item)>;
						    if constexpr (std::is_same_v<T, std::string>)
							    value = Value::string(item);
						    else if constexpr (std::is_same_v<T, bool>)
							    value = Value::boolean(item);
						    else
							    value = Value::integer(item);
					    },
					    found->second);
				facts.set_bound(*definition.value_slot, std::move(value));
			}
			const bool future = p.observed_tick > input.tick;
			const bool stale = input.stale(*row, input.tick);
			constexpr KnowledgeTick safe_tick = 9007199254740991ULL;
			auto remaining = safe_tick;
			if (row->expires_tick)
				remaining = row->expires_tick > input.tick ? std::min(safe_tick, row->expires_tick - input.tick) : 0;
			if (compiled.report_stale_after && !future)
			{
				const auto age = input.tick - p.observed_tick;
				remaining = std::min(remaining, age > compiled.report_stale_after
				                                    ? KnowledgeTick{0}
				                                    : std::min(safe_tick - 1, compiled.report_stale_after - age) + 1);
			}
			const auto required = binding.report_delay_ticks >= safe_tick ? safe_tick : binding.report_delay_ticks + 1;
			const bool can_deliver =
			    remaining > required && (binding.report_expiry_ticks == 0 || binding.report_expiry_ticks > required) &&
			    binding.report_delay_ticks < std::numeric_limits<KnowledgeTick>::max() - input.tick;
			number(CandidateField::report_candidate_remaining_validity, remaining);
			number(CandidateField::report_candidate_required_validity, required);
			number(CandidateField::report_candidate_record, row->id);
			number(CandidateField::report_candidate_subject, row->value.subject);
			number(CandidateField::report_candidate_target, target);
			number(CandidateField::report_candidate_source, p.source);
			number(CandidateField::report_candidate_revision, p.revision);
			number(CandidateField::report_candidate_observed_tick, p.observed_tick);
			number(CandidateField::report_candidate_age,
			       future ? 0 : std::min<KnowledgeTick>(1000000000, input.tick - p.observed_tick));
			number(CandidateField::report_candidate_uncertainty, p.uncertainty);
			number(CandidateField::report_candidate_since_sent,
			       !sent ? 1000000000
			       : input.tick < receipt->second.sent_tick
			           ? 0
			           : std::min<KnowledgeTick>(1000000000, input.tick - receipt->second.sent_tick));
			number(CandidateField::report_candidate_since_route_sent,
			       !route_sent               ? 1000000000
			       : input.tick < route_tick ? 0
			                                 : std::min<KnowledgeTick>(1000000000, input.tick - route_tick));
			flag(CandidateField::report_candidate_available, !future && !stale && complete && can_deliver);
			flag(CandidateField::report_candidate_changed, changed);
			flag(CandidateField::report_candidate_withdrawn, row->value.withdrawn);
			text(CandidateField::report_candidate_kind, p.kind == EvidenceKind::Fact    ? "fact"
			                                            : p.kind == EvidenceKind::Claim ? "claim"
			                                                                            : "intent");
			text(CandidateField::report_candidate_schema, binding.report_schema);
			text(CandidateField::report_candidate_message, binding.report_template);
			text(CandidateField::report_candidate_reason, !complete      ? "report_fields_unavailable"
			                                              : future       ? "future_report_evidence"
			                                              : stale        ? "stale_report_evidence"
			                                              : !can_deliver ? "report_expires_before_decision"
			                                              : !changed     ? "unchanged_report_evidence"
			                                                             : "permitted_report_evidence");
			result.push_back(std::move(facts));
		}
	if (result.empty())
	{
		auto empty = empty_candidate_facts(controller, CandidateGroup::report_candidate);
		for (const auto &definition : compiled.report_fields)
		{
			if (definition.available_slot)
				empty.set_bound(*definition.available_slot, Value::boolean(false));
			if (definition.value_slot)
				empty.set_bound(*definition.value_slot, definition.type == KnowledgeType::Text ? Value::string("")
				                                        : definition.type == KnowledgeType::Boolean
				                                            ? Value::boolean(false)
				                                            : Value::integer(0));
		}
		empty[CandidateField::report_candidate_reason] = Value::string("no_permitted_report_evidence");
		result.push_back(std::move(empty));
	}
	return result;
}
CandidateAdapter reports_adapter()
{
	return {"reports",
	        "objective_parameter_outside_known_report_scope",
	        false,
	        [](const CandidateCache &cache, const ConsoleControllerRuntime &controller,
	           const ControllerObservationSources &input, const ControllerObjectiveBinding &binding,
	           const KnowledgeRecord &record,
	           std::span<const state_tree::Value> source) -> std::vector<DecisionCandidateFrame>
	        {
		        return candidate_frames(controller_evidence_candidates(controller, input, binding), controller, source);
	        },
	        [](CandidateContext &context)
	        {
		        if (context.source() == CandidateSource::reports &&
		            ((!context.text(*context.record, ObjectiveField::mission, "").empty() &&
		              context.text(*context.record, ObjectiveField::mission, "") !=
		                  context.fact(CandidateField::report_candidate_mission).text) ||
		             (!context.text(*context.record, ObjectiveField::compartment, "").empty() &&
		              context.text(*context.record, ObjectiveField::compartment, "") !=
		                  context.fact(CandidateField::report_candidate_compartment).text) ||
		             (!context.text(*context.record, ObjectiveField::system, "").empty() &&
		              context.text(*context.record, ObjectiveField::system, "") !=
		                  context.fact(CandidateField::report_candidate_system).text) ||
		             (context.number(*context.record, ObjectiveField::target) &&
		              context.number(*context.record, ObjectiveField::target) !=
		                  context.fact(CandidateField::report_candidate_target).number)))
			        return false;
		        return true;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
		        if (context.source() == CandidateSource::reports && candidate.reason == "binding_not_applicable" &&
		            !context.fact(CandidateField::report_candidate_available).number)
			        candidate.reason = context.fact(CandidateField::report_candidate_reason).text;
		        if (context.source() == CandidateSource::reports)
			        candidate.target = context.fact(CandidateField::report_candidate_subject).number;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
	        },
	        [](CandidateContext &context) -> Json
	        {
		        if (context.source() == CandidateSource::reports)
		        {
			        Json evidence;
			        constexpr std::array entries{
			            std::pair{"record", CandidateField::report_candidate_record},
			            std::pair{"subject", CandidateField::report_candidate_subject},
			            std::pair{"source", CandidateField::report_candidate_source},
			            std::pair{"schema", CandidateField::report_candidate_schema},
			            std::pair{"message", CandidateField::report_candidate_message},
			            std::pair{"kind", CandidateField::report_candidate_kind},
			            std::pair{"age", CandidateField::report_candidate_age},
			            std::pair{"uncertainty", CandidateField::report_candidate_uncertainty},
			            std::pair{"withdrawn", CandidateField::report_candidate_withdrawn},
			            std::pair{"changed", CandidateField::report_candidate_changed},
			            std::pair{"since_sent", CandidateField::report_candidate_since_sent},
			            std::pair{"since_route_sent", CandidateField::report_candidate_since_route_sent},
			            std::pair{"remaining_validity", CandidateField::report_candidate_remaining_validity},
			            std::pair{"required_validity", CandidateField::report_candidate_required_validity}};
			        for (const auto &[name, field] : entries)
			        {
				        const auto &value = context.fact(field);
				        if (value.type == state_tree::Type::Text)
					        evidence[name] = value.text;
				        else if (value.type == state_tree::Type::Boolean)
					        evidence[name] = value.number != 0;
				        else
					        evidence[name] = value.number;
			        }
			        return evidence;
		        }
		        return {};
	        }};
}
} // namespace phoenix
