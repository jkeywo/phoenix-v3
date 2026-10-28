module;
#include <optional>
#include <variant>
#include <map>
#include <vector>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

module phoenix.scenario;

namespace phoenix
{
#include "scenario_internal.hpp"

std::optional<bool> scenario_condition(const ScenarioCondition &c, const ScenarioFacts &facts)
{
	if (c.kind == ScenarioCondition::Kind::Leaf)
	{
		auto i = facts.find(c.fact);
		if (i == facts.end() || i->second.type == state_tree::Type::Missing)
			return {};
		return state_tree::compare_values(i->second, c.operation, c.value);
	}
	bool missing = false;
	bool value = c.kind == ScenarioCondition::Kind::All;
	for (const auto &child : c.children)
	{
		auto r = scenario_condition(child, facts);
		if (!r)
		{
			missing = true;
			continue;
		}
		if (c.kind == ScenarioCondition::Kind::Not)
			return !*r;
		if (c.kind == ScenarioCondition::Kind::All && !*r)
			return false;
		if (c.kind == ScenarioCondition::Kind::Any && *r)
			return true;
		if (c.kind == ScenarioCondition::Kind::All)
			value = value && *r;
		else
			value = value || *r;
	}
	if (missing)
		return {};
	return value;
}
void ScenarioRuntime::synchronize_time(ScenarioTick tick)
{
	require(tick >= tick_, "scenario time cannot reverse");
	tick_ = tick;
}
void ScenarioRuntime::evaluate_conditions(ScenarioTick tick, const std::map<ScenarioId, ScenarioFacts> &snapshots)
{
	synchronize_time(tick);
	for (auto &[id, o] : objectives_)
	{
		const auto &d = definition_.objectives.at(o.definition);
		if (!d.condition || o.status != ObjectiveStatus::Active || !snapshots.contains(o.instance))
			continue;
		const auto &facts = snapshots.at(o.instance);
		const auto failed = d.fail_when && scenario_condition(*d.fail_when, facts).value_or(false);
		if (failed || scenario_condition(d.complete_when, facts).value_or(false))
		{
			o.status = failed ? ObjectiveStatus::Failed : ObjectiveStatus::Completed;
			if (!failed)
			{
				o.completed_tick = tick_;
				auto i = facts.find("actor." + o.definition);
				o.completed_actor = i != facts.end() ? i->second.number : resolve(o.instance, o.actor);
				require(o.completed_actor > 0, "completed condition needs resolved actor");
			}
			objective_publication(o);
		}
	}
}
bool ScenarioActionToken::operator<(const ScenarioActionToken &other) const
{
	return std::tie(instance, call_path, call_activations, activation, task) <
	       std::tie(other.instance, other.call_path, other.call_activations, other.activation, other.task);
}
ScenarioRuntime::ScenarioRuntime(ScenarioDefinition definition) : definition_(std::move(definition))
{
}
const ScenarioDefinition &ScenarioRuntime::definition() const
{
	return definition_;
}
const std::map<ScenarioId, ScenarioInstance> &ScenarioRuntime::instances() const
{
	return instances_;
}
const std::map<ScenarioId, ScenarioObjective> &ScenarioRuntime::objectives() const
{
	return objectives_;
}
const std::map<ScenarioId, std::string> &ScenarioRuntime::spawned_entities() const
{
	return spawned_;
}
ScenarioId ScenarioRuntime::allocate()
{
	require(next_id_ && next_id_ < std::numeric_limits<ScenarioId>::max(), "identity allocator exhausted");
	return next_id_++;
}
ScenarioInstance &ScenarioRuntime::instance(ScenarioId id)
{
	require(instances_.contains(id), "unknown situation instance");
	return instances_.at(id);
}
ScenarioId ScenarioRuntime::create_instance(std::string name, std::map<std::string, ScenarioParameter> overrides)
{
	require(valid_identifier(name), "invalid situation instance name");
	require(instances_.size() < definition_.maximum_instances, "situation instance limit reached");
	for (const auto &[id, existing] : instances_)
		require(existing.name != name, "duplicate situation instance name");
	auto values = definition_.parameters;
	validate_parameters(overrides);
	for (auto &[key, value] : overrides)
	{
		require(values.contains(key) && values.at(key).index() == value.index(),
		        "unknown or mistyped situation parameter " + key);
		values[key] = std::move(value);
	}
	ScenarioInstance v;
	v.id = allocate();
	v.name = std::move(name);
	v.parameters = std::move(values);
	v.started_tick = tick_;
	const auto id = v.id;
	instances_.emplace(id, std::move(v));
	return id;
}
const ScenarioActionResult *ScenarioRuntime::result(ScenarioActionToken t) const
{
	const auto i = actions_.find(t);
	return i == actions_.end() ? nullptr : &i->second;
}
const ScenarioActionResult &ScenarioRuntime::start_situation(ScenarioActionToken t, std::string configuration,
                                                             std::string name,
                                                             std::map<std::string, ScenarioParameter> parameters_value)
{
	validate_parameters(parameters_value);
	const auto fp = part(configuration) + part(name) + fingerprint_parameters(parameters_value);
	const bool repeated = result(t) != nullptr;
	auto &out = admit(t, ScenarioActionKind::StartSituation, fp);
	if (repeated)
		return out;
	try
	{
		require(valid_identifier(configuration), "invalid situation configuration identity");
		out.value = create_instance(std::move(name), std::move(parameters_value));
		out.status = ScenarioActionStatus::Succeeded;
	}
	catch (const std::exception &e)
	{
		out.status = ScenarioActionStatus::Failed;
		out.reason = e.what();
	}
	return out;
}
ScenarioActionResult &ScenarioRuntime::admit(ScenarioActionToken t, ScenarioActionKind kind, std::string fp)
{
	if (auto i = actions_.find(t); i != actions_.end())
	{
		require(i->second.kind == kind && i->second.fingerprint == fp, "action token reused with different arguments");
		return i->second;
	}
	require(t.activation > 0, "action requires nonzero tree activation");
	require(t.call_path.size() == t.call_activations.size() && t.call_path.size() <= 64,
	        "action call ancestry mismatch");
	for (auto activation : t.call_activations)
		require(activation > 0, "action ancestor activation must be nonzero");
	require(!instance(t.instance).finished, "finished situation cannot issue new actions");
	require(actions_.size() < definition_.maximum_actions, "scenario action journal limit reached");
	return actions_.emplace(t, ScenarioActionResult{kind, ScenarioActionStatus::Pending, 0, {}, std::move(fp)})
	    .first->second;
}
void ScenarioRuntime::validate_actor(const ScenarioActor &a) const
{
	if (a.kind == ScenarioActorKind::Entity)
		require(a.entity > 0 && a.name.empty(), "invalid entity actor");
	else
	{
		require(a.entity == 0 && valid_identifier(a.name), "invalid named actor");
		require(a.kind == ScenarioActorKind::ShipSlot || a.kind == ScenarioActorKind::Handle, "invalid actor kind");
		if (a.kind == ScenarioActorKind::ShipSlot)
			require(contains_name(definition_.ship_slots, a.name), "unresolved actor ship slot " + a.name);
	}
}
void ScenarioRuntime::validate_recipients(const ScenarioRecipients &r) const
{
	validate_actor(r.ship);
	require((r.audience == ScenarioAudience::Ship && r.consoles.empty()) ||
	            (r.audience == ScenarioAudience::Consoles && !r.consoles.empty()),
	        "recipient scope/Console mismatch");
	std::set<std::string> names;
	for (const auto &c : r.consoles)
		require(valid_identifier(c) && names.insert(c).second, "invalid/duplicate recipient Console");
}
EntityId ScenarioRuntime::resolve(ScenarioId id, const ScenarioActor &a) const
{
	if (a.kind == ScenarioActorKind::Entity)
		return positions_.contains(a.entity) ? a.entity : 0;
	if (a.kind == ScenarioActorKind::ShipSlot)
	{
		auto i = slots_.find(a.name);
		return i != slots_.end() && positions_.contains(i->second) ? i->second : 0;
	}
	auto i = instances_.find(id);
	if (i == instances_.end())
		return 0;
	auto h = i->second.entity_handles.find(a.name);
	return h != i->second.entity_handles.end() && h->second.size() == 1 && positions_.contains(h->second.front())
	           ? h->second.front()
	           : 0;
}
const ScenarioActionResult &ScenarioRuntime::spawn_entity(ScenarioActionToken t, std::string definition,
                                                          ScenarioPoint p, std::int64_t heading, std::string handle)
{
	const auto fp = "entity:" + part(definition) + ":" + std::to_string(p.x) + ":" + std::to_string(p.y) + ":" +
	                std::to_string(heading) + part(handle);
	return spawn(t, {{std::move(definition), p, heading}}, std::move(handle), fp);
}
const ScenarioActionResult &ScenarioRuntime::spawn_group(ScenarioActionToken t, std::string name, ScenarioPoint offset,
                                                         std::string handle)
{
	const auto fp =
	    "group:" + part(name) + ":" + std::to_string(offset.x) + ":" + std::to_string(offset.y) + part(handle);
	std::vector<ScenarioSpawnPlacement> placements;
	if (scenario_valid_point(offset) && definition_.spawn_groups.contains(name))
	{
		placements = definition_.spawn_groups.at(name).entities;
		for (auto &p : placements)
		{
			p.position.x += offset.x;
			p.position.y += offset.y;
		}
	}
	return spawn(t, std::move(placements), std::move(handle), fp);
}
const ScenarioActionResult &ScenarioRuntime::spawn(ScenarioActionToken t, std::vector<ScenarioSpawnPlacement> entities,
                                                   std::string handle, std::string fp)
{
	const bool repeated = result(t) != nullptr;
	auto &out = admit(t, ScenarioActionKind::Spawn, std::move(fp));
	if (repeated)
		return out;
	auto fail = [&](std::string why) -> const ScenarioActionResult &
	{
		out.status = ScenarioActionStatus::Failed;
		out.reason = std::move(why);
		return out;
	};
	if (entities.empty())
		return fail("unknown or empty spawn group");
	if (!valid_identifier(handle) || instance(t.instance).entity_handles.contains(handle))
		return fail("invalid or already bound spawn handle");
	for (const auto &[id, request] : requests_)
		if (request.instance == t.instance && request.handle == handle)
			return fail("spawn handle already pending");
	std::size_t pending_count = 0;
	for (const auto &[id, r] : requests_)
		pending_count += r.entities.size();
	if (entities.size() > definition_.spawn_per_tick - spawned_this_tick_)
		return fail("per-tick spawn limit reached");
	if (entities.size() + spawned_.size() + pending_count > definition_.spawned_population)
		return fail("scenario spawned population limit reached");
	for (const auto &p : entities)
		if (!contains_name(definition_.entity_definitions, p.definition) || !scenario_valid_point(p.position) ||
		    p.heading < 0 || p.heading > 359999)
			return fail("invalid spawn definition, position or heading");
	ScenarioSpawnRequest r;
	r.id = allocate();
	r.instance = t.instance;
	r.handle = std::move(handle);
	r.entities = std::move(entities);
	r.action = t;
	out.value = r.id;
	out.handle = r.handle;
	spawned_this_tick_ += static_cast<std::uint32_t>(r.entities.size());
	requests_.emplace(r.id, std::move(r));
	return out;
}
std::vector<ScenarioSpawnRequest> ScenarioRuntime::pending_spawns() const
{
	std::vector<ScenarioSpawnRequest> out;
	for (const auto &[id, r] : requests_)
		out.push_back(r);
	return out;
}
void ScenarioRuntime::confirm_spawn(ScenarioId id, std::span<const EntityId> entities, std::string failure)
{
	auto found = requests_.find(id);
	require(found != requests_.end(), "unknown or already confirmed spawn request");
	const auto &r = found->second;
	auto &out = actions_.at(r.action);
	if (!failure.empty())
	{
		require(entities.empty(), "failed construction cannot acknowledge partial entity IDs");
		out.status = ScenarioActionStatus::Failed;
		out.reason = std::move(failure);
		requests_.erase(found);
		return;
	}
	require(entities.size() == r.entities.size(), "construction must confirm complete spawn group");
	std::set<EntityId> unique;
	for (auto entity : entities)
		require(entity > 0 && !spawned_.contains(entity) && !positions_.contains(entity) &&
		            unique.insert(entity).second,
		        "invalid/duplicate spawned entity identity");
	std::vector<EntityId> ids(entities.begin(), entities.end());
	for (std::size_t i = 0; i < ids.size(); ++i)
	{
		spawned_.emplace(ids[i], r.entities[i].definition);
		positions_.emplace(ids[i], r.entities[i].position);
	}
	out.entities = ids;
	instance(r.instance).entity_handles.emplace(r.handle, std::move(ids));
	out.status = ScenarioActionStatus::Succeeded;
	requests_.erase(found);
}
void ScenarioRuntime::objective_publication(const ScenarioObjective &o)
{
	ScenarioPublication p;
	p.id = allocate();
	p.instance = o.instance;
	p.objective = o.id;
	p.record = "objective";
	p.recipients = o.recipients;
	p.status = o.status;
	p.tick = tick_;
	publications_.push_back(std::move(p));
}
const ScenarioActionResult &ScenarioRuntime::add_objective(ScenarioActionToken t, std::string definition,
                                                           std::string handle, std::optional<ScenarioActor> target,
                                                           bool activate, std::string sequence)
{
	if (target)
		validate_actor(*target);
	const auto fp = part(definition) + part(handle) + (target ? fingerprint_actor(*target) : "default") +
	                (activate ? ":active" : ":pending") + (sequence.empty() ? "" : part(sequence));
	const bool repeated = result(t) != nullptr;
	auto &out = admit(t, ScenarioActionKind::Objective, fp);
	if (repeated)
		return out;
	auto fail = [&](std::string reason) -> const ScenarioActionResult &
	{
		out.status = ScenarioActionStatus::Failed;
		out.reason = std::move(reason);
		return out;
	};
	if (!sequence.empty())
	{
		if (!definition_.objective_sequences.contains(sequence))
			return fail("unknown objective sequence");
		definition = definition_.objective_sequences.at(sequence).objectives.front();
	}
	if (!definition_.objectives.contains(definition))
		return fail("unknown objective definition");
	if (!valid_identifier(handle) || instance(t.instance).objective_handles.contains(handle))
		return fail("invalid or already bound objective handle");
	if (objectives_.size() >= definition_.maximum_objectives)
		return fail("objective population limit reached");
	const auto &d = definition_.objectives.at(definition);
	if (!d.competition.empty())
		for (const auto &[id, existing] : objectives_)
			if (definition_.objectives.at(existing.definition).competition == d.competition &&
			    (!existing.outcome.empty() || existing.definition == definition))
				return fail("competition already finished or participant already admitted");
	const auto assigned = target.value_or(d.actor);
	if (activate && !resolve(t.instance, assigned))
		return fail("assigned objective actor is unavailable or handle is not singular");
	ScenarioObjective o;
	o.id = allocate();
	o.instance = t.instance;
	o.definition = std::move(definition);
	o.sequence = std::move(sequence);
	o.handle = std::move(handle);
	o.actor = assigned;
	o.recipients = d.recipients;
	o.created_tick = tick_;
	validate_actor(o.actor);
	validate_recipients(o.recipients);
	if (activate)
	{
		o.status = ObjectiveStatus::Active;
		const auto id = resolve(t.instance, o.actor);
		if (!d.condition && d.competition.empty() && id &&
		    scenario_contains(definition_.regions.at(d.region), positions_.at(id)))
		{
			o.status = ObjectiveStatus::Completed;
			o.completed_tick = tick_;
			o.completed_actor = id;
		}
	}
	out.value = o.id;
	out.handle = o.handle;
	out.status = ScenarioActionStatus::Succeeded;
	instance(t.instance).objective_handles.emplace(o.handle, o.id);
	objective_publication(o);
	objectives_.emplace(o.id, std::move(o));
	return out;
}
const ScenarioActionResult &ScenarioRuntime::set_objective_status(ScenarioActionToken t, ScenarioId id,
                                                                  ObjectiveStatus status)
{
	const auto fp = std::to_string(id) + ":" + std::to_string(static_cast<unsigned>(status));
	const bool repeated = result(t) != nullptr;
	auto &out = admit(t, ScenarioActionKind::ObjectiveStatus, fp);
	if (repeated)
		return out;
	auto fail = [&](std::string reason) -> const ScenarioActionResult &
	{
		out.status = ScenarioActionStatus::Failed;
		out.reason = std::move(reason);
		return out;
	};
	if (!objectives_.contains(id) || objectives_.at(id).instance != t.instance)
		return fail("objective does not belong to this situation");
	auto &o = objectives_.at(id);
	if (status == ObjectiveStatus::Completed)
		return fail("completion requires physical execution");
	if (o.status != ObjectiveStatus::Pending && o.status != ObjectiveStatus::Active)
		return fail("objective already terminal");
	if (status != ObjectiveStatus::Active && status != ObjectiveStatus::Failed && status != ObjectiveStatus::Cancelled)
		return fail("unsupported objective transition");
	if (status == ObjectiveStatus::Active && o.status != ObjectiveStatus::Pending)
		return fail("objective already active");
	if (status == ObjectiveStatus::Active && !resolve(o.instance, o.actor))
		return fail("assigned objective actor is unavailable or handle is not singular");
	o.status = status;
	out.status = ScenarioActionStatus::Succeeded;
	out.value = id;
	if (status == ObjectiveStatus::Active)
	{
		const auto actor_id = resolve(o.instance, o.actor);
		if (!definition_.objectives.at(o.definition).condition &&
		    definition_.objectives.at(o.definition).competition.empty() && actor_id &&
		    scenario_contains(definition_.regions.at(definition_.objectives.at(o.definition).region),
		                      positions_.at(actor_id)))
		{
			o.status = ObjectiveStatus::Completed;
			o.completed_tick = tick_;
			o.completed_actor = actor_id;
		}
	}
	objective_publication(o);
	return out;
}
std::optional<ObjectiveStatus> ScenarioRuntime::objective_status(ScenarioId id) const
{
	auto i = objectives_.find(id);
	if (i == objectives_.end())
		return {};
	return i->second.status;
}
const ScenarioActionResult &ScenarioRuntime::publish_objective(ScenarioActionToken t, ScenarioId id)
{
	const bool repeated = result(t) != nullptr;
	auto &out = admit(t, ScenarioActionKind::ObjectivePublication, std::to_string(id));
	if (repeated)
		return out;
	if (!objectives_.contains(id) || objectives_.at(id).instance != t.instance)
	{
		out.status = ScenarioActionStatus::Failed;
		out.reason = "objective does not belong to this situation";
		return out;
	}
	const auto &o = objectives_.at(id);
	for (const auto &p : publications_)
		if (p.objective == id && p.status == o.status)
		{
			out.status = ScenarioActionStatus::Succeeded;
			out.value = p.id;
			return out;
		}
	objective_publication(o);
	out.status = ScenarioActionStatus::Succeeded;
	out.value = publications_.back().id;
	return out;
}
std::optional<ScenarioId> ScenarioRuntime::objective_handle(ScenarioId id, std::string_view name) const
{
	auto i = instances_.find(id);
	if (i == instances_.end())
		return {};
	auto h = i->second.objective_handles.find(std::string(name));
	if (h == i->second.objective_handles.end())
		return {};
	return h->second;
}
const ScenarioActionResult &ScenarioRuntime::emit(ScenarioActionToken t, std::string name,
                                                  std::map<std::string, ScenarioParameter> payload)
{
	validate_parameters(payload);
	const auto fp = part(name) + fingerprint_parameters(payload);
	const bool repeated = result(t) != nullptr;
	auto &out = admit(t, ScenarioActionKind::Event, fp);
	if (repeated)
		return out;
	if (!contains_name(definition_.events, name) || events_.size() >= definition_.maximum_events)
	{
		out.status = ScenarioActionStatus::Failed;
		out.reason = "undeclared event or event journal limit reached";
		return out;
	}
	out.status = ScenarioActionStatus::Succeeded;
	out.value = allocate();
	events_.push_back({out.value, t.instance, std::move(name), std::move(payload), tick_});
	return out;
}
std::vector<ScenarioEvent> ScenarioRuntime::events(std::string_view name, ScenarioId after) const
{
	std::vector<ScenarioEvent> out;
	for (const auto &e : events_)
		if (e.id > after && e.name == name)
			out.push_back(e);
	return out;
}
bool ScenarioRuntime::has_event(std::string_view name, const std::map<std::string, ScenarioParameter> &match) const
{
	return std::any_of(events_.begin(), events_.end(),
	                   [&](const ScenarioEvent &event)
	                   {
		                   return event.name == name &&
		                          std::all_of(match.begin(), match.end(),
		                                      [&](const auto &field)
		                                      {
			                                      const auto value = event.payload.find(field.first);
			                                      return value != event.payload.end() && value->second == field.second;
		                                      });
	                   });
}
const ScenarioActionResult &ScenarioRuntime::publish(ScenarioActionToken t, std::string record,
                                                     ScenarioRecipients recipients_value,
                                                     std::map<std::string, ScenarioParameter> fields)
{
	validate_recipients(recipients_value);
	validate_parameters(fields);
	const auto fp = part(record) + fingerprint_recipients(recipients_value) + fingerprint_parameters(fields);
	const bool repeated = result(t) != nullptr;
	auto &out = admit(t, ScenarioActionKind::Publish, fp);
	if (repeated)
		return out;
	if (!contains_name(definition_.publications, record))
	{
		out.status = ScenarioActionStatus::Failed;
		out.reason = "undeclared scenario publication";
		return out;
	}
	out.value = allocate();
	out.status = ScenarioActionStatus::Succeeded;
	publications_.push_back({out.value, t.instance, 0, std::move(record), std::move(recipients_value),
	                         ObjectiveStatus::Pending, std::move(fields), tick_});
	return out;
}
const ScenarioActionResult &ScenarioRuntime::finish(ScenarioActionToken t, bool success, std::string reason)
{
	require(reason.size() <= 4096, "finish reason exceeds text limit");
	const bool repeated = result(t) != nullptr;
	auto &out = admit(t, ScenarioActionKind::Finish, (success ? "success" : "failure") + part(reason));
	if (repeated)
		return out;
	auto &s = instance(t.instance);
	s.finished = true;
	s.succeeded = success;
	s.finish_reason = std::move(reason);
	out.status = ScenarioActionStatus::Succeeded;
	return out;
}
void ScenarioRuntime::start_timer(ScenarioId id, std::string name)
{
	require(valid_identifier(name), "invalid timer name");
	instance(id).timers[std::move(name)] = tick_;
}
bool ScenarioRuntime::elapsed(ScenarioId id, std::string_view timer, ScenarioTick duration) const
{
	auto i = instances_.find(id);
	if (i == instances_.end())
		return false;
	auto t = i->second.timers.find(std::string(timer));
	return t != i->second.timers.end() && tick_ >= t->second && tick_ - t->second >= duration;
}
bool ScenarioRuntime::actor_in_region(ScenarioId id, const ScenarioActor &a, std::string_view region) const
{
	auto r = definition_.region_ids.find(std::string(region));
	if (r == definition_.region_ids.end())
		return false;
	const auto entity = resolve(id, a);
	return entity && scenario_contains(definition_.regions.at(r->second), positions_.at(entity));
}
std::optional<EntityId> ScenarioRuntime::resolve_actor(ScenarioId id, const ScenarioActor &a) const
{
	validate_actor(a);
	const auto entity = resolve(id, a);
	if (!entity)
		return {};
	return entity;
}
void ScenarioRuntime::begin_tick(ScenarioTick tick, std::map<std::string, EntityId> slots,
                                 std::map<EntityId, ScenarioPoint> positions)
{
	require(tick >= tick_, "scenario time cannot reverse");
	for (const auto &[slot, id] : slots)
		require(contains_name(definition_.ship_slots, slot) && id > 0 && positions.contains(id),
		        "invalid world ship slot binding");
	for (const auto &[id, p] : positions)
		require(id > 0 && scenario_valid_point(p), "invalid world position");
	if (tick != tick_)
		spawned_this_tick_ = 0;
	tick_ = tick;
	slots_ = std::move(slots);
	positions_ = std::move(positions);
	for (auto &[id, o] : objectives_)
	{
		if (o.sequence.empty() || o.status != ObjectiveStatus::Completed || !o.completion_published ||
		    !o.completion_credited || o.completed_tick >= tick_)
			continue;
		// Do not reuse the slot while any publication still refers to its previous leg.
		if (std::any_of(publications_.begin(), publications_.end(),
		                [&](const auto &p)
		                {
			                return p.objective == id;
		                }))
			continue;
		const auto &sequence = definition_.objective_sequences.at(o.sequence);
		auto next = o.sequence_index + 1;
		if (next == sequence.objectives.size())
		{
			if (!sequence.repeat)
				continue;
			require(o.cycle < std::numeric_limits<std::uint64_t>::max(), "objective cycle overflow");
			++o.cycle;
			next = 0;
		}
		o.sequence_index = next;
		o.definition = sequence.objectives.at(next);
		o.status = ObjectiveStatus::Active;
		o.created_tick = tick_;
		o.completed_tick = 0;
		o.completed_actor = 0;
		o.completion_published = o.completion_credited = false;
		objective_publication(o);
	}
}
void ScenarioRuntime::withdraw_actor(EntityId entity)
{
	positions_.erase(entity);
	std::erase_if(slots_,
	              [entity](const auto &entry)
	              {
		              return entry.second == entity;
	              });
}
void ScenarioRuntime::evaluate(std::span<const ScenarioMotionSegment> motion)
{
	std::map<std::string, std::vector<ScenarioId>> arrivals;
	for (const auto &m : motion)
	{
		require(m.entity > 0 && scenario_valid_point(m.from) && scenario_valid_point(m.to),
		        "invalid executed movement segment");
		positions_[m.entity] = m.to;
	}
	for (auto &[id, o] : objectives_)
	{
		if (o.status != ObjectiveStatus::Active || definition_.objectives.at(o.definition).condition)
			continue;
		const auto entity = resolve(o.instance, o.actor);
		if (!entity)
			continue;
		const auto &r = definition_.regions.at(definition_.objectives.at(o.definition).region);
		bool reached = o.sequence.empty() && scenario_contains(r, positions_.at(entity));
		for (const auto &m : motion)
			if (m.entity == entity && (o.sequence.empty() || !scenario_contains(r, m.from)) &&
			    scenario_crosses(r, m.from, m.to))
			{
				reached = true;
				break;
			}
		if (reached)
		{
			const auto &competition = definition_.objectives.at(o.definition).competition;
			if (!competition.empty())
			{
				arrivals[competition].push_back(id);
				continue;
			}
			o.status = ObjectiveStatus::Completed;
			o.completed_tick = tick_;
			o.completed_actor = entity;
			objective_publication(o);
		}
	}
	// Resolve a whole movement tick before assigning credit: map iteration order
	// must never choose the winner of simultaneous physical arrivals.
	for (const auto &[group, winners] : arrivals)
		for (auto &[id, o] : objectives_)
			if (o.status == ObjectiveStatus::Active && definition_.objectives.at(o.definition).competition == group)
			{
				const bool won = std::find(winners.begin(), winners.end(), id) != winners.end();
				o.status = won ? ObjectiveStatus::Completed : ObjectiveStatus::Failed;
				o.outcome = won ? (winners.size() > 1 ? "draw" : "won") : "lost";
				if (won)
				{
					o.completed_tick = tick_;
					o.completed_actor = resolve(o.instance, o.actor);
				}
				objective_publication(o);
			}
}
bool ScenarioRuntime::mission_complete() const
{
	return std::any_of(objectives_.begin(), objectives_.end(),
	                   [](const auto &entry)
	                   {
		                   return !entry.second.outcome.empty();
	                   });
}
std::vector<ScenarioPublication> ScenarioRuntime::pending_publications() const
{
	return publications_;
}
void ScenarioRuntime::acknowledge_publication(ScenarioId id)
{
	auto i = std::find_if(publications_.begin(), publications_.end(),
	                      [&](const auto &p)
	                      {
		                      return p.id == id;
	                      });
	require(i != publications_.end(), "unknown or acknowledged publication");
	if (i->objective && i->status == ObjectiveStatus::Completed)
		objectives_.at(i->objective).completion_published = true;
	publications_.erase(i);
}
void ScenarioRuntime::acknowledge_credit(ScenarioId id)
{
	require(objectives_.contains(id) && objectives_.at(id).status == ObjectiveStatus::Completed,
	        "credit requires completed objective");
	objectives_.at(id).completion_credited = true;
}

Json ScenarioRuntime::save() const
{
	Json j{{"version", 2},
	       {"contract", definition_.signature},
	       {"next_id", next_id_},
	       {"tick", tick_},
	       {"spawned_this_tick", spawned_this_tick_}};
	for (const char *key :
	     {"instances", "objectives", "spawned", "requests", "actions", "events", "publications", "positions"})
		j[key] = Json::array();
	j["slots"] = slots_;
	for (const auto &[id, i] : instances_)
		j["instances"].push_back({{"id", id},
		                          {"name", i.name},
		                          {"parameters", encode_parameters(i.parameters)},
		                          {"entities", i.entity_handles},
		                          {"objectives", i.objective_handles},
		                          {"timers", i.timers},
		                          {"started", i.started_tick},
		                          {"finished", i.finished},
		                          {"succeeded", i.succeeded},
		                          {"finish_reason", i.finish_reason}});
	for (const auto &[id, o] : objectives_)
		j["objectives"].push_back({{"id", id},
		                           {"instance", o.instance},
		                           {"definition", o.definition},
		                           {"sequence", o.sequence},
		                           {"outcome", o.outcome},
		                           {"sequence_index", o.sequence_index},
		                           {"cycle", o.cycle},
		                           {"handle", o.handle},
		                           {"actor", encode_actor(o.actor)},
		                           {"recipients", encode_recipients(o.recipients)},
		                           {"status", static_cast<unsigned>(o.status)},
		                           {"created", o.created_tick},
		                           {"completed", o.completed_tick},
		                           {"completed_actor", o.completed_actor},
		                           {"published", o.completion_published},
		                           {"credited", o.completion_credited}});
	for (const auto &[id, definition] : spawned_)
		j["spawned"].push_back({{"id", id}, {"definition", definition}});
	for (const auto &[id, r] : requests_)
	{
		Json placements = Json::array();
		for (const auto &p : r.entities)
			placements.push_back(encode_placement(p));
		j["requests"].push_back({{"id", id},
		                         {"instance", r.instance},
		                         {"handle", r.handle},
		                         {"entities", placements},
		                         {"action", encode_token(r.action)}});
	}
	for (const auto &[t, r] : actions_)
		j["actions"].push_back({{"token", encode_token(t)},
		                        {"kind", static_cast<unsigned>(r.kind)},
		                        {"status", static_cast<unsigned>(r.status)},
		                        {"value", r.value},
		                        {"reason", r.reason},
		                        {"fingerprint", r.fingerprint},
		                        {"handle", r.handle},
		                        {"entities", r.entities}});
	for (const auto &e : events_)
		j["events"].push_back({{"id", e.id},
		                       {"instance", e.source_instance},
		                       {"name", e.name},
		                       {"payload", encode_parameters(e.payload)},
		                       {"tick", e.tick}});
	for (const auto &p : publications_)
		j["publications"].push_back({{"id", p.id},
		                             {"instance", p.instance},
		                             {"objective", p.objective},
		                             {"record", p.record},
		                             {"recipients", encode_recipients(p.recipients)},
		                             {"status", static_cast<unsigned>(p.status)},
		                             {"fields", encode_parameters(p.fields)},
		                             {"tick", p.tick}});
	for (const auto &[id, p] : positions_)
		j["positions"].push_back({{"id", id}, {"x_mm", p.x}, {"y_mm", p.y}});
	return j;
}

void ScenarioRuntime::restore(const Json &j)
{
	require((j.at("version") == 1 || j.at("version") == 2) && j.at("contract") == definition_.signature,
	        "unsupported continuation or changed scenario definitions");
	ScenarioRuntime r(definition_);
	r.next_id_ = unsigned_number(j.at("next_id"), "next_id");
	r.tick_ = unsigned_number(j.at("tick"), "tick");
	const auto saved_spawn_count = unsigned_number(j.at("spawned_this_tick"), "spawned_this_tick");
	require(saved_spawn_count <= definition_.spawn_per_tick, "invalid saved spawn count");
	r.spawned_this_tick_ = static_cast<std::uint32_t>(saved_spawn_count);
	require(r.next_id_ > 0 && r.spawned_this_tick_ <= definition_.spawn_per_tick, "invalid allocator/spawn count");
	std::set<ScenarioId> ids;
	auto identity = [&](ScenarioId id)
	{
		require(id > 0 && id < r.next_id_ && ids.insert(id).second, "invalid/duplicate scenario identity");
	};
	std::set<std::string> instance_names;
	for (const auto &v : list(j, "instances"))
	{
		ScenarioInstance i;
		i.id = unsigned_number(v.at("id"), "id");
		identity(i.id);
		i.name = identifier(v, "name");
		require(instance_names.insert(i.name).second, "duplicate saved situation name");
		i.parameters = parameters(v.at("parameters"));
		require(i.parameters.size() == definition_.parameters.size(), "saved situation parameter mismatch");
		for (const auto &[key, value] : i.parameters)
			require(definition_.parameters.contains(key) && definition_.parameters.at(key).index() == value.index(),
			        "saved situation parameter type mismatch");
		i.entity_handles = v.at("entities").get<std::map<std::string, std::vector<EntityId>>>();
		i.objective_handles = v.at("objectives").get<std::map<std::string, ScenarioId>>();
		i.timers = v.at("timers").get<std::map<std::string, ScenarioTick>>();
		i.started_tick = unsigned_number(v.at("started"), "started");
		i.finished = v.at("finished").get<bool>();
		i.succeeded = v.at("succeeded").get<bool>();
		i.finish_reason = v.at("finish_reason").get<std::string>();
		require(i.finish_reason.size() <= 4096 && (i.finished || i.finish_reason.empty()),
		        "invalid saved finish reason");
		require(i.started_tick <= r.tick_ && (!i.succeeded || i.finished), "invalid saved situation lifecycle");
		for (const auto &[name, tick] : i.timers)
			require(valid_identifier(name) && tick >= i.started_tick && tick <= r.tick_, "invalid saved timer");
		r.instances_.emplace(i.id, std::move(i));
	}
	require(r.instances_.size() <= definition_.maximum_instances, "saved instance limit exceeded");
	for (const auto &v : list(j, "positions", 65536))
	{
		const auto id = unsigned_number(v.at("id"), "id");
		require(id > 0 && r.positions_.emplace(id, point(v)).second, "invalid/duplicate saved position");
	}
	r.slots_ = j.at("slots").get<std::map<std::string, EntityId>>();
	for (const auto &[name, id] : r.slots_)
		require(contains_name(definition_.ship_slots, name) && r.positions_.contains(id), "invalid saved slot");
	for (const auto &v : list(j, "spawned", definition_.spawned_population))
	{
		const auto id = unsigned_number(v.at("id"), "id");
		const auto name = read_scenario_text(v, "definition");
		require(id > 0 && contains_name(definition_.entity_definitions, name) && r.spawned_.emplace(id, name).second,
		        "invalid/duplicate spawned entity definition");
	}
	for (const auto &v : list(j, "objectives", definition_.maximum_objectives))
	{
		ScenarioObjective o;
		o.id = unsigned_number(v.at("id"), "id");
		identity(o.id);
		o.instance = unsigned_number(v.at("instance"), "instance");
		require(r.instances_.contains(o.instance), "saved objective has unknown instance");
		o.definition = read_scenario_text(v, "definition");
		o.sequence = v.value("sequence", std::string{});
		o.cycle = unsigned_number(v.value("cycle", Json(0)), "cycle");
		const auto index = unsigned_number(v.value("sequence_index", Json(0)), "sequence_index");
		if (!o.sequence.empty())
		{
			require(definition_.objective_sequences.contains(o.sequence), "saved objective sequence missing");
			const auto &sequence = definition_.objective_sequences.at(o.sequence);
			require(index < sequence.objectives.size() && sequence.objectives[index] == o.definition &&
			            (sequence.repeat || o.cycle == 0),
			        "invalid saved sequence position");
		}
		else
			require(index == 0 && o.cycle == 0, "ordinary objective has sequence state");
		o.sequence_index = static_cast<std::uint32_t>(index);
		o.handle = identifier(v, "handle");
		require(definition_.objectives.contains(o.definition), "saved objective definition missing");
		o.actor = actor(v.at("actor"));
		o.recipients = recipients(v.at("recipients"));
		r.validate_actor(o.actor);
		r.validate_recipients(o.recipients);
		require(o.recipients == definition_.objectives.at(o.definition).recipients,
		        "saved objective recipient scope differs");
		const auto status = v.at("status").get<unsigned>();
		require(status <= static_cast<unsigned>(ObjectiveStatus::Cancelled), "invalid saved objective status");
		o.status = static_cast<ObjectiveStatus>(status);
		o.outcome = v.value("outcome", std::string{});
		require(o.outcome.empty() ||
		            (!definition_.objectives.at(o.definition).competition.empty() &&
		             ((o.outcome == "lost" && o.status == ObjectiveStatus::Failed) ||
		              ((o.outcome == "won" || o.outcome == "draw") && o.status == ObjectiveStatus::Completed))),
		        "invalid saved competition result");
		o.created_tick = unsigned_number(v.at("created"), "created");
		o.completed_tick = unsigned_number(v.at("completed"), "completed");
		o.completed_actor = unsigned_number(v.at("completed_actor"), "completed_actor");
		o.completion_published = v.at("published").get<bool>();
		o.completion_credited = v.at("credited").get<bool>();
		require(o.created_tick <= r.tick_, "objective created in future");
		if (o.status == ObjectiveStatus::Completed)
			require(o.completed_actor > 0 && o.completed_tick >= o.created_tick && o.completed_tick <= r.tick_,
			        "invalid completed objective evidence");
		else
			require(o.completed_actor == 0 && o.completed_tick == 0 && !o.completion_published &&
			            !o.completion_credited,
			        "uncompleted objective has completion credit");
		r.objectives_.emplace(o.id, std::move(o));
	}
	require(r.objectives_.size() <= definition_.maximum_objectives, "saved objective limit exceeded");
	for (const auto &[id, i] : r.instances_)
	{
		for (const auto &[name, entities] : i.entity_handles)
		{
			require(valid_identifier(name) && !entities.empty(), "invalid saved entity handle");
			std::set<EntityId> members;
			for (auto e : entities)
				require(r.spawned_.contains(e) && members.insert(e).second, "invalid saved handle member");
		}
		for (const auto &[name, o] : i.objective_handles)
			require(valid_identifier(name) && r.objectives_.contains(o) && r.objectives_.at(o).instance == id &&
			            r.objectives_.at(o).handle == name,
			        "invalid saved objective handle");
	}
	for (const auto &[id, o] : r.objectives_)
		require(r.instances_.at(o.instance).objective_handles.contains(o.handle) &&
		            r.instances_.at(o.instance).objective_handles.at(o.handle) == id,
		        "saved objective lacks handle");
	for (const auto &v : list(j, "actions", definition_.maximum_actions))
	{
		const auto t = token(v.at("token"));
		require(t.activation > 0 && r.instances_.contains(t.instance), "invalid saved action origin");
		const auto kind = v.at("kind").get<unsigned>(), status = v.at("status").get<unsigned>();
		require(kind <= static_cast<unsigned>(ScenarioActionKind::StartSituation) &&
		            status <= static_cast<unsigned>(ScenarioActionStatus::Failed),
		        "invalid saved action kind/status");
		ScenarioActionResult a{static_cast<ScenarioActionKind>(kind), static_cast<ScenarioActionStatus>(status),
		                       unsigned_number(v.at("value"), "value"), v.at("reason").get<std::string>(),
		                       v.at("fingerprint").get<std::string>()};
		a.handle = v.at("handle").get<std::string>();
		a.entities = v.at("entities").get<std::vector<EntityId>>();
		require(a.fingerprint.size() <= 1048576 && a.reason.size() <= 4096 && (a.value == 0 || a.value < r.next_id_),
		        "invalid saved action result");
		require(r.actions_.emplace(t, std::move(a)).second, "duplicate saved action token");
	}
	require(r.actions_.size() <= definition_.maximum_actions, "saved action limit exceeded");
	std::set<std::pair<ScenarioId, std::string>> pending_handles;
	std::size_t pending_population = 0;
	for (const auto &v : list(j, "requests", definition_.spawned_population))
	{
		ScenarioSpawnRequest q;
		q.id = unsigned_number(v.at("id"), "id");
		identity(q.id);
		q.instance = unsigned_number(v.at("instance"), "instance");
		q.handle = identifier(v, "handle");
		q.action = token(v.at("action"));
		require(r.instances_.contains(q.instance) && q.action.instance == q.instance && r.actions_.contains(q.action),
		        "invalid saved spawn origin");
		const auto &a = r.actions_.at(q.action);
		require(a.kind == ScenarioActionKind::Spawn && a.status == ScenarioActionStatus::Pending && a.value == q.id,
		        "saved spawn action mismatch");
		require(!r.instances_.at(q.instance).entity_handles.contains(q.handle) &&
		            pending_handles.insert({q.instance, q.handle}).second,
		        "duplicate pending spawn handle");
		for (const auto &p : list(v, "entities"))
		{
			ScenarioSpawnPlacement placement{read_scenario_text(p, "definition"), point(p),
			                                 number(p, "heading_mdeg", 0, 0, 359999)};
			require(contains_name(definition_.entity_definitions, placement.definition),
			        "saved spawn definition missing");
			q.entities.push_back(std::move(placement));
		}
		require(!q.entities.empty(), "empty saved spawn request");
		pending_population += q.entities.size();
		r.requests_.emplace(q.id, std::move(q));
	}
	require(pending_population + r.spawned_.size() <= definition_.spawned_population,
	        "saved spawned population limit exceeded");
	for (const auto &v : list(j, "events", definition_.maximum_events))
	{
		ScenarioEvent e{unsigned_number(v.at("id"), "id"), unsigned_number(v.at("instance"), "instance"),
		                read_scenario_text(v, "name"), parameters(v.at("payload")),
		                unsigned_number(v.at("tick"), "tick")};
		identity(e.id);
		require(r.instances_.contains(e.source_instance) && contains_name(definition_.events, e.name) &&
		            e.tick <= r.tick_,
		        "invalid saved event");
		r.events_.push_back(std::move(e));
	}
	require(r.events_.size() <= definition_.maximum_events, "saved event journal limit exceeded");
	for (const auto &v : list(j, "publications", definition_.maximum_actions + 3 * definition_.maximum_objectives))
	{
		ScenarioPublication p;
		p.id = unsigned_number(v.at("id"), "id");
		identity(p.id);
		p.instance = unsigned_number(v.at("instance"), "instance");
		p.objective = unsigned_number(v.at("objective"), "objective");
		p.record = read_scenario_text(v, "record");
		require(r.instances_.contains(p.instance), "saved publication origin missing");
		p.recipients = recipients(v.at("recipients"));
		r.validate_recipients(p.recipients);
		const auto status = v.at("status").get<unsigned>();
		require(status <= static_cast<unsigned>(ObjectiveStatus::Cancelled), "invalid published objective status");
		p.status = static_cast<ObjectiveStatus>(status);
		p.fields = parameters(v.at("fields"));
		p.tick = unsigned_number(v.at("tick"), "tick");
		require(p.tick <= r.tick_, "publication is from future");
		if (p.objective)
			require(p.record == "objective" && r.objectives_.contains(p.objective) &&
			            r.objectives_.at(p.objective).instance == p.instance &&
			            r.objectives_.at(p.objective).recipients == p.recipients,
			        "saved objective publication scope mismatch");
		else
			require(contains_name(definition_.publications, p.record), "saved publication not declared");
		r.publications_.push_back(std::move(p));
	}
	for (const auto &[t, a] : r.actions_)
	{
		if (a.status == ScenarioActionStatus::Pending)
			require(a.kind == ScenarioActionKind::Spawn && r.requests_.contains(a.value),
			        "pending saved action has no request");
		if (a.status != ScenarioActionStatus::Succeeded)
			continue;
		if (a.kind == ScenarioActionKind::Spawn)
		{
			const auto &handles = r.instances_.at(t.instance).entity_handles;
			require(valid_identifier(a.handle) && handles.contains(a.handle) && !a.entities.empty() &&
			            handles.at(a.handle) == a.entities,
			        "successful saved spawn has no matching entity handle");
		}
		if (a.kind == ScenarioActionKind::Objective || a.kind == ScenarioActionKind::ObjectiveStatus)
			require(r.objectives_.contains(a.value) && r.objectives_.at(a.value).instance == t.instance,
			        "saved action objective missing");
		if (a.kind == ScenarioActionKind::Event)
			require(std::any_of(r.events_.begin(), r.events_.end(),
			                    [&](const auto &e)
			                    {
				                    return e.id == a.value && e.source_instance == t.instance;
			                    }),
			        "saved action event missing");
		if (a.kind == ScenarioActionKind::StartSituation)
			require(r.instances_.contains(a.value) && a.value != t.instance, "saved started situation missing");
	}
	*this = std::move(r);
}
} // namespace phoenix
