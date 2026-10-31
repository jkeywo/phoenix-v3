module;
#include <algorithm>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

module phoenix.control.execution;
import phoenix.effects.processes;

namespace phoenix
{
const KnowledgeRecord *controller_navigation_objective(const ControllerObservationSources &input,
                                                       const ConsoleControllerRuntime &controller, Json *diagnostics,
                                                       bool scan)
{
	const auto schema = input.definitions.schema("phoenix.objective");
	const auto field = [&](const KnowledgeRecord &record, const char *name) -> const KnowledgeValue *
	{
		const auto at = record.value.fields.find(input.definitions.field(schema, name));
		return at == record.value.fields.end() ? nullptr : &at->second;
	};
	const auto text = [&](const KnowledgeRecord &record, const char *name, const std::string &fallback)
	{
		const auto value = field(record, name);
		return value && std::get_if<std::string>(value) ? std::get<std::string>(*value) : fallback;
	};
	const auto number = [&](const KnowledgeRecord &record, const char *name, std::int64_t fallback = 0)
	{
		const auto value = field(record, name);
		return value && std::get_if<std::int64_t>(value) ? std::get<std::int64_t>(*value) : fallback;
	};
	std::set<KnowledgeId> seen;
	std::vector<const KnowledgeRecord *> records;
	std::vector<ObjectiveCandidate> candidates;
	for (auto r = input.board.shared.rbegin(); r != input.board.shared.rend(); ++r)
		if (r->value.schema == schema && seen.insert(r->value.subject).second)
		{
			ObjectiveCandidate candidate;
			candidate.objective = r->value.subject;
			candidate.target = number(*r, "target");
			candidate.action = text(*r, "behavior", "navigate");
			candidate.tier = objective_tier(text(*r, "tier", "objective_other"));
			candidate.marked = input.priority_objectives.contains(candidate.objective);
			const bool actionable =
			    scan ? candidate.action == "scan" && candidate.target &&
			               (std::any_of(input.observation.contacts.begin(), input.observation.contacts.end(),
			                            [&](const auto &contact)
			                            {
				                            return contact.id == candidate.target;
			                            }) ||
			                std::any_of(input.observation.bearing_contacts.begin(),
			                            input.observation.bearing_contacts.end(),
			                            [&](const auto &contact)
			                            {
				                            return contact.id == candidate.target;
			                            }))
			         : field(*r, "destination_x") && field(*r, "destination_y") &&
			               (candidate.action == "navigate" || candidate.action == "deliver" ||
			                candidate.action == "formation" || candidate.action == "withdraw");
			candidate.feasible =
			    !r->value.withdrawn && !input.stale(*r, input.tick) && text(*r, "status", "") == "active" && actionable;
			candidate.reason = candidate.feasible ? "permitted_destination" : "no_actionable_destination";
			candidate.committed =
			    controller.selected_objective == candidate.objective && controller.action.status == "running";
			candidate.factors.urgency = number(*r, "urgency");
			candidate.factors.progress = 1000;
			candidate.factors.travel = fixed::length({number(*r, "destination_x") - input.observation.own.x,
			                                          number(*r, "destination_y") - input.observation.own.y}) /
			                           1000;
			if (controller.brain)
			{
				const auto &policies = controller.brain->objective_scoring;
				const auto policy = policies.find(text(*r, "scoring_policy", "default"));
				if (policy == policies.end())
				{
					candidate.feasible = false;
					candidate.reason = "unsupported_scoring_policy";
				}
				else
				{
					const auto &w = policy->second;
					candidate.policy = {w[0], w[1], w[2], w[3], w[4], w[5]};
				}
			}
			records.push_back(&*r);
			candidates.push_back(std::move(candidate));
		}
	const auto selected = select_objective(candidates);
	if (diagnostics)
	{
		*diagnostics = Json::array();
		for (std::size_t i = 0; i < candidates.size(); ++i)
		{
			const auto &c = candidates[i];
			const auto score = score_objective(c);
			diagnostics->push_back({{"objective", c.objective},
			                        {"target", c.target},
			                        {"action", c.action},
			                        {"tier", objective_tier_name(c.tier)},
			                        {"marked", c.marked},
			                        {"feasible", c.feasible},
			                        {"reason", c.reason},
			                        {"score", score.total},
			                        {"terms", score.terms},
			                        {"selected", selected && *selected == i}});
		}
	}
	return selected ? records[*selected] : nullptr;
}

namespace
{
static state_tree::Value controller_property_fact(const Json *rows, const ControllerAbilityPropertyBinding &property)
{
	if (rows)
		for (const auto &row : *rows)
			if (row.at("id") == property.property)
			{
				const auto &value = row.at("value");
				if (property.type == state_tree::Type::Boolean && value.is_boolean())
					return state_tree::Value::boolean(value.get<bool>());
				if (property.type == state_tree::Type::Text && value.is_string())
					return state_tree::Value::string(value.get<std::string>());
				if (property.type == state_tree::Type::Integer && value.is_number_integer())
					return state_tree::Value::integer(value.get<std::int64_t>());
				break;
			}
	return {};
}
} // namespace
void append_controller_execution_facts(EntityId id, const ConsoleControllerRuntime &controller,
                                       const EntityControls &entity_controls, const Observation &observation,
                                       std::span<const AbilityControlDefinition> ability_controls,
                                       std::span<const ProcessControlDefinition> process_controls,
                                       std::vector<state_tree::Value> &values)
{
	using state_tree::Value;
	const ConsoleAddress address{id, controller.definition.console};
	const auto &brain = *controller.brain;
	for (const auto &binding : brain.abilities)
	{
		const auto endpoint = std::find_if(ability_controls.begin(), ability_controls.end(),
		                                   [&](const auto &control)
		                                   {
			                                   return control.id == binding.endpoint;
		                                   });
		const auto &controllers = entity_controls.controllers;
		const auto owner = static_cast<std::size_t>(&controller - controllers.data());
		if (endpoint == ability_controls.end() ||
		    controllers.at(endpoint->controller).definition.console != address.console ||
		    (endpoint->controller != owner && (!endpoint->planner || *endpoint->planner != owner)))
			throw std::runtime_error("Controller ability observation is not owned by this endpoint");
		std::string phase = "unavailable";
		bool ready = false, blocked = true, requested = false;
		std::int64_t preparation = 0, recovery = 0, output = 0;
		const auto facts = observation.gameplay.find("ability_facts");
		if (facts != observation.gameplay.end() && facts->contains(binding.endpoint))
		{
			const auto &fact = facts->at(binding.endpoint);
			phase = fact.at("phase");
			ready = fact.at("ready");
			blocked = fact.at("blocked");
			requested = fact.at("requested");
			preparation = fact.at("preparation");
			recovery = fact.at("recovery");
			output = fact.at("output");
		}
		else if (const auto rows = observation.gameplay.find("abilities"); rows != observation.gameplay.end())
		{
			// Explicit debug/model requests can supply a full captured Console
			// projection. Extract its existing facts; never read live ability state.
			for (const auto &row : *rows)
				if (row.at("control") == binding.endpoint)
				{
					phase = row.at("phase");
					ready = phase == "ready";
					blocked = !row.at("blocker").get<std::string>().empty();
					requested = row.at("requested");
					preparation = row.at("progress");
					const auto deadline = row.at("ready").get<std::uint64_t>();
					recovery = deadline > observation.observed_tick ? deadline - observation.observed_tick : 0;
					output = row.at("output");
					break;
				}
		}
		values[binding.fields[ControllerAbilityField::Phase]] = Value::string(phase);
		values[binding.fields[ControllerAbilityField::Ready]] = Value::boolean(ready);
		values[binding.fields[ControllerAbilityField::Preparation]] = Value::integer(preparation);
		values[binding.fields[ControllerAbilityField::Recovery]] = Value::integer(recovery);
		values[binding.fields[ControllerAbilityField::Output]] = Value::integer(output);
		values[binding.fields[ControllerAbilityField::Blocked]] = Value::boolean(blocked);
		values[binding.fields[ControllerAbilityField::Requested]] = Value::boolean(requested);
		for (const auto &property : binding.properties)
		{
			values.at(property.destination) = {};
			const Json *rows = nullptr;
			if (facts != observation.gameplay.end() && facts->contains(binding.endpoint))
			{
				const auto &fact = facts->at(binding.endpoint);
				if (fact.contains("properties"))
					rows = &fact.at("properties");
			}
			else if (const auto board = observation.gameplay.find("abilities"); board != observation.gameplay.end())
				for (const auto &row : *board)
					if (row.at("control") == binding.endpoint && row.contains("properties"))
					{
						rows = &row.at("properties");
						break;
					}
			values.at(property.destination) = controller_property_fact(rows, property);
		}
	}
	for (const auto &binding : brain.processes)
	{
		const auto &controls = process_controls;
		const auto endpoint = std::find_if(controls.begin(), controls.end(),
		                                   [&](const auto &candidate)
		                                   {
			                                   return candidate.id == binding.endpoint;
		                                   });
		const auto &controllers = entity_controls.controllers;
		const auto owner = static_cast<std::size_t>(&controller - controllers.data());
		if (endpoint == controls.end() || endpoint->controller != owner ||
		    controller.definition.console != address.console)
			throw std::runtime_error("Controller process observation is not owned by this endpoint");
		Json fallback;
		const Json *fact = nullptr;
		if (const auto facts = observation.gameplay.find("process_facts");
		    facts != observation.gameplay.end() && facts->contains(binding.endpoint))
			fact = &facts->at(binding.endpoint);
		else if (const auto rows = observation.gameplay.find("processes"); rows != observation.gameplay.end())
			for (const auto &row : *rows)
				if (row.at("control") == binding.endpoint)
				{
					fallback = process_row_decision_facts(row);
					fact = &fallback;
					break;
				}
		for (const auto &[field, name] : std::array<std::pair<ControllerProcessField, const char *>, 15>{
		         {{ControllerProcessField::Phase, "phase"},
		          {ControllerProcessField::Requested, "requested"},
		          {ControllerProcessField::Running, "running"},
		          {ControllerProcessField::Paused, "paused"},
		          {ControllerProcessField::Blocked, "blocked"},
		          {ControllerProcessField::Activity, "activity"},
		          {ControllerProcessField::Work, "work"},
		          {ControllerProcessField::Finite, "finite"},
		          {ControllerProcessField::Driver, "driver"},
		          {ControllerProcessField::Target, "target"},
		          {ControllerProcessField::Progress, "progress"},
		          {ControllerProcessField::RequiredWork, "required_work"},
		          {ControllerProcessField::Completed, "completed"},
		          {ControllerProcessField::Ready, "ready"},
		          {ControllerProcessField::CompletionDelivered, "completion_delivered"}}})
		{
			auto &value = values.at(binding.fields[field]);
			value = {};
			if (!fact || !fact->contains(name))
				continue;
			if (field == ControllerProcessField::Phase || field == ControllerProcessField::Target)
				value = Value::string(fact->at(name).get<std::string>());
			else if (field == ControllerProcessField::Activity || field == ControllerProcessField::Work ||
			         field == ControllerProcessField::Progress || field == ControllerProcessField::RequiredWork)
				value = Value::integer(fact->at(name));
			else
				value = Value::boolean(fact->at(name).get<bool>());
		}
		for (const auto &property : binding.properties)
			values.at(property.destination) = controller_property_fact(
			    fact && fact->contains("properties") ? &fact->at("properties") : nullptr, property);
	}
}
void append_controller_observations(const ConsoleControllerRuntime &controller,
                                    const ControllerObservationSources &input, std::vector<state_tree::Value> &values)
{
	using state_tree::Type;
	using state_tree::Value;
	const auto id = input.entity;
	const auto &brain = *controller.brain;
	const auto &observation = input.observation;
	const auto &board = input.board;
	const auto inbox = input.inbox;
	for (std::size_t i = 0; i < brain.commands.size(); ++i)
	{
		const auto field = brain.commands[i].channel_index;
		const auto &controllers = input.controls.controllers;
		const auto owner = static_cast<std::size_t>(&controller - controllers.data());
		const auto amount = input.frame.commands().contains(id)
		                        ? input.frame.commands().at(id).at(owner).at(field).value
		                        : controller.commands[field].value;
		values[brain.commands[i].destination] = controller.definition.commands[field].type == KnowledgeType::Boolean
		                                            ? Value::boolean(amount != 0)
		                                            : Value::integer(amount);
	}
	std::vector<const KnowledgeRecord *> evidence(brain.reads.size(), nullptr);
	for (std::size_t i = 0; i < brain.reads.size(); ++i)
	{
		const auto &binding = brain.reads[i];
		const auto &records = binding.shared ? board.shared : board.records;
		KnowledgeId subject = binding.subject ? binding.subject : id;
		if (binding.latest)
		{
			const auto latest = std::find_if(records.rbegin(), records.rend(),
			                                 [&](const auto &record)
			                                 {
				                                 return record.value.schema == binding.schema;
			                                 });
			subject = latest == records.rend() ? 0 : latest->value.subject;
			if (binding.priority)
			{
				const auto *objective = controller_navigation_objective(input, controller);
				subject = objective ? objective->value.subject : 0;
			}
		}
		std::set<std::pair<KnowledgeId, KnowledgeId>> seen;
		for (auto record = records.rbegin(); record != records.rend(); ++record)
			if (record->value.schema == binding.schema && record->value.subject == subject &&
			    seen.emplace(record->value.provenance.source, record->publication).second)
			{
				if (!evidence[i])
					evidence[i] = &*record;
				if (record->value.withdrawn || input.stale(*record, input.tick) ||
				    !record->value.fields.contains(binding.field))
					continue;
				evidence[i] = &*record;
				const auto &value = record->value.fields.at(binding.field);
				auto &target = values[binding.destination];
				if (auto n = std::get_if<std::int64_t>(&value))
					target = Value::integer(*n);
				else if (auto b = std::get_if<bool>(&value))
					target = Value::boolean(*b);
				else
					target = Value::string(std::get<std::string>(value));
				break;
			}
	}
	for (std::size_t i = 0; i < brain.telemetry_reads.size(); ++i)
	{
		const auto &binding = brain.telemetry_reads[i].source;
		for (const auto &system : observation.systems)
			if (system.id == binding.system && system.telemetry.contains(binding.field))
			{
				const auto number = system.telemetry.at(binding.field);
				values[brain.telemetry_reads[i].destination] =
				    binding.type == Type::Boolean ? Value::boolean(number != 0) : Value::integer(number);
				break;
			}
	}
	for (std::size_t i = 0; i < brain.command_reads.size(); ++i)
	{
		const auto &read = brain.command_reads[i];
		const auto target = read.controller, channel = read.channel;
		const auto &source = input.controls.controllers.at(target);
		const auto number = input.frame.commands().contains(id)
		                        ? input.frame.commands().at(id).at(target).at(channel).value
		                        : source.commands.at(channel).value;
		values[read.destination] = source.definition.commands.at(channel).type == KnowledgeType::Boolean
		                               ? Value::boolean(number != 0)
		                               : Value::integer(number);
	}
	values[brain.inbox[ControllerInboxField::Available]] = Value::boolean(!inbox.empty());
	if (!inbox.empty())
	{
		const auto &delivery = inbox.front();
		const auto &message = delivery.message;
		const auto &payload = message.value.payload;
		values[brain.inbox[ControllerInboxField::Id]] = Value::reference(Type::Message, message.id);
		values[brain.inbox[ControllerInboxField::Source]] = Value::integer(message.value.sender_system);
		values[brain.inbox[ControllerInboxField::SourceConsole]] =
		    Value::reference(Type::Console, message.sender_console);
		values[brain.inbox[ControllerInboxField::Schema]] =
		    Value::string(input.definitions.schemas.at(payload.schema - 1).name);
		values[brain.inbox[ControllerInboxField::Subject]] = Value::reference(Type::Entity, payload.subject);
		values[brain.inbox[ControllerInboxField::ObservedTick]] = Value::integer(payload.provenance.observed_tick);
		values[brain.inbox[ControllerInboxField::DeliveredTick]] = Value::integer(delivery.delivered_tick);
		values[brain.inbox[ControllerInboxField::Uncertainty]] = Value::integer(payload.provenance.uncertainty);
		values[brain.inbox[ControllerInboxField::Kind]] =
		    Value::integer(static_cast<unsigned>(payload.provenance.kind));
		values[brain.inbox[ControllerInboxField::Urgency]] = Value::integer(message.value.presentation.urgency);
		values[brain.inbox[ControllerInboxField::RequestKey]] =
		    Value::string(input.request_key_name(message.value.request_key));
		for (std::size_t i = 0; i < brain.message_reads.size(); ++i)
		{
			const auto &binding = brain.message_reads[i];
			if (binding.schema != payload.schema || !payload.fields.contains(binding.field))
				continue;
			const auto &field = payload.fields.at(binding.field);
			auto &target = values[binding.destination];
			if (const auto number = std::get_if<std::int64_t>(&field))
				target = Value::integer(*number);
			else if (const auto flag = std::get_if<bool>(&field))
				target = Value::boolean(*flag);
			else
				target = Value::string(std::get<std::string>(field));
		}
	}
	const auto &controllers = input.controls.controllers;
	const auto owner = static_cast<std::size_t>(&controller - controllers.data());
	for (const auto &binding : brain.commands)
	{
		const auto field = binding.channel_index;
		const auto &state = input.frame.commands().contains(id) ? input.frame.commands().at(id).at(owner).at(field)
		                                                        : controller.commands[field];
		for (std::size_t i = 0; i < binding.arguments.size(); ++i)
			values.at(binding.arguments[i]) = Value::integer(state.arguments.at(i));
	}
	for (std::size_t i = 0; i < evidence.size(); ++i)
	{
		const auto *record = evidence[i];
		const auto &fields = brain.reads[i].metadata;
		values.at(fields[ControllerRecordField::Available]) =
		    Value::boolean(record && !record->value.withdrawn && !input.stale(*record, input.tick) &&
		                   record->value.fields.contains(brain.reads[i].field));
		if (!record)
			continue;
		values.at(fields[ControllerRecordField::ObservedTick]) = Value::integer(record->value.provenance.observed_tick);
		values.at(fields[ControllerRecordField::ReceivedTick]) = Value::integer(record->received_tick);
		values.at(fields[ControllerRecordField::Source]) = Value::integer(record->value.provenance.source);
		values.at(fields[ControllerRecordField::Uncertainty]) = Value::integer(record->value.provenance.uncertainty);
		values.at(fields[ControllerRecordField::Kind]) =
		    Value::integer(static_cast<unsigned>(record->value.provenance.kind));
		values.at(fields[ControllerRecordField::Withdrawn]) = Value::boolean(record->value.withdrawn);
		values.at(fields[ControllerRecordField::Stale]) = Value::boolean(input.stale(*record, input.tick));
		values.at(fields[ControllerRecordField::Revision]) = Value::integer(record->value.provenance.revision);
		values.at(fields[ControllerRecordField::Subject]) = Value::integer(record->value.subject);
	}
	for (const auto &binding : brain.commands)
	{
		const auto field = binding.channel_index;
		const auto &state = input.frame.commands().contains(id) ? input.frame.commands().at(id).at(owner).at(field)
		                                                        : controller.commands[field];
		values.at(binding.metadata[ControllerCommandField::Accepted]) = Value::boolean(state.accepted);
		values.at(binding.metadata[ControllerCommandField::Revision]) = Value::integer(state.revision);
		values.at(binding.metadata[ControllerCommandField::Consumed]) = Value::integer(state.consumed);
		values.at(binding.metadata[ControllerCommandField::RequestId]) = Value::integer(state.request_id);
		values.at(binding.metadata[ControllerCommandField::Pending]) = Value::boolean(state.consumed < state.revision);
	}
	for (const auto &binding : brain.contact_reads)
	{
		const auto target = binding.controller, channel = binding.channel;
		const auto &source = controllers.at(target);
		const auto &state = input.frame.commands().contains(id) ? input.frame.commands().at(id).at(target).at(channel)
		                                                        : source.commands.at(channel);
		const auto selected = source.definition.commands.at(channel).action == OperatingAction::FollowContact
		                          ? state.arguments.at(0)
		                          : state.value;
		const auto contact = std::find_if(observation.contacts.begin(), observation.contacts.end(),
		                                  [&](const auto &candidate)
		                                  {
			                                  return candidate.id == static_cast<EntityId>(selected);
		                                  });
		const auto &fields = binding.fields;
		values.at(fields[ControllerContactField::Available]) = Value::boolean(contact != observation.contacts.end());
		const auto bearing = std::find_if(observation.bearing_contacts.begin(), observation.bearing_contacts.end(),
		                                  [&](const auto &b)
		                                  {
			                                  return b.id == static_cast<EntityId>(selected);
		                                  });
		values.at(fields[ControllerContactField::BearingAvailable]) =
		    Value::boolean(bearing != observation.bearing_contacts.end());
		if (bearing != observation.bearing_contacts.end())
		{
			values.at(fields[ControllerContactField::Id]) = Value::integer(bearing->id);
			values.at(fields[ControllerContactField::BearingMdeg]) = Value::integer(bearing->bearing_mdeg);
			values.at(fields[ControllerContactField::ObservedTick]) = Value::integer(bearing->observed_tick);
		}
		if (contact == observation.contacts.end())
			continue;
		values.at(fields[ControllerContactField::Id]) = Value::integer(contact->id);
		values.at(fields[ControllerContactField::X]) = Value::integer(contact->x);
		values.at(fields[ControllerContactField::Y]) = Value::integer(contact->y);
		values.at(fields[ControllerContactField::Vx]) = Value::integer(contact->vx);
		values.at(fields[ControllerContactField::Vy]) = Value::integer(contact->vy);
		values.at(fields[ControllerContactField::Heading]) = Value::integer(contact->heading);
		values.at(fields[ControllerContactField::Speed]) = Value::integer(fixed::length({contact->vx, contact->vy}));
		values.at(fields[ControllerContactField::ObservedTick]) = Value::integer(observation.observed_tick);
		values.at(fields[ControllerContactField::Uncertainty]) = Value::integer(0);
	}

	const auto &result = controller.result;
	values[brain.result[ControllerResultField::Available]] = Value::boolean(result.available);
	values[brain.result[ControllerResultField::Success]] = Value::boolean(result.success);
	values[brain.result[ControllerResultField::Id]] = Value::integer(result.id);
	values[brain.result[ControllerResultField::Publication]] = Value::integer(result.publication);
	values[brain.result[ControllerResultField::Message]] =
	    result.message ? Value::reference(Type::Message, result.message) : Value{};
	values[brain.result[ControllerResultField::Reason]] = Value::string(result.reason);
	values.at(brain.action_base) = Value::string(controller.action.status);
	values.at(brain.action_base + 1) = Value::string(controller.action.reason);
	values.at(brain.action_base + 2) = Value::string(controller.action.id);
}
} // namespace phoenix
