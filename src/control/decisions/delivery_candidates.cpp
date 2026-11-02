module;
#include <optional>
#include <utility>
#include <initializer_list>
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
std::vector<CandidateFacts> controller_delivery_candidates(const ConsoleControllerRuntime &controller,
                                                           const ControllerObservationSources &input,
                                                           const KnowledgeRecord &objective)
{
	using state_tree::Value;
	const auto text = [&](ObjectiveField field)
	{
		return objective_text(controller, input, objective, field);
	};
	const auto number = [&](ObjectiveField field)
	{
		return objective_number(controller, input, objective, field);
	};
	const auto source = text(ObjectiveField::delivery_source), destination = text(ObjectiveField::delivery_store),
	           material = text(ObjectiveField::delivery_material);
	const auto target = number(ObjectiveField::delivery_target), quantity = number(ObjectiveField::delivery_quantity);
	const auto &board = input.observation.logistics;
	const LogisticsStoreDecision *store = nullptr;
	const LogisticsOfferDecision *offer = nullptr;
	const std::int64_t revision = board.observed ? board.revision : 0;
	std::int64_t available = 0;
	bool pending = false;
	// A truncated or unowned feed cannot prove a new request is useful.
	const bool complete = board.observed && board.controller == controller.definition.id &&
	                      board.requests.size() < 256 && board.batches.size() < 256;
	for (const auto &row : board.stores)
		if (row.id == source && row.controller == controller.definition.id)
			store = &row;
	for (const auto &row : board.offers)
		if (row.entity == target && row.store == destination && row.material == material)
			offer = &row;
	if (store)
		if (const auto at = store->available.find(material); at != store->available.end())
			available = at->second;
	if (store && offer)
		for (const auto *rows : {&board.requests, &board.batches})
			for (const auto &row : *rows)
				pending |= row.source == input.entity && row.destination == target && row.from == store->index &&
				           row.to == offer->index && row.material == material;
	const auto equipment = store ? store->system : std::string{};
	const bool delegated = controller_operation_owns(controller, "logistics", equipment) &&
	                       !controller_operation_human(controller, "logistics");
	std::string reason = "permitted_delivery_request";
	if (source.empty() || destination.empty() || material.empty() || target <= 0 || target == input.entity ||
	    quantity <= 0 || quantity > 1000000000 || !text(ObjectiveField::mission).empty() ||
	    !text(ObjectiveField::compartment).empty() ||
	    (!text(ObjectiveField::system).empty() && text(ObjectiveField::system) != source) ||
	    number(ObjectiveField::target) && number(ObjectiveField::target) != target)
		reason = "no_scoped_delivery_instruction";
	else if (!delegated)
		reason = "delivery_not_delegated";
	else if (!complete || !store || !offer)
		reason = "no_complete_delivery_observation";
	else if (pending)
		reason = "delivery_already_in_progress";
	else if (!offer->connected)
		reason = "delivery_requires_connection";
	else if (!store->working || store->held)
		reason = "delivery_source_unavailable";
	else if (available <= 0)
		reason = "no_observed_delivery_supplies";
	const auto transfer = std::to_string(target) + "/" + destination + "/" + material + "/" + std::to_string(quantity);
	return {{{CandidateField::delivery_candidate_eligible, Value::boolean(reason == "permitted_delivery_request")},
	         {CandidateField::delivery_candidate_pending, Value::boolean(pending)},
	         {CandidateField::delivery_candidate_reason, Value::string(reason)},
	         {CandidateField::delivery_candidate_source, Value::string(source)},
	         {CandidateField::delivery_candidate_destination, Value::string(destination)},
	         {CandidateField::delivery_candidate_material, Value::string(material)},
	         {CandidateField::delivery_candidate_transfer, Value::string(transfer)},
	         {CandidateField::delivery_candidate_target, Value::integer(target)},
	         {CandidateField::delivery_candidate_quantity, Value::integer(quantity)},
	         {CandidateField::delivery_candidate_available, Value::integer(available)},
	         {CandidateField::delivery_candidate_revision, Value::integer(revision)}}};
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
		        if (context.source() == CandidateSource::delivery && candidate.reason == "binding_not_applicable" &&
		            !context.fact(CandidateField::delivery_candidate_eligible).number)
			        candidate.reason = context.fact(CandidateField::delivery_candidate_reason).text;
		        if (context.source() == CandidateSource::delivery)
			        candidate.target = context.fact(CandidateField::delivery_candidate_target).number;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
		        if (context.source() == CandidateSource::delivery &&
		            !context.fact(CandidateField::delivery_candidate_eligible).number)
			        candidate.reason = context.fact(CandidateField::delivery_candidate_reason).text;
	        },
	        [](CandidateContext &context) -> Json
	        {
		        if (context.source() == CandidateSource::delivery)
		        {
			        Json evidence;
			        for (const auto &[name, field] : std::initializer_list<std::pair<const char *, CandidateField>>{
			                 {"eligible", CandidateField::delivery_candidate_eligible},
			                 {"pending", CandidateField::delivery_candidate_pending},
			                 {"reason", CandidateField::delivery_candidate_reason},
			                 {"source", CandidateField::delivery_candidate_source},
			                 {"destination", CandidateField::delivery_candidate_destination},
			                 {"material", CandidateField::delivery_candidate_material},
			                 {"target", CandidateField::delivery_candidate_target},
			                 {"quantity", CandidateField::delivery_candidate_quantity},
			                 {"available", CandidateField::delivery_candidate_available},
			                 {"revision", CandidateField::delivery_candidate_revision}})
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
