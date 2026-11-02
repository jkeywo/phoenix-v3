module;
#include <optional>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <span>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

module phoenix.control.decisions;
import phoenix.knowledge.decision_combat;
import phoenix.control.candidate_facts;
import phoenix.knowledge;

namespace phoenix
{
#include "candidate_adapters.hpp"
std::vector<CandidateFacts> controller_contact_candidates(const ConsoleControllerRuntime &controller,
                                                          const ControllerObservationSources &input)
{
	using state_tree::Value;
	using Facts = CandidateFacts;
	struct Contact
	{
		BodyView body;
		KnowledgeTick observed = 0, identity = 0, revision = 0;
		std::int64_t uncertainty = 0;
		KnowledgeId source = 0, publication = 0, recipient = 0;
		bool current = false;
	};
	std::map<EntityId, Contact> contacts;
	for (const auto &body : input.observation.contacts)
		if (body.id != input.entity)
			contacts[body.id] = {body, input.tick, input.tick, 0, 0, 0, 0, 0, true};
	{
		KnowledgeStreamSelection streams;
		for (const auto *records : {&input.board.records, &input.board.shared})
			for (auto row = records->rbegin(); row != records->rend(); ++row)
			{
				const auto &p = row->value.provenance;
				const auto *payload = input.board.decision_payload(*row);
				const auto *report = payload ? std::get_if<ContactReportDecision>(payload) : nullptr;
				if (!report || !streams.take_latest(*row))
					continue;
				if (!report->valid || row->value.withdrawn || p.kind != EvidenceKind::Fact ||
				    p.observed_tick > input.tick || input.stale(*row, input.tick) || row->value.subject == input.entity)
					continue;
				const auto previous = contacts.find(row->value.subject);
				if (previous != contacts.end() && previous->second.current)
					continue;
				Contact contact;
				contact.body.id = row->value.subject;
				contact.observed = p.observed_tick;
				contact.revision = p.revision;
				contact.uncertainty = p.uncertainty;
				contact.source = p.source;
				contact.publication = row->publication;
				contact.recipient = row->recipient_actor;
				contact.body = report->body;
				contact.identity = report->identity_tick;
				const auto quality = [](const Contact &value)
				{
					return knowledge_evidence_quality(value.observed, value.revision, value.uncertainty);
				};
				const auto identity = [](const Contact &value)
				{
					return std::tuple{value.source, value.publication, value.recipient};
				};
				if (previous == contacts.end() || quality(contact) > quality(previous->second) ||
				    (quality(contact) == quality(previous->second) && identity(contact) < identity(previous->second)))
					contacts[contact.body.id] = std::move(contact);
			}
	}
	std::set<EntityId> accepted;
	const auto owner = static_cast<std::size_t>(&controller - input.controls.controllers.data());
	for (std::size_t i = 0; i < controller.definition.commands.size(); ++i)
	{
		const auto action = controller.definition.commands[i].action;
		if (action == OperatingAction::WeaponTarget || action == OperatingAction::DirectedTarget ||
		    action == OperatingAction::FollowContact || action == OperatingAction::SelectContact)
		{
			const auto &state = input.frame.commands().contains(input.entity)
			                        ? input.frame.commands().at(input.entity).at(owner).at(i)
			                        : controller.commands[i];
			if (state.accepted && state.value > 0)
				accepted.insert(static_cast<EntityId>(state.value));
		}
	}
	std::vector<Facts> result;
	for (const auto &[id, contact] : contacts)
	{
		Facts facts;
		const auto integer = [&](CandidateField field, std::int64_t value)
		{
			facts[field] = Value::integer(value);
		};
		const auto boolean = [&](CandidateField field, bool value)
		{
			facts[field] = Value::boolean(value);
		};
		const auto text = [&](CandidateField field, const std::string &value)
		{
			facts[field] = Value::string(value);
		};
		integer(CandidateField::contact_candidate_id, static_cast<std::int64_t>(id));
		integer(CandidateField::contact_candidate_x, contact.body.x);
		integer(CandidateField::contact_candidate_y, contact.body.y);
		integer(CandidateField::contact_candidate_vx, contact.body.vx);
		integer(CandidateField::contact_candidate_vy, contact.body.vy);
		integer(CandidateField::contact_candidate_heading, contact.body.heading);
		integer(CandidateField::contact_candidate_altitude, contact.body.altitude);
		const fixed::Vec delta{contact.body.x - input.observation.own.x, contact.body.y - input.observation.own.y};
		integer(CandidateField::contact_candidate_distance, fixed::length(delta));
		integer(CandidateField::contact_candidate_bearing, fixed::bearing(delta));
		integer(CandidateField::contact_candidate_away_bearing, fixed::bearing({-delta.x, -delta.y}));
		// Geometry is a calculation over this Controller's frozen evidence. The
		// authored policy decides whether a track or an accepted-order boundary matters.
		const auto relative =
		    fixed::Vec{contact.body.vx - input.observation.own.vx, contact.body.vy - input.observation.own.vy};
		const auto speed = fixed::length(relative);
		const auto direction = fixed::normal(relative);
		const auto closing = -fixed::dot(delta, direction) / fixed::unit;
		boolean(CandidateField::contact_candidate_closing, speed > 0 && closing > 0);
		integer(CandidateField::contact_candidate_closest_approach,
		        fixed::length(delta + fixed::scale(direction, std::max<std::int64_t>(0, closing))) +
		            std::abs(contact.body.altitude - input.observation.own.altitude));
		integer(CandidateField::contact_candidate_time_to_approach,
		        speed > 0 ? std::max<std::int64_t>(0, closing) * 1000 / speed : 0);
		const auto &craft = input.observation.craft_self;
		integer(CandidateField::contact_candidate_order_distance,
		        craft ? fixed::length({contact.body.x - craft->anchor_x, contact.body.y - craft->anchor_y}) : 0);
		integer(CandidateField::contact_candidate_order_range, craft ? craft->engagement_range : 0);
		integer(CandidateField::contact_candidate_age, static_cast<std::int64_t>(input.tick - contact.observed));
		integer(CandidateField::contact_candidate_identity_age,
		        static_cast<std::int64_t>(input.tick >= contact.identity ? input.tick - contact.identity : 0));
		integer(CandidateField::contact_candidate_observed_tick, static_cast<std::int64_t>(contact.observed));
		integer(CandidateField::contact_candidate_revision, static_cast<std::int64_t>(contact.revision));
		integer(CandidateField::contact_candidate_uncertainty, contact.uncertainty);
		boolean(CandidateField::contact_candidate_available, true);
		boolean(CandidateField::contact_candidate_fire_allowed, true);
		boolean(CandidateField::contact_candidate_current, contact.current);
		boolean(CandidateField::contact_candidate_hostile, contact.body.disposition == "hostile");
		boolean(CandidateField::contact_candidate_continuing, accepted.contains(id));
		text(CandidateField::contact_candidate_source, contact.current ? "console" : std::to_string(contact.source));
		text(CandidateField::contact_candidate_classification, contact.body.definition);
		text(CandidateField::contact_candidate_category, id >= 7000000000000000ULL && id < 8000000000000000ULL
		                                                     ? "missile"
		                                                     : std::string(radar_icon_name(contact.body.radar_icon)));
		text(CandidateField::contact_candidate_disposition, contact.body.disposition);
		text(CandidateField::contact_candidate_reason,
		     contact.current ? "observed_contact" : "retained_contact_report");
		result.push_back(std::move(facts));
	}
	if (result.empty())
	{
		auto empty = empty_candidate_facts(controller, CandidateGroup::contact_candidate);
		empty[CandidateField::contact_candidate_reason] = Value::string("no_permitted_contact");
		result.push_back(std::move(empty));
	}
	return result;
}
CandidateAdapter contacts_adapter()
{
	return {"contacts",
	        "objective_parameter_outside_known_contact_scope",
	        true,
	        [](const CandidateCache &cache, const ConsoleControllerRuntime &controller,
	           const ControllerObservationSources &input, const ControllerObjectiveBinding &binding,
	           const KnowledgeRecord &record,
	           std::span<const state_tree::Value> source) -> std::vector<DecisionCandidateFrame>
	        {
		        return candidate_frames(cache.contact_options(), controller, source);
	        },
	        [](CandidateContext &context)
	        {
		        if ((context.source() == CandidateSource::contacts || context.source() == CandidateSource::armament) &&
		            (!context.text(*context.record, ObjectiveField::mission, "").empty() ||
		             !context.text(*context.record, ObjectiveField::compartment, "").empty() ||
		             (!context.text(*context.record, ObjectiveField::system, "").empty() &&
		              (context.source() != CandidateSource::armament ||
		               context.text(*context.record, ObjectiveField::system, "") !=
		                   context.fact(CandidateField::armament_weapon).text)) ||
		             (context.number(*context.record, ObjectiveField::target) &&
		              context.number(*context.record, ObjectiveField::target) !=
		                  context.fact(CandidateField::contact_candidate_id).number)))
			        return false;
		        return true;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
		        if ((context.source() == CandidateSource::contacts || context.source() == CandidateSource::armament) &&
		            candidate.reason == "binding_not_applicable")
		        {
			        if (context.source() == CandidateSource::armament &&
			            context.fact(CandidateField::armament_weapon).text.empty())
				        candidate.reason = "no_owned_ammunition_options";
			        else if (!context.fact(CandidateField::contact_candidate_available).number)
				        candidate.reason = "no_permitted_contact";
			        else if (context.behavior == "engage" &&
			                 !context.fact(CandidateField::contact_candidate_hostile).number &&
			                 !(context.fact(CandidateField::contact_candidate_category).text == "missile" &&
			                   context.fact(CandidateField::contact_candidate_disposition).text != "friendly"))
				        candidate.reason = "no_known_hostile";
		        }
		        if (context.source() == CandidateSource::contacts || context.source() == CandidateSource::armament)
			        candidate.target = context.fact(CandidateField::contact_candidate_id).number;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
	        },
	        [](CandidateContext &context) -> Json
	        {
		        return {};
	        }};
}
} // namespace phoenix
