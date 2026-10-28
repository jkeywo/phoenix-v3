module;
#include "native_headers.hpp"

module phoenix.simulation;
import :scenario_bindings;

namespace phoenix::world_detail
{
auto Runtime::scenario_spawns() -> void
{
	if (!scenario)
		return;
	for (const auto &request : scenario->pending_spawns())
	{
		if (next_entity < 1 || next_entity > 1000000000ULL || request.entities.size() > 1000000001ULL - next_entity)
		{
			scenario->confirm_spawn(request.id, {}, "entity identity space exhausted");
			continue;
		}
		const auto allocator = next_entity;
		const auto previous_instances = instance_systems;
		const auto previous_stations = explicit_stations;
		auto previous = knowledge;
		const auto previous_participants = conversations.participants;
		std::vector<EntityId> ids;
		try
		{
			for (const auto &p : request.entities)
				if (!definitions.contains(p.definition))
					throw std::runtime_error("Uncompiled dynamic entity");
			for (const auto &p : request.entities)
			{
				const auto id = next_entity++;
				ids.push_back(id);
				create(id, p.definition, {{"x_mm", p.position.x}, {"y_mm", p.position.y}, {"heading_mdeg", p.heading}});
			}
			const auto parents = ids;
			initialise_craft(&ids);
			scenario->confirm_spawn(request.id, parents);
		}
		catch (const std::exception &error)
		{
			for (const auto id : ids)
			{
				if (entities.contains(id))
				{
					entities.at(id).destruct();
					entities.erase(id);
				}
			}
			knowledge = std::move(previous);
			conversations.participants = previous_participants;
			next_entity = allocator;
			instance_systems = previous_instances;
			explicit_stations = previous_stations;
			scenario->confirm_spawn(request.id, {}, error.what());
		}
	}
}
auto Runtime::scenario_publications() -> void
{
	if (!scenario)
		return;
	auto &runtime = scenario->runtime();
	const auto &definition = runtime.definition();
	for (const auto &publication : runtime.pending_publications())
	{
		const auto ship = runtime.resolve_actor(publication.instance, publication.recipients.ship);
		if (!ship || !entities.contains(*ship))
			continue;
		if (publication.objective)
		{
			const auto &objective = runtime.objectives().at(publication.objective);
			const auto &spec = definition.objectives.at(objective.definition);
			const ScenarioRegion region = spec.condition ? ScenarioRegion{} : definition.regions.at(spec.region);
			const auto &schema = knowledge.definition();
			const auto key = schema.schema("phoenix.objective");
			KnowledgeRecordInput value;
			value.schema = key;
			value.producer = schema.role("phoenix.scenario");
			value.subject = objective.id;
			value.provenance = {publication.instance, publication.tick, publication.id, 0, EvidenceKind::Fact};
			const char *names[] = {"pending", "active", "completed", "failed", "cancelled"};
			value.fields = {
			    {schema.field(key, "id"), static_cast<std::int64_t>(objective.id)},
			    {schema.field(key, "status"), std::string(names[static_cast<unsigned>(publication.status)])},
			    {schema.field(key, "title"), spec.title_key},
			    {schema.field(key, "description"), spec.description_key},
			    {schema.field(key, "outcome"), objective.outcome},
			    {schema.field(key, "radius"), region.radius},
			    {schema.field(key, "region"), region.id},
			    {schema.field(key, "credited"),
			     objective.completion_credited || publication.status == ObjectiveStatus::Completed},
			    {schema.field(key, "destination_x"), region.centre.x},
			    {schema.field(key, "destination_y"), region.centre.y}};
			if (spec.condition)
				for (const auto *field : {"radius", "region", "destination_x", "destination_y"})
					value.fields.erase(schema.field(key, field));
			if (publication.recipients.audience == ScenarioAudience::Ship)
				knowledge.publish_ship(*ship, value, publication.tick);
			else
				for (const auto &console : publication.recipients.consoles)
					knowledge.publish_scenario_console({*ship, console}, value, publication.tick);
			if (objective.status == ObjectiveStatus::Completed)
				runtime.acknowledge_credit(objective.id);
		}
		// Custom scenario publications are routed only to their declared recipients.
		else
		{
			try
			{
				const auto &schema = knowledge.definition();
				const auto key = schema.schema(publication.record);
				const auto &contract = schema.schemas.at(key - 1);
				KnowledgeRecordInput value;
				value.schema = key;
				value.producer = schema.role("phoenix.scenario");
				value.subject = *ship;
				value.provenance = {publication.instance, publication.tick, publication.id, 0, EvidenceKind::Claim};
				for (const auto &[name, field] : publication.fields)
					value.fields[schema.field(key, name)] = field;
				if (publication.recipients.audience == ScenarioAudience::Ship)
					knowledge.publish_ship(*ship, value, publication.tick);
				else
					for (const auto &console : publication.recipients.consoles)
						knowledge.publish_scenario_console({*ship, console}, value, publication.tick);
			}
			catch (const std::exception &)
			{
				continue;
			}
		}
		runtime.acknowledge_publication(publication.id);
	}
}

auto Runtime::scenario_effects() -> void
{
	if (!scenario)
		return;
	for (const auto &request : scenario->pending_effects())
	{
		std::string failure;
		try
		{
			if (request.kind == ScenarioEffectKind::Group)
				failure = execute_scenario_effect_group(request);
			else
				apply_scenario_effect(request);
		}
		catch (const std::exception &error)
		{
			failure = error.what();
		}
		scenario->confirm_effect(request.token, std::move(failure));
	}
}

} // namespace phoenix::world_detail
