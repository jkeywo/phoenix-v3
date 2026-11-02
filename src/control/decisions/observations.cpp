module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <map>
#include <limits>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

module phoenix.control.decisions;

namespace phoenix
{

namespace
{
static state_tree::Value controller_property_fact(const DecisionProperties *rows,
                                                  const ControllerAbilityPropertyBinding &property)
{
	if (rows)
		if (const auto found = rows->find(property.property);
		    found != rows->end() && found->second.value.type == property.type)
			return found->second.value;
	return {};
}
} // namespace
void append_controller_execution_facts(EntityId id, const ConsoleControllerRuntime &controller,
                                       const EntityControls &entity_controls, const DecisionObservation &observation,
                                       std::span<const AbilityControlDefinition> ability_controls,
                                       std::span<const ProcessControlDefinition> process_controls,
                                       std::vector<state_tree::Value> &values)
{
	using state_tree::Value;
	const ConsoleAddress address{id, controller.definition.console};
	const auto &brain = *controller.brain;
	const auto &controllers = entity_controls.controllers;
	const auto owner = static_cast<std::size_t>(&controller - controllers.data());
	for (const auto &binding : brain.abilities)
	{
		const auto *endpoint =
		    binding.endpoint_index < ability_controls.size() ? &ability_controls[binding.endpoint_index] : nullptr;
		if (!endpoint || controllers.at(endpoint->controller).definition.console != address.console ||
		    (endpoint->controller != owner && (!endpoint->planner || *endpoint->planner != owner)))
			throw std::runtime_error("Controller ability observation is not owned by this endpoint");
		const auto found = observation.machinery.abilities.find(binding.endpoint);
		const AbilityDecisionFacts missing;
		const auto &fact = found == observation.machinery.abilities.end() ? missing : found->second;
		values[binding.fields[ControllerAbilityField::Phase]] = Value::string(fact.phase);
		values[binding.fields[ControllerAbilityField::Ready]] = Value::boolean(fact.ready);
		values[binding.fields[ControllerAbilityField::Preparation]] = Value::integer(fact.preparation);
		values[binding.fields[ControllerAbilityField::Recovery]] = Value::integer(fact.recovery);
		values[binding.fields[ControllerAbilityField::Output]] = Value::integer(fact.output);
		values[binding.fields[ControllerAbilityField::Blocked]] = Value::boolean(fact.blocked);
		values[binding.fields[ControllerAbilityField::Requested]] = Value::boolean(fact.requested);
		for (const auto &property : binding.properties)
			values.at(property.destination) = controller_property_fact(&fact.properties, property);
	}
	for (const auto &binding : brain.processes)
	{
		const auto *endpoint =
		    binding.endpoint_index < process_controls.size() ? &process_controls[binding.endpoint_index] : nullptr;
		if (!endpoint || endpoint->controller != owner || controller.definition.console != address.console)
			throw std::runtime_error("Controller process observation is not owned by this endpoint");
		for (const auto destination : binding.fields.values)
			values.at(destination) = {};
		const auto found = observation.machinery.processes.find(binding.endpoint);
		const ProcessDecisionFacts *fact = found == observation.machinery.processes.end() ? nullptr : &found->second;
		if (fact)
		{
			using F = ControllerProcessField;
			values[binding.fields[F::Phase]] = Value::string(fact->phase);
			values[binding.fields[F::Requested]] = Value::boolean(fact->requested);
			values[binding.fields[F::Running]] = Value::boolean(fact->running);
			values[binding.fields[F::Paused]] = Value::boolean(fact->paused);
			values[binding.fields[F::Blocked]] = Value::boolean(fact->blocked);
			values[binding.fields[F::Finite]] = Value::boolean(fact->finite);
			values[binding.fields[F::Driver]] = Value::boolean(fact->driver);
			if (fact->activity)
				values[binding.fields[F::Activity]] = Value::integer(*fact->activity);
			if (fact->work)
				values[binding.fields[F::Work]] = Value::integer(*fact->work);
			if (fact->target)
				values[binding.fields[F::Target]] = Value::string(*fact->target);
			if (fact->progress)
				values[binding.fields[F::Progress]] = Value::integer(*fact->progress);
			if (fact->required_work)
				values[binding.fields[F::RequiredWork]] = Value::integer(*fact->required_work);
			if (fact->completed)
				values[binding.fields[F::Completed]] = Value::boolean(*fact->completed);
			if (fact->ready)
				values[binding.fields[F::Ready]] = Value::boolean(*fact->ready);
			if (fact->completion_delivered)
				values[binding.fields[F::CompletionDelivered]] = Value::boolean(*fact->completion_delivered);
		}
		for (const auto &property : binding.properties)
			values.at(property.destination) = controller_property_fact(fact ? &fact->properties : nullptr, property);
	}
}
void append_controller_docking_facts(const ConsoleControllerRuntime &, const ControllerObservationSources &,
                                     std::vector<state_tree::Value> &);
void append_controller_observations(const ConsoleControllerRuntime &controller,
                                    const ControllerObservationSources &input, std::vector<state_tree::Value> &values)
{
	using state_tree::Type;
	using state_tree::Value;
	const auto id = input.entity;
	append_controller_docking_facts(controller, input, values);
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
		}
		std::set<std::tuple<KnowledgeId, KnowledgeId, KnowledgeId>> seen;
		for (auto record = records.rbegin(); record != records.rend(); ++record)
			if (record->value.schema == binding.schema && record->value.subject == subject &&
			    seen.emplace(record->value.provenance.source, record->publication, record->recipient_actor).second)
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
			if (system.id == binding.system && brain.telemetry_reads[i].field &&
			    system.telemetry.contains(*brain.telemetry_reads[i].field))
			{
				const auto number = system.telemetry.at(*brain.telemetry_reads[i].field);
				values[brain.telemetry_reads[i].destination] =
				    binding.type == Type::Boolean ? Value::boolean(number != 0) : Value::integer(number);
				break;
			}
	}
	for (const auto &read : brain.command_reads)
	{
		const auto &source = input.controls.controllers.at(read.controller);
		const auto &state = input.frame.commands().contains(id)
		                        ? input.frame.commands().at(id).at(read.controller).at(read.channel)
		                        : source.commands.at(read.channel);
		using F = ControllerCommandReadField;
		auto &value = values.at(read.destination);
		switch (read.source.field)
		{
		case F::Value:
			value = source.definition.commands.at(read.channel).type == KnowledgeType::Boolean
			            ? Value::boolean(state.value != 0)
			            : Value::integer(state.value);
			break;
		case F::Accepted:
			value = Value::boolean(state.accepted);
			break;
		case F::Revision:
			value = Value::integer(state.revision);
			break;
		case F::Consumed:
			value = Value::integer(state.consumed);
			break;
		case F::RequestId:
			value = Value::integer(state.request_id);
			break;
		case F::Pending:
			value = Value::boolean(state.consumed < state.revision);
			break;
		case F::Argument:
			value = Value::integer(state.arguments.at(read.source.argument));
			break;
		}
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
		values.at(fields[ControllerRecordField::Id]) = Value::integer(record->id);
		values.at(fields[ControllerRecordField::Age]) = Value::integer(
		    input.tick >= record->value.provenance.observed_tick ? input.tick - record->value.provenance.observed_tick
		                                                         : 0);
		values.at(fields[ControllerRecordField::ExpiresTick]) = Value::integer(std::min<KnowledgeTick>(
		    record->expires_tick, static_cast<KnowledgeTick>(std::numeric_limits<std::int64_t>::max())));
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
