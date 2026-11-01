module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

module phoenix.control.execution;

namespace phoenix
{
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
		if (body.radar_icon == RadarIcon::ship && body.id != input.entity)
			contacts[body.id] = {body, input.tick, input.tick, 0, 0, 0, 0, 0, true};
	const auto schema = std::find_if(input.definitions.schemas.begin(), input.definitions.schemas.end(),
	                                 [](const auto &definition)
	                                 {
		                                 return definition.name == "phoenix.contact.report";
	                                 });
	if (schema != input.definitions.schemas.end())
	{
		const auto key = input.definitions.schema(schema->name);
		std::set<std::tuple<KnowledgeScope, KnowledgeId, KnowledgeId, KnowledgeId, KnowledgeId, KnowledgeId>> seen;
		for (const auto *records : {&input.board.records, &input.board.shared})
			for (auto row = records->rbegin(); row != records->rend(); ++row)
			{
				const auto &p = row->value.provenance;
				if (row->value.schema != key || !seen.emplace(row->scope, row->owner, row->value.subject, p.source,
				                                              row->publication, row->recipient_actor)
				                                     .second)
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
					return std::tuple{value.observed, value.revision, -static_cast<std::int64_t>(value.uncertainty)};
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
		integer("age", static_cast<std::int64_t>(input.tick - contact.observed));
		integer("identity_age",
		        static_cast<std::int64_t>(input.tick >= contact.identity ? input.tick - contact.identity : 0));
		integer("observed_tick", static_cast<std::int64_t>(contact.observed));
		integer("revision", static_cast<std::int64_t>(contact.revision));
		integer("uncertainty", contact.uncertainty);
		boolean("available", true);
		boolean("current", contact.current);
		boolean("hostile", contact.body.disposition == "hostile");
		boolean("continuing", accepted.contains(id));
		text("source", contact.current ? "console" : std::to_string(contact.source));
		text("classification", contact.body.definition);
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
} // namespace phoenix
