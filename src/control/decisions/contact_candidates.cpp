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
import phoenix.knowledge;

namespace phoenix
{
#include "candidate_adapters.hpp"
std::vector<std::map<std::string, state_tree::Value>>
controller_contact_candidates(const ConsoleControllerRuntime &controller, const ControllerObservationSources &input)
{
	using state_tree::Value;
	using Facts = std::map<std::string, Value>;
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
	const auto schema = std::find_if(input.definitions.schemas.begin(), input.definitions.schemas.end(),
	                                 [](const auto &definition)
	                                 {
		                                 return definition.name == "phoenix.contact.report";
	                                 });
	if (schema != input.definitions.schemas.end())
	{
		const auto key = input.definitions.schema(schema->name);
		KnowledgeStreamSelection streams;
		for (const auto *records : {&input.board.records, &input.board.shared})
			for (auto row = records->rbegin(); row != records->rend(); ++row)
			{
				const auto &p = row->value.provenance;
				if (row->value.schema != key || !streams.take_latest(*row))
					continue;
				if (row->value.withdrawn || p.kind != EvidenceKind::Fact || p.observed_tick > input.tick ||
				    input.stale(*row, input.tick) || row->value.subject == input.entity ||
				    row->value.fields.size() != schema->fields.size())
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
				const auto number = [&](const char *field)
				{
					return std::get<std::int64_t>(row->value.fields.at(input.definitions.field(key, field)));
				};
				const auto category =
				    std::get<std::string>(row->value.fields.at(input.definitions.field(key, "category")));
				const auto icon = radar_icon_named(category);
				if (!icon)
					continue;
				contact.body.radar_icon = *icon;
				contact.body.x = number("x");
				contact.body.y = number("y");
				contact.body.vx = number("vx");
				contact.body.vy = number("vy");
				contact.body.heading = number("heading");
				contact.body.altitude = number("altitude");
				contact.identity = static_cast<KnowledgeTick>(std::max<std::int64_t>(0, number("identity_tick")));
				contact.body.definition =
				    std::get<std::string>(row->value.fields.at(input.definitions.field(key, "classification")));
				contact.body.disposition =
				    std::get<std::string>(row->value.fields.at(input.definitions.field(key, "disposition")));
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
		const auto integer = [&](const char *field, std::int64_t value)
		{
			facts[std::string("contact_candidate.") + field] = Value::integer(value);
		};
		const auto boolean = [&](const char *field, bool value)
		{
			facts[std::string("contact_candidate.") + field] = Value::boolean(value);
		};
		const auto text = [&](const char *field, const std::string &value)
		{
			facts[std::string("contact_candidate.") + field] = Value::string(value);
		};
		integer("id", static_cast<std::int64_t>(id));
		integer("x", contact.body.x);
		integer("y", contact.body.y);
		integer("vx", contact.body.vx);
		integer("vy", contact.body.vy);
		integer("heading", contact.body.heading);
		integer("altitude", contact.body.altitude);
		const fixed::Vec delta{contact.body.x - input.observation.own.x, contact.body.y - input.observation.own.y};
		integer("distance", fixed::length(delta));
		integer("bearing", fixed::bearing(delta));
		integer("away_bearing", fixed::bearing({-delta.x, -delta.y}));
		// Geometry is a calculation over this Controller's frozen evidence. The
		// authored policy decides whether a track or an accepted-order boundary matters.
		const auto relative =
		    fixed::Vec{contact.body.vx - input.observation.own.vx, contact.body.vy - input.observation.own.vy};
		const auto speed = fixed::length(relative);
		const auto direction = fixed::normal(relative);
		const auto closing = -fixed::dot(delta, direction) / fixed::unit;
		boolean("closing", speed > 0 && closing > 0);
		integer("closest_approach", fixed::length(delta + fixed::scale(direction, std::max<std::int64_t>(0, closing))) +
		                                std::abs(contact.body.altitude - input.observation.own.altitude));
		integer("time_to_approach", speed > 0 ? std::max<std::int64_t>(0, closing) * 1000 / speed : 0);
		const auto craft = input.observation.gameplay.find("craft_self");
		integer("order_distance", craft == input.observation.gameplay.end()
		                              ? 0
		                              : fixed::length({contact.body.x - craft->at("anchor_x").get<std::int64_t>(),
		                                               contact.body.y - craft->at("anchor_y").get<std::int64_t>()}));
		integer("order_range",
		        craft == input.observation.gameplay.end() ? 0 : craft->at("engagement_range").get<std::int64_t>());
		integer("age", static_cast<std::int64_t>(input.tick - contact.observed));
		integer("identity_age",
		        static_cast<std::int64_t>(input.tick >= contact.identity ? input.tick - contact.identity : 0));
		integer("observed_tick", static_cast<std::int64_t>(contact.observed));
		integer("revision", static_cast<std::int64_t>(contact.revision));
		integer("uncertainty", contact.uncertainty);
		boolean("available", true);
		boolean("fire_allowed", true);
		boolean("current", contact.current);
		boolean("hostile", contact.body.disposition == "hostile");
		boolean("continuing", accepted.contains(id));
		text("source", contact.current ? "console" : std::to_string(contact.source));
		text("classification", contact.body.definition);
		text("category", id >= 7000000000000000ULL && id < 8000000000000000ULL
		                     ? "missile"
		                     : std::string(radar_icon_name(contact.body.radar_icon)));
		text("disposition", contact.body.disposition);
		text("reason", contact.current ? "observed_contact" : "retained_contact_report");
		result.push_back(std::move(facts));
	}
	if (result.empty())
	{
		Facts empty;
		for (const auto &field : controller.brain->program.registry.observations)
			if (field.name.starts_with("contact_candidate."))
				empty[field.name] = field.type == state_tree::Type::Text      ? Value::string("")
				                    : field.type == state_tree::Type::Boolean ? Value::boolean(false)
				                                                              : Value::integer(0);
		empty["contact_candidate.reason"] = Value::string("no_permitted_contact");
		result.push_back(std::move(empty));
	}
	return result;
}
CandidateAdapter contacts_adapter()
{
	return {
	    "contacts",
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
		    if ((context.binding.candidate_source == "contacts" || context.binding.candidate_source == "armament") &&
		        (!context.text(*context.record, "mission", "").empty() ||
		         !context.text(*context.record, "compartment", "").empty() ||
		         (!context.text(*context.record, "system", "").empty() &&
		          (context.binding.candidate_source != "armament" ||
		           context.text(*context.record, "system", "") != context.fact("armament.weapon").text)) ||
		         (context.number(*context.record, "target") &&
		          context.number(*context.record, "target") != context.fact("contact_candidate.id").number)))
			    return false;
		    return true;
	    },
	    [](CandidateContext &context, ObjectiveCandidate &candidate)
	    {
		    if ((context.binding.candidate_source == "contacts" || context.binding.candidate_source == "armament") &&
		        candidate.reason == "binding_not_applicable")
		    {
			    if (context.binding.candidate_source == "armament" && context.fact("armament.weapon").text.empty())
				    candidate.reason = "no_owned_ammunition_options";
			    else if (!context.fact("contact_candidate.available").number)
				    candidate.reason = "no_permitted_contact";
			    else if (context.behavior == "engage" && !context.fact("contact_candidate.hostile").number &&
			             !(context.fact("contact_candidate.category").text == "missile" &&
			               context.fact("contact_candidate.disposition").text != "friendly"))
				    candidate.reason = "no_known_hostile";
		    }
		    if (context.binding.candidate_source == "contacts" || context.binding.candidate_source == "armament")
			    candidate.target = context.fact("contact_candidate.id").number;
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
