module;
#include <optional>
#include <algorithm>
#include <cstdint>
#include <span>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <variant>
#include <vector>

module phoenix.control.decisions;
import phoenix.control.coordination;

namespace phoenix
{
#include "candidate_adapters.hpp"
std::vector<std::map<std::string, state_tree::Value>>
controller_delivery_candidates(const ConsoleControllerRuntime &controller, const ControllerObservationSources &input,
                               const KnowledgeRecord &objective)
{
	using state_tree::Value;
	const auto field = [&](const char *name) -> const KnowledgeValue *
	{
		const auto found = objective.value.fields.find(input.definitions.field(objective.value.schema, name));
		return found == objective.value.fields.end() ? nullptr : &found->second;
	};
	const auto text = [&](const char *name)
	{
		const auto value = field(name);
		return value && std::get_if<std::string>(value) ? std::get<std::string>(*value) : std::string{};
	};
	const auto number = [&](const char *name)
	{
		const auto value = field(name);
		return value && std::get_if<std::int64_t>(value) ? std::get<std::int64_t>(*value) : std::int64_t{};
	};
	const auto source = text("delivery_source"), destination = text("delivery_store"),
	           material = text("delivery_material");
	const auto target = number("delivery_target"), quantity = number("delivery_quantity");
	const auto board = input.observation.gameplay.find("logistics");
	const Json *store = nullptr, *offer = nullptr;
	std::int64_t revision = 0, available = 0;
	bool pending = false, complete = false;
	if (board != input.observation.gameplay.end())
	{
		revision = board->value("revision", std::int64_t{});
		// This departmental feed contains the complete bounded request/batch view.
		// A truncated or unowned feed cannot prove that a new request is useful.
		complete = board->value("controller", std::string{}) == controller.definition.id &&
		           board->at("requests").size() < 256 && board->at("batches").size() < 256;
		for (const auto &row : board->at("stores"))
			if (row.at("id") == source && row.at("controller") == controller.definition.id)
				store = &row;
		for (const auto &row : board->at("offers"))
			if (row.at("entity") == target && row.at("store") == destination && row.at("material") == material)
				offer = &row;
		if (store && store->contains("available"))
			available = store->at("available").value(material, std::int64_t{});
		if (store && offer && store->contains("index") && offer->contains("index"))
			for (const auto *rows : {"requests", "batches"})
				for (const auto &row : board->at(rows))
					pending |= row.at("source") == input.entity && row.at("destination") == target &&
					           row.at("from") == store->at("index") && row.at("to") == offer->at("index") &&
					           row.at("material") == material;
	}
	const auto equipment = store ? store->value("system", std::string{}) : std::string{};
	const bool delegated = controller_operation_owns(controller, "logistics", equipment) &&
	                       !controller_operation_human(controller, "logistics");
	std::string reason = "permitted_delivery_request";
	if (source.empty() || destination.empty() || material.empty() || target <= 0 || target == input.entity ||
	    quantity <= 0 || quantity > 1000000000 || !text("mission").empty() || !text("compartment").empty() ||
	    (!text("system").empty() && text("system") != source) || number("target") && number("target") != target)
		reason = "no_scoped_delivery_instruction";
	else if (!delegated)
		reason = "delivery_not_delegated";
	else if (!complete || !store || !store->contains("index") || !offer || !offer->contains("index"))
		reason = "no_complete_delivery_observation";
	else if (pending)
		reason = "delivery_already_in_progress";
	else if (!offer->value("connected", false))
		reason = "delivery_requires_connection";
	else if (!store->value("working", false) || store->value("held", true))
		reason = "delivery_source_unavailable";
	else if (available <= 0)
		reason = "no_observed_delivery_supplies";
	const auto transfer = std::to_string(target) + "/" + destination + "/" + material + "/" + std::to_string(quantity);
	return {{{"delivery_candidate.eligible", Value::boolean(reason == "permitted_delivery_request")},
	         {"delivery_candidate.pending", Value::boolean(pending)},
	         {"delivery_candidate.reason", Value::string(reason)},
	         {"delivery_candidate.source", Value::string(source)},
	         {"delivery_candidate.destination", Value::string(destination)},
	         {"delivery_candidate.material", Value::string(material)},
	         {"delivery_candidate.transfer", Value::string(transfer)},
	         {"delivery_candidate.target", Value::integer(target)},
	         {"delivery_candidate.quantity", Value::integer(quantity)},
	         {"delivery_candidate.available", Value::integer(available)},
	         {"delivery_candidate.revision", Value::integer(revision)}}};
}
CandidateAdapter delivery_adapter()
{
	return {"delivery",
	        "objective_parameter_outside_addressed_request_scope",
	        false,
	        [](const CandidateCache &cache, const ConsoleControllerRuntime &controller,
	           const ControllerObservationSources &input, const ControllerObjectiveBinding &binding,
	           const KnowledgeRecord &record,
	           std::span<const state_tree::Value> source) -> std::vector<DecisionCandidateFrame>
	        {
		        return candidate_frames(controller_delivery_candidates(controller, input, record), controller, source);
	        },
	        [](CandidateContext &context)
	        {
		        return true;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
		        if (context.binding.candidate_source == "delivery" && candidate.reason == "binding_not_applicable" &&
		            !context.fact("delivery_candidate.eligible").number)
			        candidate.reason = context.fact("delivery_candidate.reason").text;
		        if (context.binding.candidate_source == "delivery")
			        candidate.target = context.fact("delivery_candidate.target").number;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
		        if (context.binding.candidate_source == "delivery" &&
		            !context.fact("delivery_candidate.eligible").number)
			        candidate.reason = context.fact("delivery_candidate.reason").text;
	        },
	        [](CandidateContext &context) -> Json
	        {
		        if (context.binding.candidate_source == "delivery")
		        {
			        Json evidence;
			        for (const auto *name : {"eligible", "pending", "reason", "source", "destination", "material",
			                                 "target", "quantity", "available", "revision"})
			        {
				        const auto &value = context.fact((std::string("delivery_candidate.") + name).c_str());
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
