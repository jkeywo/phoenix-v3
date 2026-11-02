module;
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

module phoenix.gameplay_scenario;
import phoenix.content.effects;
import phoenix.content.knowledge;

namespace phoenix
{
namespace
{
using state_tree::Type;
using state_tree::Value;
void scenario_require(bool valid, const std::string &reason)
{
	if (!valid)
		throw std::runtime_error("scenario bridge: " + reason);
}
std::uint64_t scenario_unsigned(const Json &v)
{
	scenario_require(v.is_number_integer() && v >= 0, "saved unsigned integer required");
	return v.get<std::uint64_t>();
}
bool is_effect_action(GameplayTreeAction action)
{
	return action == GameplayTreeAction::ApplyShieldEffect || action == GameplayTreeAction::RefreshShieldEffect ||
	       action == GameplayTreeAction::RemoveShieldEffect || action == GameplayTreeAction::ApplyModifierEffect ||
	       action == GameplayTreeAction::RefreshModifierEffect || action == GameplayTreeAction::RemoveModifierEffect ||
	       action == GameplayTreeAction::ApplyEffectGroup;
}
GameplayTreeAction effect_action(ScenarioEffectKind kind, ScenarioEffectOperation operation)
{
	if (kind == ScenarioEffectKind::Group)
	{
		scenario_require(operation == ScenarioEffectOperation::Apply, "operation group only supports apply");
		return GameplayTreeAction::ApplyEffectGroup;
	}
	if (kind == ScenarioEffectKind::Shield)
		return operation == ScenarioEffectOperation::Apply     ? GameplayTreeAction::ApplyShieldEffect
		       : operation == ScenarioEffectOperation::Refresh ? GameplayTreeAction::RefreshShieldEffect
		                                                       : GameplayTreeAction::RemoveShieldEffect;
	return operation == ScenarioEffectOperation::Apply     ? GameplayTreeAction::ApplyModifierEffect
	       : operation == ScenarioEffectOperation::Refresh ? GameplayTreeAction::RefreshModifierEffect
	                                                       : GameplayTreeAction::RemoveModifierEffect;
}
std::vector<Value> effect_arguments(const ScenarioEffectRequest &request)
{
	std::vector<Value> arguments{{Type::Entity, static_cast<std::int64_t>(request.target), {}}};
	if (request.kind == ScenarioEffectKind::Group)
	{
		arguments.push_back({Type::Entity, static_cast<std::int64_t>(request.source), {}});
		arguments.push_back({Type::Text, 0, request.definition});
		return arguments;
	}
	if (request.kind == ScenarioEffectKind::Modifier)
		arguments.push_back({Type::Text, 0, request.equipment});
	if (request.operation == ScenarioEffectOperation::Apply)
		arguments.push_back({Type::Text, 0, request.definition});
	arguments.push_back({Type::Text, 0, request.grant});
	return arguments;
}
ScenarioEffectRequest effect_request(ScenarioActionToken token, GameplayTreeAction action,
                                     const std::vector<Value> &arguments)
{
	ScenarioEffectRequest request;
	request.token = std::move(token);
	scenario_require(arguments.at(0).number > 0, "effect target must be nonzero");
	request.target = static_cast<EntityId>(arguments[0].number);
	if (action == GameplayTreeAction::ApplyEffectGroup)
	{
		request.kind = ScenarioEffectKind::Group;
		scenario_require(arguments.at(1).number >= 0, "invalid effect group source");
		request.source = static_cast<EntityId>(arguments[1].number);
		request.definition = arguments.at(2).text;
		scenario_require(!request.definition.empty() && request.definition.size() <= 256,
		                 "invalid effect group definition");
		return request;
	}
	request.kind = action == GameplayTreeAction::ApplyModifierEffect ||
	                       action == GameplayTreeAction::RefreshModifierEffect ||
	                       action == GameplayTreeAction::RemoveModifierEffect
	                   ? ScenarioEffectKind::Modifier
	                   : ScenarioEffectKind::Shield;
	request.operation =
	    action == GameplayTreeAction::ApplyShieldEffect || action == GameplayTreeAction::ApplyModifierEffect
	        ? ScenarioEffectOperation::Apply
	    : action == GameplayTreeAction::RefreshShieldEffect || action == GameplayTreeAction::RefreshModifierEffect
	        ? ScenarioEffectOperation::Refresh
	        : ScenarioEffectOperation::Remove;
	std::size_t index = 1;
	if (request.kind == ScenarioEffectKind::Modifier)
	{
		request.equipment = arguments.at(index++).text;
		scenario_require(!request.equipment.empty() && request.equipment.size() <= 128,
		                 "bounded nonempty equipment target required");
	}
	if (request.operation == ScenarioEffectOperation::Apply)
	{
		request.definition = arguments.at(index++).text;
		scenario_require(!request.definition.empty() && request.definition.size() <= 4096,
		                 "effect definition required");
	}
	request.grant = arguments.at(index).text;
	scenario_require(!request.grant.empty() && request.grant.size() <= 128,
	                 "bounded nonempty effect grant name required");
	return request;
}
ScenarioActor bridge_actor(const Json &v)
{
	scenario_require(v.is_object() && v.size() == 1, "actor binding needs one entity/slot/handle");
	if (v.contains("entity"))
	{
		const auto id = scenario_unsigned(v.at("entity"));
		scenario_require(id > 0, "actor entity must be nonzero");
		return {ScenarioActorKind::Entity, id, {}};
	}
	if (v.contains("slot"))
		return {ScenarioActorKind::ShipSlot, 0, v.at("slot").get<std::string>()};
	if (v.contains("participant"))
		return {ScenarioActorKind::Participant, 0, v.at("participant").get<std::string>()};
	return {ScenarioActorKind::Handle, 0, v.at("handle").get<std::string>()};
}
Json bridge_actor_json(const ScenarioActor &a)
{
	if (a.kind == ScenarioActorKind::Entity)
		return {{"entity", a.entity}};
	return {{a.kind == ScenarioActorKind::ShipSlot      ? "slot"
	         : a.kind == ScenarioActorKind::Participant ? "participant"
	                                                    : "handle",
	         a.name}};
}
ScenarioTreeSelectors bridge_selectors(const Json &v)
{
	scenario_require(v.is_object(), "scenario bindings must be object");
	ScenarioTreeSelectors out;
	if (v.contains("actor"))
		out.actor = bridge_actor(v.at("actor"));
	out.objective = v.value("objective", std::string{});
	out.region = v.value("region", std::string{});
	out.event = v.value("event", std::string{});
	out.knowledge_console = v.value("knowledge_console", std::string{});
	return out;
}
Json bridge_selectors_json(const ScenarioTreeSelectors &s)
{
	Json j{{"objective", s.objective},
	       {"region", s.region},
	       {"event", s.event},
	       {"knowledge_console", s.knowledge_console}};
	if (s.actor)
		j["actor"] = bridge_actor_json(*s.actor);
	return j;
}
ScenarioParameter bridge_parameter(const Json &v)
{
	if (v.is_boolean())
		return v.get<bool>();
	if (v.is_number_integer())
	{
		scenario_require(v >= -1000000000000LL && v <= 1000000000000LL, "parameter outside bounded integer range");
		return v.get<std::int64_t>();
	}
	scenario_require(v.is_string() && v.get_ref<const std::string &>().size() <= 4096, "invalid scalar parameter");
	return v.get<std::string>();
}
std::map<std::string, ScenarioParameter> bridge_parameters(const Json &j)
{
	scenario_require(j.is_object() && j.size() <= 128, "bounded parameter object required");
	std::map<std::string, ScenarioParameter> out;
	for (auto i = j.begin(); i != j.end(); ++i)
		out.emplace(i.key(), bridge_parameter(i.value()));
	return out;
}
Json bridge_token_json(const ScenarioActionToken &t)
{
	return {{"instance", t.instance},
	        {"activation", t.activation},
	        {"task", t.task},
	        {"calls", t.call_path},
	        {"activations", t.call_activations}};
}
ScenarioActionToken bridge_token(const Json &j)
{
	const auto task = scenario_unsigned(j.at("task"));
	scenario_require(task <= std::numeric_limits<std::uint32_t>::max(), "saved task out of range");
	ScenarioActionToken t{scenario_unsigned(j.at("instance")), scenario_unsigned(j.at("activation")),
	                      static_cast<std::uint32_t>(task)};
	scenario_require(j.at("calls").is_array() && j.at("activations").is_array() && j.at("calls").size() <= 64 &&
	                     j.at("calls").size() == j.at("activations").size(),
	                 "invalid saved call ancestry");
	for (const auto &c : j.at("calls"))
	{
		const auto value = scenario_unsigned(c);
		scenario_require(value <= std::numeric_limits<std::uint32_t>::max(), "saved call task out of range");
		t.call_path.push_back(static_cast<std::uint32_t>(value));
	}
	for (const auto &a : j.at("activations"))
	{
		const auto value = scenario_unsigned(a);
		scenario_require(value > 0, "saved ancestor activation is zero");
		t.call_activations.push_back(value);
	}
	scenario_require(t.instance > 0 && t.activation > 0, "saved effect identity cannot be zero");
	return t;
}
bool bridge_value_valid(const Value &v, Type expected)
{
	if (v.type != expected)
		return false;
	if (expected == Type::Boolean)
		return v.text.empty() && (v.number == 0 || v.number == 1);
	if (expected == Type::Text)
		return v.number == 0 && v.text.size() <= 4096;
	return v.text.empty() && (expected == Type::Integer || v.number > 0);
}
std::string bridge_fingerprint(GameplayTreeAction action, std::span<const Value> arguments)
{
	std::string out = std::to_string(static_cast<unsigned>(action)) + ":";
	for (const auto &v : arguments)
		out += std::to_string(static_cast<unsigned>(v.type)) + ":" + std::to_string(v.number) + ":" +
		       std::to_string(v.text.size()) + ":" + v.text;
	return out;
}
std::string bridge_request(const ScenarioActionToken &token)
{
	auto out = std::to_string(token.instance) + "/";
	for (std::size_t i = 0; i < token.call_path.size(); ++i)
		out += std::to_string(token.call_path[i]) + ":" + std::to_string(token.call_activations[i]) + "/";
	return out + std::to_string(token.activation) + "/" + std::to_string(token.task);
}
const std::string domain_fingerprint_prefix = "bridge:";
ScenarioActionKind bridge_action_kind(GameplayTreeAction action)
{
	switch (action)
	{
	case GameplayTreeAction::ApplyShieldEffect:
	case GameplayTreeAction::RefreshShieldEffect:
	case GameplayTreeAction::RemoveShieldEffect:
	case GameplayTreeAction::ApplyModifierEffect:
	case GameplayTreeAction::RefreshModifierEffect:
	case GameplayTreeAction::RemoveModifierEffect:
	case GameplayTreeAction::ApplyEffectGroup:
		return ScenarioActionKind::Event;
	case GameplayTreeAction::SpawnEntity:
	case GameplayTreeAction::SpawnGroup:
		return ScenarioActionKind::Spawn;
	case GameplayTreeAction::AddObjective:
	case GameplayTreeAction::StartObjectiveSequence:
		return ScenarioActionKind::Objective;
	case GameplayTreeAction::PublishObjective:
		return ScenarioActionKind::ObjectivePublication;
	case GameplayTreeAction::PublishScenarioMessage:
		return ScenarioActionKind::Publish;
	case GameplayTreeAction::EmitScenarioEvent:
		return ScenarioActionKind::Event;
	case GameplayTreeAction::FinishScenario:
		return ScenarioActionKind::Finish;
	case GameplayTreeAction::StartSituation:
		return ScenarioActionKind::StartSituation;
	case GameplayTreeAction::RequestTask:
		return ScenarioActionKind::Task;
	case GameplayTreeAction::SetObjectiveStatus:
		return ScenarioActionKind::ObjectiveStatus;
	default:
		throw std::runtime_error("scenario bridge: unsupported scenario effect kind");
	}
}
std::int64_t bounded_tick(std::uint64_t value)
{
	return static_cast<std::int64_t>(std::min<std::uint64_t>(value, std::numeric_limits<std::int64_t>::max()));
}
} // namespace

void GameplayScenarioBridge::load(const Catalog &catalog, const std::string &name, SharedPropertyCatalog properties)
{
	scenario_require(catalog.maps.contains(name), "map does not exist " + name);
	GameplayScenarioBridge candidate;
	candidate.map_ = name;
	candidate.tree_sources_ = catalog.trees;
	candidate.programs_ = catalog.tree_programs;
	candidate.knowledge_ = catalog.knowledge_definition;
	for (const auto &[id, tree] : catalog.trees)
		if (catalog.tree_programs.contains(id) && catalog.tree_programs.at(id).context == state_tree::Context::Scenario)
		{
			candidate.record_bindings_.emplace(id, gameplay_record_bindings(tree, catalog));
			for (const auto &binding : scenario_tree_facts(tree, catalog))
			{
				const auto &fields = catalog.tree_programs.at(id).registry.observations;
				const auto field = std::find_if(fields.begin(), fields.end(),
				                                [&](const auto &f)
				                                {
					                                return f.name == binding.name;
				                                });
				scenario_require(field != fields.end(), "scenario fact missing from compiled registry");
				candidate.fact_bindings_[id].push_back({static_cast<std::size_t>(field - fields.begin()), binding});
			}
		}
	const auto compiled_knowledge = compile_gameplay_knowledge(catalog, std::move(properties));
	for (const auto &[hull, ship] : compiled_knowledge.ships)
		for (const auto &console : ship.consoles)
			candidate.reader_contracts_[hull].emplace(console.console.id, console.readers);
	const auto definition = catalog.scenarios.find(name);
	if (definition != catalog.scenarios.end())
		candidate.runtime_ = ScenarioRuntime(definition->second);
	else
	{
		std::vector<std::string> entity_ids, slots;
		for (const auto &[id, entity] : catalog.entities)
			entity_ids.push_back(id);
		for (const auto &slot : catalog.maps.at(name).at("ship_slots"))
			slots.push_back(slot.at("id").get<std::string>());
		candidate.runtime_ = ScenarioRuntime(compile_scenario(catalog.maps.at(name), entity_ids, slots));
	}
	candidate.contract_ = candidate.runtime_.definition().signature;
	candidate.task_seed_ = candidate.runtime_.definition().task_seed;
	const auto &map = catalog.maps.at(name);
	if (map.contains("scenario"))
	{
		const auto &s = map.at("scenario");
		const auto selectors = bridge_selectors(s.value("bindings", Json::object()));
		if (s.contains("tree"))
			candidate.create_instance(s.at("tree").get<std::string>(), "main", {}, selectors);
		if (s.contains("situations"))
		{
			scenario_require(s.at("situations").is_array() && s.at("situations").size() <= 4096,
			                 "invalid authored situation configuration list");
			for (const auto &v : s.at("situations"))
			{
				const auto id = v.at("id").get<std::string>();
				scenario_require(valid_identifier(id), "invalid reusable situation identity");
				ScenarioSituationConfiguration config{v.at("tree").get<std::string>(),
				                                      bridge_parameters(v.value("parameters", Json::object())),
				                                      bridge_selectors(v.value("bindings", Json::object()))};
				candidate.validate_selectors(config.selectors);
				candidate.validate_reader(config.tree, config.selectors);
				auto parameters = candidate.runtime_.definition().parameters;
				for (const auto &[key, value] : config.parameters)
				{
					scenario_require(parameters.contains(key) && parameters.at(key).index() == value.index(),
					                 "unknown/mistyped reusable situation parameter " + key);
					parameters[key] = value;
				}
				candidate.instantiate(config.tree, parameters);
				scenario_require(candidate.situations_.emplace(id, config).second,
				                 "duplicate reusable situation configuration");
				if (v.value("start", false))
					candidate.create_instance(config.tree, v.value("instance_name", id), config.parameters,
					                          config.selectors);
			}
		}
	}
	for (const auto &[id, task] : candidate.runtime_.definition().tasks)
		for (const auto &method : task.methods)
			if (!method.situation.empty())
				scenario_require(candidate.situations_.contains(method.situation),
				                 "unknown task situation " + method.situation);
	*this = std::move(candidate);
}
state_tree::Program GameplayScenarioBridge::instantiate(const std::string &tree,
                                                        const std::map<std::string, ScenarioParameter> &values) const
{
	scenario_require(programs_.contains(tree) && programs_.at(tree).context == state_tree::Context::Scenario,
	                 "tree is not a compiled scenario program " + tree);
	if (values.empty())
		return programs_.at(tree);
	auto source = tree_sources_.at(tree);
	std::set<std::string> declared;
	std::string ancestor = tree;
	std::set<std::string> visited;
	while (tree_sources_.contains(ancestor) && visited.insert(ancestor).second)
	{
		const auto &definition = tree_sources_.at(ancestor);
		if (definition.contains("parameters"))
			for (auto i = definition.at("parameters").begin(); i != definition.at("parameters").end(); ++i)
				declared.insert(i.key());
		if (!definition.contains("extends"))
			break;
		ancestor = definition.at("extends").get<std::string>();
	}
	bool changed = false;
	for (const auto &[key, value] : values)
		if (declared.contains(key))
		{
			std::visit(
			    [&](const auto &v)
			    {
				    source["arguments"][key] = v;
			    },
			    value);
			changed = true;
		}
	return changed ? state_tree::compile(source, programs_.at(tree).registry, tree_sources_) : programs_.at(tree);
}
bool GameplayScenarioBridge::prepare_task_seed(std::uint64_t seed)
{
	if (!runtime_.definition().task_random_seed || seed < 1 || seed > 2147483647 || stepped_tick_ || task_attempt_ ||
	    !tasks_.empty())
		return false;
	task_seed_ = seed;
	return true;
}
ScenarioId GameplayScenarioBridge::create_instance(std::string tree, std::string name,
                                                   std::map<std::string, ScenarioParameter> parameters,
                                                   ScenarioTreeSelectors selectors,
                                                   std::map<std::string, EntityId> participants,
                                                   ScenarioId mission_scope)
{
	auto candidate = runtime_;
	const auto id =
	    candidate.create_instance(std::move(name), std::move(parameters), std::move(participants), mission_scope);
	validate_selectors(selectors);
	validate_reader(tree, selectors);
	ScenarioTreeController c;
	c.tree = std::move(tree);
	c.selectors = std::move(selectors);
	c.program = instantiate(c.tree, candidate.instances().at(id).parameters);
	runtime_ = std::move(candidate);
	controllers_.emplace(id, std::move(c));
	return id;
}
void GameplayScenarioBridge::validate_selectors(const ScenarioTreeSelectors &selectors) const
{
	if (!selectors.knowledge_console.empty())
		scenario_require(valid_identifier(selectors.knowledge_console) && selectors.actor.has_value(),
		                 "knowledge Console requires a valid explicit actor selector");
	if (!selectors.objective.empty())
		scenario_require(valid_identifier(selectors.objective), "invalid selected objective handle");
	if (!selectors.region.empty())
		scenario_require(runtime_.definition().region_ids.contains(selectors.region),
		                 "unknown selected region " + selectors.region);
	if (!selectors.event.empty())
		scenario_require(std::find(runtime_.definition().events.begin(), runtime_.definition().events.end(),
		                           selectors.event) != runtime_.definition().events.end(),
		                 "unknown selected event " + selectors.event);
	if (selectors.actor)
	{
		const auto &a = *selectors.actor;
		scenario_require((a.kind == ScenarioActorKind::Entity && a.entity > 0 && a.name.empty()) ||
		                     ((a.kind == ScenarioActorKind::ShipSlot || a.kind == ScenarioActorKind::Handle ||
		                       a.kind == ScenarioActorKind::Participant) &&
		                      a.entity == 0 && valid_identifier(a.name)),
		                 "invalid selected actor");
		if (a.kind == ScenarioActorKind::ShipSlot)
			scenario_require(std::find(runtime_.definition().ship_slots.begin(), runtime_.definition().ship_slots.end(),
			                           a.name) != runtime_.definition().ship_slots.end(),
			                 "unknown selected actor slot");
	}
}
void GameplayScenarioBridge::validate_reader(const std::string &tree, const ScenarioTreeSelectors &selectors) const
{
	const auto bindings = record_bindings_.find(tree);
	if (bindings != record_bindings_.end() && !bindings->second.empty())
		scenario_require(selectors.actor && !selectors.knowledge_console.empty(),
		                 "custom scenario record bindings require actor and knowledge_console selectors");
}
void GameplayScenarioBridge::begin_tick(ScenarioTick tick, std::map<std::string, EntityId> slots,
                                        std::span<const BodyView> bodies)
{
	std::map<EntityId, ScenarioPoint> positions;
	std::map<EntityId, BodyView> next_bodies;
	for (const auto &b : bodies)
	{
		scenario_require(positions.emplace(b.id, ScenarioPoint{b.x, b.y}).second, "duplicate sampled body identity");
		next_bodies.emplace(b.id, b);
	}
	runtime_.begin_tick(tick, std::move(slots), std::move(positions));
	tick_ = tick;
	bodies_ = std::move(next_bodies);
	knowledge_snapshots_.clear();
	knowledge_supplied_ = false;
	task_operators_.clear();
}
void GameplayScenarioBridge::supply_task_operators(std::span<const ScenarioTaskOperator> operators)
{
	std::map<ConsoleAddress, std::uint64_t> candidate;
	for (const auto &operator_entry : operators)
	{
		scenario_require(operator_entry.ship && valid_identifier(operator_entry.console) && operator_entry.operator_id,
		                 "invalid task participation snapshot");
		const auto [entry, inserted] =
		    candidate.emplace(ConsoleAddress{operator_entry.ship, operator_entry.console}, operator_entry.operator_id);
		scenario_require(inserted || entry->second == operator_entry.operator_id, "conflicting task Console operators");
	}
	task_operators_ = std::move(candidate);
}
std::vector<ScenarioKnowledgeReader> GameplayScenarioBridge::knowledge_readers() const
{
	std::vector<ScenarioKnowledgeReader> out;
	for (const auto &[id, controller] : controllers_)
	{
		if (runtime_.instances().at(id).finished || controller.selectors.knowledge_console.empty() ||
		    !controller.selectors.actor || record_bindings_.at(controller.tree).empty())
			continue;
		const auto ship = runtime_.resolve_actor(id, *controller.selectors.actor);
		if (!ship || !bodies_.contains(*ship))
			continue;
		const auto hull = reader_contracts_.find(bodies_.at(*ship).definition);
		if (hull == reader_contracts_.end() || !hull->second.contains(controller.selectors.knowledge_console))
			continue;
		out.push_back({id, *ship, controller.selectors.knowledge_console});
	}
	return out;
}
void GameplayScenarioBridge::supply_knowledge(std::span<const ScenarioKnowledgeSnapshot> snapshots)
{
	scenario_require(!knowledge_supplied_ && !(stepped_tick_ && *stepped_tick_ == tick_),
	                 "knowledge projection is immutable within a scenario tick");
	const auto readers = knowledge_readers();
	std::map<ScenarioId, ScenarioKnowledgeSnapshot> candidate;
	for (const auto &snapshot : snapshots)
	{
		const auto found = std::find_if(readers.begin(), readers.end(),
		                                [&](const auto &reader)
		                                {
			                                return reader.instance == snapshot.reader.instance &&
			                                       reader.ship == snapshot.reader.ship &&
			                                       reader.console == snapshot.reader.console;
		                                });
		scenario_require(found != readers.end(), "knowledge snapshot does not match an authored reader");
		scenario_require(snapshot.records.size() <= 65536 && snapshot.shared.size() <= 65536,
		                 "knowledge snapshot exceeds record limit");
		scenario_require(candidate.emplace(found->instance, snapshot).second, "duplicate knowledge snapshot");
	}
	knowledge_snapshots_ = std::move(candidate);
	knowledge_supplied_ = true;
}
std::optional<ScenarioId> GameplayScenarioBridge::selected_objective(ScenarioId id) const
{
	const auto &selector = controllers_.at(id).selectors.objective;
	if (!selector.empty())
		return runtime_.objective_handle(id, selector);
	std::optional<ScenarioId> chosen;
	for (const auto &[key, objective] : runtime_.objectives())
		if (objective.instance == id)
			chosen = key;
	return chosen;
}
std::vector<Value> GameplayScenarioBridge::snapshot(ScenarioId id, ScenarioId *new_event_cursor) const
{
	const auto &controller = controllers_.at(id);
	const auto &situation = runtime_.instances().at(id);
	std::vector<Value> out(controller.program.registry.observations.size());
	auto set = [&](GameplayTreeObservation field, Value value)
	{
		out.at(static_cast<std::size_t>(field)) = std::move(value);
	};
	set(GameplayTreeObservation::Tick, Value::integer(bounded_tick(tick_)));
	set(GameplayTreeObservation::ScenarioElapsed, Value::integer(bounded_tick(tick_ - situation.started_tick)));
	set(GameplayTreeObservation::ScenarioFinished, Value::boolean(situation.finished));
	set(GameplayTreeObservation::ObjectiveKnown, Value::boolean(false));
	std::optional<ScenarioActor> actor = controller.selectors.actor;
	std::string region = controller.selectors.region;
	if (const auto objective_id = selected_objective(id))
	{
		const auto &o = runtime_.objectives().at(*objective_id);
		set(GameplayTreeObservation::ObjectiveKnown, Value::boolean(true));
		set(GameplayTreeObservation::Objective, Value::reference(Type::Objective, o.id));
		set(GameplayTreeObservation::ObjectiveState, Value::integer(static_cast<std::int64_t>(o.status)));
		const auto r = runtime_.definition().objectives.at(o.definition).region;
		const ScenarioRegion destination = runtime_.definition().objectives.at(o.definition).condition
		                                       ? ScenarioRegion{}
		                                       : runtime_.definition().regions.at(r);
		if (!runtime_.definition().objectives.at(o.definition).condition)
			set(GameplayTreeObservation::DestinationRegion, Value::reference(Type::Region, r + 1));
		if (!runtime_.definition().objectives.at(o.definition).condition)
		{
			set(GameplayTreeObservation::DestinationX, Value::integer(destination.centre.x));
			set(GameplayTreeObservation::DestinationY, Value::integer(destination.centre.y));
		}
		if (!actor)
			actor = o.actor;
		if (region.empty())
			region = destination.id;
	}
	if (actor)
	{
		const auto entity = runtime_.resolve_actor(id, *actor);
		if (entity)
		{
			set(GameplayTreeObservation::SelfEntity, Value::reference(Type::Entity, *entity));
			if (const auto b = bodies_.find(*entity); b != bodies_.end())
			{
				set(GameplayTreeObservation::SelfDefinition, Value::string(b->second.definition));
				set(GameplayTreeObservation::PositionX, Value::integer(b->second.x));
				set(GameplayTreeObservation::PositionY, Value::integer(b->second.y));
				set(GameplayTreeObservation::VelocityX, Value::integer(b->second.vx));
				set(GameplayTreeObservation::VelocityY, Value::integer(b->second.vy));
				set(GameplayTreeObservation::Heading, Value::integer(b->second.heading));
			}
			if (!region.empty())
				set(GameplayTreeObservation::RegionActorInside,
				    Value::boolean(runtime_.actor_in_region(id, *actor, region)));
		}
	}
	if (const auto board = knowledge_snapshots_.find(id); board != knowledge_snapshots_.end())
	{
		const auto &bindings = record_bindings_.at(controller.tree);
		const auto &roles =
		    reader_contracts_.at(bodies_.at(board->second.reader.ship).definition).at(board->second.reader.console);
		for (std::size_t i = 0; i < bindings.size(); ++i)
		{
			const auto &binding = bindings[i];
			const auto schema_key = knowledge_.schema(binding.schema),
			           field_key = knowledge_.field(schema_key, binding.field);
			const auto &schema = knowledge_.schemas.at(schema_key - 1);
			if (std::none_of(roles.begin(), roles.end(),
			                 [&](auto role)
			                 {
				                 return std::find(schema.readers.begin(), schema.readers.end(), role) !=
				                        schema.readers.end();
			                 }))
				continue;
			const auto subject =
			    binding.subject_parameter.empty() ? board->second.reader.ship : std::stoull(binding.subject_parameter);
			const auto &records =
			    binding.scope == GameplayTreeRecordScope::Ship ? board->second.shared : board->second.records;
			std::set<std::pair<KnowledgeId, KnowledgeId>> streams;
			for (auto record = records.rbegin(); record != records.rend(); ++record)
			{
				if (record->value.schema != schema_key || record->value.subject != subject)
					continue;
				if (!streams.emplace(record->value.provenance.source, record->publication).second)
					continue;
				const auto observed = record->value.provenance.observed_tick;
				if (record->value.withdrawn || observed > tick_ ||
				    (schema.stale_after && tick_ - observed > schema.stale_after))
					continue;
				const auto field = record->value.fields.find(field_key);
				if (field == record->value.fields.end())
					continue;
				Value value;
				if (const auto n = std::get_if<std::int64_t>(&field->second))
					value = Value::integer(*n);
				else if (const auto b = std::get_if<bool>(&field->second))
					value = Value::boolean(*b);
				else
					value = Value::string(std::get<std::string>(field->second));
				scenario_require(value.type == binding.type,
				                 "knowledge projection field type differs from compiled binding");
				out.at(static_cast<std::size_t>(GameplayTreeObservation::Count) + i) = std::move(value);
				break;
			}
		}
	}
	if (const auto bindings = fact_bindings_.find(controller.tree); bindings != fact_bindings_.end())
		for (const auto &[index, binding] : bindings->second)
		{
			const auto scope = task_instance_facts_.find(id);
			if (scope == task_instance_facts_.end())
				continue;
			const auto fact = scope->second.find(binding.fact);
			if (fact != scope->second.end() && fact->second.type == binding.type)
				out.at(index) = fact->second;
		}
	set(GameplayTreeObservation::ResultAvailable, Value::boolean(false));
	if (controller.last_receipt)
		if (const auto r = receipt(*controller.last_receipt))
		{
			set(GameplayTreeObservation::ResultAvailable,
			    Value::boolean(r->result.status != ScenarioActionStatus::Pending));
			set(GameplayTreeObservation::ResultRequest, Value::string(r->request));
			set(GameplayTreeObservation::ResultSucceeded,
			    Value::boolean(r->result.status == ScenarioActionStatus::Succeeded));
			set(GameplayTreeObservation::ResultReason, Value::string(r->result.reason));
			set(GameplayTreeObservation::ResultInteger, Value::integer(bounded_tick(r->result.value)));
			if (r->result.entities.size() == 1)
				set(GameplayTreeObservation::ResultEntity, Value::reference(Type::Entity, r->result.entities.front()));
			if (r->action == GameplayTreeAction::AddObjective && r->result.status == ScenarioActionStatus::Succeeded)
				set(GameplayTreeObservation::ResultObjective, Value::reference(Type::Objective, r->result.value));
			if ((r->action == GameplayTreeAction::PublishObjective ||
			     r->action == GameplayTreeAction::PublishScenarioMessage) &&
			    r->result.status == ScenarioActionStatus::Succeeded)
				set(GameplayTreeObservation::ResultPublication, Value::integer(bounded_tick(r->result.value)));
		}
	set(GameplayTreeObservation::EventAvailable, Value::boolean(false));
	std::optional<ScenarioEvent> event;
	for (const auto &name : runtime_.definition().events)
	{
		if (!controller.selectors.event.empty() && name != controller.selectors.event)
			continue;
		for (const auto &e : runtime_.events(name, controller.event_cursor))
			if (!event || e.id < event->id)
				event = e;
	}
	if (event)
	{
		set(GameplayTreeObservation::EventAvailable, Value::boolean(true));
		set(GameplayTreeObservation::EventName, Value::string(event->name));
		set(GameplayTreeObservation::EventSource, Value::integer(bounded_tick(event->source_instance)));
		auto scalar = [&](const char *key, GameplayTreeObservation field)
		{
			auto v = event->payload.find(key);
			if (v == event->payload.end())
				return;
			std::visit(
			    [&](const auto &value)
			    {
				    using T = std::decay_t<decltype(value)>;
				    if constexpr (std::is_same_v<T, bool>)
					    set(field, Value::boolean(value));
				    else if constexpr (std::is_same_v<T, std::string>)
					    set(field, Value::string(value));
				    else
					    set(field, Value::integer(value));
			    },
			    v->second);
		};
		scalar("amount", GameplayTreeObservation::EventInteger);
		scalar("flag", GameplayTreeObservation::EventBoolean);
		scalar("detail", GameplayTreeObservation::EventText);
		if (new_event_cursor)
			*new_event_cursor = event->id;
	}
	return out;
}
std::vector<Value> GameplayScenarioBridge::observations(ScenarioId id) const
{
	return snapshot(id, nullptr);
}
void GameplayScenarioBridge::step(std::uint64_t ticks)
{
	if (stepped_tick_ && *stepped_tick_ == tick_)
		return;
	// All domain effects remain queued here; physical World construction happens
	// only after success. Roll back decision continuations and admitted effects
	// together if a bounded journal/evaluator refuses the complete tick.
	auto old_runtime = runtime_;
	auto old_controllers = controllers_;
	auto old_receipts = receipts_;
	auto old_effect_requests = effect_requests_;
	auto old_tasks = tasks_;
	auto old_planned_facts = planned_facts_;
	auto old_planned_instance_facts = planned_instance_facts_;
	const auto old_task_attempt = task_attempt_;
	const auto old_task_diagnostic = task_diagnostic_;
	const auto old_stepped = stepped_tick_;
	try
	{
		step_transaction(ticks);
	}
	catch (...)
	{
		tasks_ = std::move(old_tasks);
		planned_facts_ = std::move(old_planned_facts);
		planned_instance_facts_ = std::move(old_planned_instance_facts);
		task_attempt_ = old_task_attempt;
		task_diagnostic_ = old_task_diagnostic;
		runtime_ = std::move(old_runtime);
		controllers_ = std::move(old_controllers);
		receipts_ = std::move(old_receipts);
		effect_requests_ = std::move(old_effect_requests);
		stepped_tick_ = old_stepped;
		throw;
	}
}
void GameplayScenarioBridge::step_transaction(std::uint64_t ticks)
{
	advance_tasks();
	std::map<ScenarioId, std::vector<Value>> snapshots;
	std::map<ScenarioId, ScenarioId> cursors;
	for (const auto &[id, c] : controllers_)
		if (!runtime_.instances().at(id).finished && task_controller_active(id))
		{
			auto cursor = c.event_cursor;
			snapshots.emplace(id, snapshot(id, &cursor));
			cursors.emplace(id, cursor);
		}
	struct Effect
	{
		ScenarioId instance;
		state_tree::Output output;
	};
	std::vector<Effect> effects;
	// Evaluator failure cannot commit another situation's partial continuation.
	for (auto &[id, c] : controllers_)
	{
		if (!snapshots.contains(id))
			continue;
		auto result = state_tree::step(c.program, c.continuation, snapshots.at(id), ticks);
		c.event_cursor = cursors.at(id);
		c.debug = std::move(result.debug);
		for (auto &output : result.outputs)
			effects.push_back({id, std::move(output)});
	}
	for (const auto &effect : effects)
		execute(effect.instance, effect.output);
	stepped_tick_ = tick_;
}
ScenarioTreeReceipt GameplayScenarioBridge::execute(ScenarioId id, const state_tree::Output &output)
{
	return execute({id, output.activation, output.task, output.call_path, output.call_activations},
	               static_cast<GameplayTreeAction>(output.action), output.arguments);
}
ScenarioTreeReceipt GameplayScenarioBridge::execute(ScenarioActionToken token, GameplayTreeAction action,
                                                    std::vector<Value> arguments)
{
	scenario_require(controllers_.contains(token.instance), "effect has no bound situation controller");
	scenario_require(token.activation > 0 && token.call_path.size() == token.call_activations.size() &&
	                     token.call_path.size() <= 64,
	                 "invalid effect identity/call ancestry");
	for (const auto activation : token.call_activations)
		scenario_require(activation > 0, "effect ancestor activation is zero");
	const auto action_id = static_cast<std::size_t>(action);
	const auto &registry = controllers_.at(token.instance).program.registry;
	scenario_require(action_id < registry.actions.size(), "unsupported action index");
	const auto &spec = registry.actions[action_id];
	scenario_require((spec.contexts & 2) && spec.effect && spec.arguments.size() == arguments.size(),
	                 "effect outside scenario context or wrong argument count");
	for (std::size_t i = 0; i < arguments.size(); ++i)
		scenario_require(bridge_value_valid(arguments[i], spec.arguments[i]), "effect argument type mismatch");
	const auto fingerprint = bridge_fingerprint(action, arguments);
	if (auto found = receipts_.find(token); found != receipts_.end())
	{
		scenario_require(found->second.result.fingerprint == domain_fingerprint_prefix + fingerprint,
		                 "effect identity reused with different typed arguments");
		return *receipt(token);
	}
	scenario_require(receipts_.size() < runtime_.definition().maximum_actions,
	                 "scenario receipt journal limit reached");
	ScenarioTreeReceipt out;
	out.token = token;
	out.action = action;
	out.request = bridge_request(token);
	out.result.kind = bridge_action_kind(action);
	const auto integer = [&](std::size_t i)
	{
		return arguments.at(i).number;
	};
	const auto string = [&](std::size_t i) -> const std::string &
	{
		return arguments.at(i).text;
	};
	const auto boolean = [&](std::size_t i)
	{
		return arguments.at(i).number != 0;
	};
	try
	{
		switch (action)
		{
		case GameplayTreeAction::ApplyShieldEffect:
		case GameplayTreeAction::RefreshShieldEffect:
		case GameplayTreeAction::RemoveShieldEffect:
		case GameplayTreeAction::ApplyModifierEffect:
		case GameplayTreeAction::RefreshModifierEffect:
		case GameplayTreeAction::RemoveModifierEffect:
		case GameplayTreeAction::ApplyEffectGroup:
			effect_requests_.emplace(token, effect_request(token, action, arguments));
			out.result.status = ScenarioActionStatus::Pending;
			break;
		case GameplayTreeAction::SpawnEntity:
		{
			ScenarioPoint p{integer(2), integer(3)};
			if (!string(1).empty())
			{
				const auto found = runtime_.definition().spawn_points.find(string(1));
				scenario_require(found != runtime_.definition().spawn_points.end(), "unknown named spawn point");
				scenario_require(p.x >= -scenario_coordinate_limit && p.x <= scenario_coordinate_limit &&
				                     p.y >= -scenario_coordinate_limit && p.y <= scenario_coordinate_limit,
				                 "spawn offset outside coordinate range");
				p.x += found->second.x;
				p.y += found->second.y;
			}
			out.result = runtime_.spawn_entity(token, string(0), p, integer(4), string(5));
			out.request = string(5);
			break;
		}
		case GameplayTreeAction::SpawnGroup:
			out.result = runtime_.spawn_group(token, string(0), {}, string(1));
			out.request = string(1);
			break;
		case GameplayTreeAction::AddObjective:
			out.result =
			    runtime_.add_objective(token, string(0), string(2),
			                           ScenarioActor{ScenarioActorKind::Entity, static_cast<EntityId>(integer(1)), {}});
			out.request = string(2);
			break;
		case GameplayTreeAction::StartObjectiveSequence:
			out.result = runtime_.add_objective(
			    token, {}, string(2), ScenarioActor{ScenarioActorKind::Entity, static_cast<EntityId>(integer(1)), {}},
			    true, string(0));
			out.request = string(2);
			break;
		case GameplayTreeAction::PublishObjective:
			out.result = runtime_.publish_objective(token, static_cast<ScenarioId>(integer(0)));
			break;
		case GameplayTreeAction::SetObjectiveStatus:
			scenario_require(integer(1) >= 0 && integer(1) <= static_cast<std::int64_t>(ObjectiveStatus::Cancelled),
			                 "objective status outside lifecycle vocabulary");
			out.result = runtime_.set_objective_status(token, static_cast<ScenarioId>(integer(0)),
			                                           static_cast<ObjectiveStatus>(integer(1)));
			break;
		case GameplayTreeAction::PublishScenarioMessage:
		{
			ScenarioRecipients recipients;
			recipients.ship = {ScenarioActorKind::Entity, static_cast<EntityId>(integer(1)), {}};
			out.result = runtime_.publish(token, string(0), std::move(recipients),
			                              {{"amount", integer(2)}, {"flag", boolean(3)}, {"detail", string(4)}});
			break;
		}
		case GameplayTreeAction::EmitScenarioEvent:
			out.result =
			    runtime_.emit(token, string(0), {{"amount", integer(1)}, {"flag", boolean(2)}, {"detail", string(3)}});
			break;
		case GameplayTreeAction::FinishScenario:
			out.result = runtime_.finish(token, boolean(0), string(1));
			break;
		case GameplayTreeAction::StartSituation:
		{
			const auto configuration = situations_.find(string(0));
			scenario_require(configuration != situations_.end(), "unknown reusable situation configuration");
			const auto &config = configuration->second;
			auto parameters = runtime_.definition().parameters;
			for (const auto &[key, value] : config.parameters)
				parameters[key] = value;
			auto program = instantiate(config.tree, parameters);
			out.result = runtime_.start_situation(token, string(0), string(1), config.parameters);
			if (out.result.status == ScenarioActionStatus::Succeeded)
			{
				ScenarioTreeController controller;
				controller.tree = config.tree;
				controller.program = std::move(program);
				controller.selectors = config.selectors;
				controllers_.emplace(out.result.value, std::move(controller));
			}
			break;
		}
		case GameplayTreeAction::RequestTask:
		{
			scenario_require(runtime_.definition().tasks.contains(string(0)) && valid_identifier(string(1)) &&
			                     string(1).find('.') == std::string::npos,
			                 "unknown task or invalid request handle");
			const auto path = "request." + std::to_string(token.instance) + "." + string(1);
			const auto existing = std::find_if(tasks_.begin(), tasks_.end(),
			                                   [&](const auto &node)
			                                   {
				                                   return node.path == path;
			                                   });
			if (existing != tasks_.end())
			{
				scenario_require(existing->task == string(0), "task handle reused with a different definition");
				out.result.value = existing->request_id;
			}
			else
			{
				const auto facts = task_instance_facts_.find(token.instance);
				auto plan =
				    plan_scenario_task(runtime_.definition(), string(0),
				                       facts == task_instance_facts_.end() ? task_facts_ : facts->second, task_seed_);
				scenario_require(plan.feasible, plan.reason);
				scenario_require(tasks_.size() + plan.nodes.size() <= runtime_.definition().task_nodes &&
				                     task_attempt_ < runtime_.definition().maximum_instances,
				                 "task population/attempt limit reached");
				out.result.value = ++task_attempt_;
				for (auto &node : plan.nodes)
				{
					node.path.replace(0, 4, path);
					if (!node.parent.empty())
						node.parent.replace(0, 4, path);
					for (auto &dependency : node.after)
						dependency.replace(0, 4, path);
					node.attempt = task_attempt_;
					node.request_id = task_attempt_;
					node.scope = token.instance;
					tasks_.push_back(std::move(node));
				}
			}
			out.result.status = ScenarioActionStatus::Succeeded;
			out.result.handle = string(1);
			break;
		}
		default:
			throw std::runtime_error("unsupported scenario effect");
		}
	}
	catch (const std::exception &e)
	{
		out.result.status = ScenarioActionStatus::Failed;
		out.result.reason = e.what();
	}
	// Adapter argument receipt is independent of the domain's resolved-argument
	// fingerprint (for example named spawn point + offset -> physical transform).
	out.result.fingerprint = domain_fingerprint_prefix + fingerprint;
	receipts_.emplace(token, out);
	controllers_.at(token.instance).last_receipt = token;
	return out;
}
std::optional<ScenarioTreeReceipt> GameplayScenarioBridge::receipt(ScenarioActionToken token) const
{
	const auto found = receipts_.find(token);
	if (found == receipts_.end())
		return {};
	auto out = found->second;
	if (const auto *current = runtime_.result(token))
	{
		const auto fp = out.result.fingerprint;
		out.result = *current;
		out.result.fingerprint = fp;
	}
	return out;
}
std::vector<ScenarioEffectRequest> GameplayScenarioBridge::pending_effects() const
{
	std::vector<ScenarioEffectRequest> out;
	for (const auto &[token, request] : effect_requests_)
		out.push_back(request);
	// Preserve the established physical phase order across the common queue;
	// token ordering remains stable within each family.
	std::stable_sort(out.begin(), out.end(),
	                 [](const auto &a, const auto &b)
	                 {
		                 return a.kind < b.kind;
	                 });
	return out;
}
void GameplayScenarioBridge::confirm_effect(ScenarioActionToken token, std::string failure)
{
	scenario_require(effect_requests_.contains(token), "effect request not pending");
	auto &result = receipts_.at(token).result;
	result.status = failure.empty() ? ScenarioActionStatus::Succeeded : ScenarioActionStatus::Failed;
	result.reason = std::move(failure);
	effect_requests_.erase(token);
}
std::vector<ScenarioSpawnRequest> GameplayScenarioBridge::pending_spawns() const
{
	return runtime_.pending_spawns();
}
void GameplayScenarioBridge::confirm_spawn(ScenarioId request, std::span<const EntityId> ids, std::string failure)
{
	const auto requests = runtime_.pending_spawns();
	const auto found = std::find_if(requests.begin(), requests.end(),
	                                [&](const auto &r)
	                                {
		                                return r.id == request;
	                                });
	scenario_require(found != requests.end(), "spawn request not pending");
	runtime_.confirm_spawn(request, ids, std::move(failure));
	for (std::size_t i = 0; i < ids.size(); ++i)
	{
		BodyView body{};
		body.id = ids[i];
		body.definition = found->entities[i].definition;
		body.x = found->entities[i].position.x;
		body.y = found->entities[i].position.y;
		body.heading = found->entities[i].heading;
		bodies_.emplace(body.id, std::move(body));
	}
}
std::vector<ScenarioMotionSegment> GameplayScenarioBridge::sample_motion(std::span<const BodyView> bodies) const
{
	std::vector<ScenarioMotionSegment> out;
	out.reserve(bodies.size());
	for (const auto &b : bodies)
	{
		const auto prior = bodies_.find(b.id);
		out.push_back(
		    {b.id,
		     prior == bodies_.end() ? ScenarioPoint{b.x, b.y} : ScenarioPoint{prior->second.x, prior->second.y},
		     {b.x, b.y}});
	}
	return out;
}
void GameplayScenarioBridge::synchronize_time(ScenarioTick tick)
{
	runtime_.synchronize_time(tick);
	tick_ = tick;
}
void GameplayScenarioBridge::evaluate_conditions(ScenarioTick tick,
                                                 const std::map<ScenarioId, ScenarioFacts> &snapshots)
{
	runtime_.evaluate_conditions(tick, snapshots);
	if (!runtime_.definition().task_root.empty() && !snapshots.empty())
	{
		task_instance_facts_ = snapshots;
		auto main = snapshots.begin();
		for (auto s = snapshots.begin(); s != snapshots.end(); ++s)
			if (runtime_.instances().at(s->first).name == "main")
				main = s;
		task_facts_ = main->second;
	}
	tick_ = tick;
}
void GameplayScenarioBridge::evaluate(std::span<const ScenarioMotionSegment> motion)
{
	runtime_.evaluate(motion);
}
ScenarioRuntime &GameplayScenarioBridge::runtime()
{
	return runtime_;
}
const ScenarioRuntime &GameplayScenarioBridge::runtime() const
{
	return runtime_;
}
const std::map<ScenarioId, ScenarioTreeController> &GameplayScenarioBridge::controllers() const
{
	return controllers_;
}
const std::vector<ScenarioTaskNode> &GameplayScenarioBridge::tasks() const
{
	return tasks_;
}
bool GameplayScenarioBridge::task_controller_active(ScenarioId instance) const
{
	if (runtime_.definition().task_root.empty())
		return true;
	for (const auto &node : tasks_)
		if (node.situation == instance)
			return node.status == ScenarioTaskStatus::Running;
	return !runtime_.instances().at(instance).name.starts_with("task.");
}
void GameplayScenarioBridge::advance_tasks()
{
	const auto &definition = runtime_.definition();
	if (definition.task_root.empty())
		return;
	const bool has_root = std::any_of(tasks_.begin(), tasks_.end(),
	                                  [](const auto &node)
	                                  {
		                                  return node.path == "root";
	                                  });
	if (!has_root &&
	    (!task_attempt_ || planned_facts_ != task_facts_ || planned_instance_facts_ != task_instance_facts_))
	{
		if (task_attempt_ >= definition.maximum_instances)
			task_diagnostic_ = "task attempt limit reached";
		else
		{
			auto plan = plan_scenario_task(definition, definition.task_root, task_facts_, task_seed_);
			++task_attempt_;
			planned_facts_ = task_facts_;
			planned_instance_facts_ = task_instance_facts_;
			task_diagnostic_ = plan.reason;
			if (plan.feasible && tasks_.size() + plan.nodes.size() <= definition.task_nodes)
				tasks_.insert(tasks_.end(), std::make_move_iterator(plan.nodes.begin()),
				              std::make_move_iterator(plan.nodes.end()));
			else if (plan.feasible)
				task_diagnostic_ = "task node limit reached";
		}
	}
	// Refresh task outcomes from the committed boundary before dispatching work.
	// A failed physical condition wins a simultaneous successful condition.
	for (auto &node : tasks_)
	{
		if (node.status != ScenarioTaskStatus::Running && node.status != ScenarioTaskStatus::Waiting)
			continue;
		const auto &task = definition.tasks.at(node.task);
		const auto scoped = task_instance_facts_.find(node.situation);
		const auto &facts = scoped == task_instance_facts_.end() ? task_facts_ : scoped->second;
		const auto &methods = task.methods;
		const auto method = std::find_if(methods.begin(), methods.end(),
		                                 [&](const auto &m)
		                                 {
			                                 return m.id == node.method;
		                                 });
		if (task.fail_when && scenario_condition(*task.fail_when, facts).value_or(false))
		{
			node.status = ScenarioTaskStatus::Failed;
			node.reason = "physical_failure";
		}
		else if (task.complete_when && scenario_condition(*task.complete_when, facts).value_or(false))
			node.status = ScenarioTaskStatus::Succeeded;
		else if (method->maintain && !scenario_condition(*method->maintain, facts).value_or(false))
		{
			node.status = ScenarioTaskStatus::Invalidated;
			node.reason = "method_no_longer_feasible";
		}
		else if (node.situation && runtime_.instances().at(node.situation).finished)
		{
			const auto &situation = runtime_.instances().at(node.situation);
			if (!situation.succeeded)
			{
				node.status = ScenarioTaskStatus::Invalidated;
				node.reason = situation.finish_reason;
			}
			else if (!task.complete_when)
				node.status = ScenarioTaskStatus::Succeeded;
		}
	}
	// A terminal parent retires pending decisions, never already admitted work.
	for (auto &node : tasks_)
	{
		if (node.status != ScenarioTaskStatus::Running && node.status != ScenarioTaskStatus::Waiting &&
		    node.status != ScenarioTaskStatus::Invalidated)
			continue;
		if (node.parent.empty())
			continue;
		const auto parent = std::find_if(tasks_.begin(), tasks_.end(),
		                                 [&](const auto &other)
		                                 {
			                                 return other.path == node.parent;
		                                 });
		if (parent != tasks_.end() &&
		    (parent->status == ScenarioTaskStatus::Failed || parent->status == ScenarioTaskStatus::Succeeded ||
		     parent->status == ScenarioTaskStatus::Superseded))
		{
			node.status = ScenarioTaskStatus::Superseded;
			node.reason = "parent_resolved";
		}
	}
	// Replan only the invalidated subtree. Existing successful work, siblings and
	// admitted physical operations remain intact; old tree instances retain receipts.
	if (planned_facts_ != task_facts_ || planned_instance_facts_ != task_instance_facts_)
	{
		for (std::size_t i = 0; i < tasks_.size(); ++i)
		{
			if (tasks_[i].status != ScenarioTaskStatus::Invalidated)
				continue;
			const auto previous = tasks_[i];
			const auto scoped = task_instance_facts_.find(previous.situation);
			auto plan =
			    plan_scenario_task(definition, previous.task,
			                       scoped == task_instance_facts_.end() ? task_facts_ : scoped->second, task_seed_);
			task_diagnostic_ = plan.reason;
			if (!plan.feasible)
				continue;
			const auto additional = std::count_if(plan.nodes.begin(), plan.nodes.end(),
			                                      [&](const auto &fresh)
			                                      {
				                                      auto path = fresh.path;
				                                      path.replace(0, 4, previous.path);
				                                      return std::none_of(tasks_.begin(), tasks_.end(),
				                                                          [&](const auto &old)
				                                                          {
					                                                          return old.path == path;
				                                                          });
			                                      });
			if (tasks_.size() + additional > definition.task_nodes)
			{
				task_diagnostic_ = "task node limit reached";
				continue;
			}
			if (task_attempt_ >= definition.maximum_instances)
			{
				task_diagnostic_ = "task attempt limit reached";
				continue;
			}
			++task_attempt_;
			for (auto &old : tasks_)
				if (old.path.starts_with(previous.path + ".") && old.status != ScenarioTaskStatus::Succeeded)
					old.status = ScenarioTaskStatus::Superseded;
			for (auto &fresh : plan.nodes)
			{
				fresh.path.replace(0, 4, previous.path);
				if (!fresh.parent.empty())
					fresh.parent.replace(0, 4, previous.path);
				else
					fresh.parent = previous.parent;
				for (auto &after : fresh.after)
					after.replace(0, 4, previous.path);
				fresh.attempt = task_attempt_;
				fresh.scope = previous.scope;
				fresh.request_id = previous.request_id;
				const auto old = std::find_if(tasks_.begin(), tasks_.end(),
				                              [&](const auto &n)
				                              {
					                              return n.path == fresh.path;
				                              });
				if (old != tasks_.end())
				{
					if (old->status == ScenarioTaskStatus::Succeeded && old->task == fresh.task)
						continue;
					if (fresh.path == previous.path)
						fresh.after = old->after;
					fresh.released = old->released;
					fresh.release_tick = old->release_tick;
					*old = std::move(fresh);
				}
				else
				{
					scenario_require(tasks_.size() < definition.task_nodes, "task node limit reached");
					tasks_.push_back(std::move(fresh));
				}
			}
		}
		planned_facts_ = task_facts_;
		planned_instance_facts_ = task_instance_facts_;
	}
	const auto attention = [&](const ScenarioTaskNode &node, const ScenarioTaskMethod &method)
	{
		std::map<std::uint64_t, std::int64_t> demands;
		const auto &task = definition.tasks.at(node.task);
		const auto scope = node.situation ? node.situation
		                   : node.scope   ? node.scope
		                                  : runtime_.instances().begin()->first;
		for (const auto &demand : method.attention)
		{
			std::optional<EntityId> entity;
			if (node.situation)
			{
				const auto &roles = runtime_.instances().at(node.situation).participants;
				if (const auto role = roles.find(demand.participant); role != roles.end())
					entity = role->second;
			}
			else
			{
				const auto &selector = task.participants.at(demand.participant);
				if (selector.kind == ScenarioActorKind::Fact)
				{
					const auto scoped = task_instance_facts_.find(scope);
					const auto &facts = scoped == task_instance_facts_.end() ? task_facts_ : scoped->second;
					if (const auto value = facts.find(selector.name); value != facts.end() &&
					                                                  value->second.type == state_tree::Type::Entity &&
					                                                  value->second.number > 0)
						entity = static_cast<EntityId>(value->second.number);
				}
				else
					entity = runtime_.resolve_actor(scope, selector);
			}
			if (!entity)
				continue;
			const auto operator_entry = task_operators_.find({*entity, demand.console});
			if (operator_entry != task_operators_.end())
				demands[operator_entry->second] += demand.amount;
		}
		return demands;
	};
	std::map<std::uint64_t, std::int64_t> committed_attention;
	for (const auto &node : tasks_)
		if (node.status == ScenarioTaskStatus::Running)
		{
			const auto &methods = definition.tasks.at(node.task).methods;
			const auto &method = *std::find_if(methods.begin(), methods.end(),
			                                   [&](const auto &m)
			                                   {
				                                   return m.id == node.method;
			                                   });
			for (const auto &[person, amount] : attention(node, method))
				committed_attention[person] += amount;
		}
	for (auto &node : tasks_)
	{
		if (node.status != ScenarioTaskStatus::Waiting)
			continue;
		bool ready = true;
		if (!node.parent.empty())
		{
			const auto parent = std::find_if(tasks_.begin(), tasks_.end(),
			                                 [&](const auto &n)
			                                 {
				                                 return n.path == node.parent;
			                                 });
			ready = parent != tasks_.end() && parent->status == ScenarioTaskStatus::Running;
		}
		for (const auto &dependency : node.after)
		{
			const auto predecessor = std::find_if(tasks_.begin(), tasks_.end(),
			                                      [&](const auto &n)
			                                      {
				                                      return n.path == dependency;
			                                      });
			ready &= predecessor != tasks_.end() && predecessor->status == ScenarioTaskStatus::Succeeded;
			if (predecessor != tasks_.end() && predecessor->status == ScenarioTaskStatus::Failed)
			{
				node.status = ScenarioTaskStatus::Failed;
				node.reason = "predecessor_failed";
			}
		}
		if (!ready)
			continue;
		const auto &methods = definition.tasks.at(node.task).methods;
		const auto &method = *std::find_if(methods.begin(), methods.end(),
		                                   [&](const auto &m)
		                                   {
			                                   return m.id == node.method;
		                                   });
		if (!node.released)
		{
			const auto facts = task_instance_facts_.find(node.scope);
			if (method.release_when &&
			    !scenario_condition(*method.release_when,
			                        facts == task_instance_facts_.end() ? task_facts_ : facts->second)
			         .value_or(false))
				continue;
			node.release_tick = tick_;
			node.released = true;
		}
		const auto elapsed = tick_ - node.release_tick;
		if (elapsed < static_cast<std::uint64_t>(method.earliest))
			continue;
		const auto demand = attention(node, method);
		const bool overloaded = std::any_of(demand.begin(), demand.end(),
		                                    [&](const auto &entry)
		                                    {
			                                    return committed_attention[entry.first] + entry.second > 100;
		                                    });
		if (overloaded && method.latest && elapsed < static_cast<std::uint64_t>(method.latest))
			continue;
		if (overloaded && method.optional)
		{
			node.status = ScenarioTaskStatus::Failed;
			node.reason = "optional_work_exceeds_attention_forecast";
			continue;
		}
		if (!method.situation.empty())
		{
			if (runtime_.instances().size() >= definition.maximum_instances)
			{
				node.status = ScenarioTaskStatus::Failed;
				node.reason = "situation_instance_limit_reached";
				continue;
			}
			const auto &configuration = situations_.at(method.situation);
			std::map<std::string, EntityId> participants;
			const auto scope = node.scope ? node.scope : runtime_.instances().begin()->first;
			for (const auto &[role, selector] : definition.tasks.at(node.task).participants)
			{
				std::optional<EntityId> entity;
				if (selector.kind == ScenarioActorKind::Fact)
				{
					const auto facts = task_instance_facts_.find(scope);
					if (facts != task_instance_facts_.end())
						if (const auto value = facts->second.find(selector.name);
						    value != facts->second.end() && value->second.type == state_tree::Type::Entity &&
						    value->second.number > 0)
							entity = static_cast<EntityId>(value->second.number);
				}
				else
					entity = runtime_.resolve_actor(scope, selector);
				if (!entity)
				{
					ready = false;
					node.reason = "participant_unavailable:" + role;
					break;
				}
				participants.emplace(role, *entity);
			}
			if (!ready)
				continue;
			node.situation = create_instance(
			    configuration.tree, "task." + node.path + "." + std::to_string(node.attempt), configuration.parameters,
			    configuration.selectors, std::move(participants), runtime_.instances().at(scope).mission_scope);
		}
		node.status = ScenarioTaskStatus::Running;
		for (const auto &[person, amount] : demand)
			committed_attention[person] += amount;
	}
	for (auto n = tasks_.rbegin(); n != tasks_.rend(); ++n)
	{
		if (n->status != ScenarioTaskStatus::Running || n->situation)
			continue;
		if (definition.tasks.at(n->task).complete_when)
			continue;
		bool finished = true, failed = false;
		for (const auto &child : tasks_)
			if (child.parent == n->path && child.status != ScenarioTaskStatus::Superseded)
			{
				finished &= child.status == ScenarioTaskStatus::Succeeded || child.status == ScenarioTaskStatus::Failed;
				const auto &methods = definition.tasks.at(child.task).methods;
				const auto selected = std::find_if(methods.begin(), methods.end(),
				                                   [&](const auto &method)
				                                   {
					                                   return method.id == child.method;
				                                   });
				failed |= child.status == ScenarioTaskStatus::Failed && !selected->optional;
			}
		if (finished)
			n->status = failed ? ScenarioTaskStatus::Failed : ScenarioTaskStatus::Succeeded;
	}
}
Json GameplayScenarioBridge::save() const
{
	Json out{{"version", 1},
	         {"map", map_},
	         {"contract", contract_},
	         {"runtime", runtime_.save()},
	         {"tick", tick_},
	         {"controllers", Json::array()},
	         {"receipts", Json::array()},
	         {"bodies", Json::array()}};
	if (stepped_tick_)
		out["stepped"] = *stepped_tick_;
	if (!runtime_.definition().task_root.empty())
	{
		auto encode_facts = [](const ScenarioFacts &facts)
		{
			Json result = Json::object();
			for (const auto &[key, value] : facts)
				result[key] = {
				    {"type", static_cast<unsigned>(value.type)}, {"number", value.number}, {"text", value.text}};
			return result;
		};
		Json nodes = Json::array();
		for (const auto &node : tasks_)
			nodes.push_back({{"path", node.path},
			                 {"parent", node.parent},
			                 {"scope", node.scope},
			                 {"release_tick", node.release_tick},
			                 {"released", node.released},
			                 {"task", node.task},
			                 {"method", node.method},
			                 {"reason", node.reason},
			                 {"after", node.after},
			                 {"situation", node.situation},
			                 {"attempt", node.attempt},
			                 {"request_id", node.request_id},
			                 {"status", static_cast<unsigned>(node.status)}});
		out["task_plan"] = {{"version", 1},
		                    {"seed", task_seed_},
		                    {"nodes", std::move(nodes)},
		                    {"attempt", task_attempt_},
		                    {"facts", encode_facts(task_facts_)},
		                    {"planned_facts", encode_facts(planned_facts_)},
		                    {"diagnostic", task_diagnostic_}};
		out["task_plan"]["instance_facts"] = Json::array();
		for (const auto &[id, facts] : task_instance_facts_)
			out["task_plan"]["instance_facts"].push_back({{"id", id}, {"facts", encode_facts(facts)}});
		out["task_plan"]["planned_instance_facts"] = Json::array();
		for (const auto &[id, facts] : planned_instance_facts_)
			out["task_plan"]["planned_instance_facts"].push_back({{"id", id}, {"facts", encode_facts(facts)}});
	}
	for (const auto &[id, c] : controllers_)
	{
		Json j{{"id", id},
		       {"tree", c.tree},
		       {"bindings", bridge_selectors_json(c.selectors)},
		       {"cursor", c.event_cursor},
		       {"continuation", state_tree::save(c.program, c.continuation)}};
		if (c.last_receipt)
			j["receipt"] = bridge_token_json(*c.last_receipt);
		out["controllers"].push_back(std::move(j));
	}
	for (const auto &[token, old] : receipts_)
	{
		const auto current = *receipt(token);
		const auto &r = current.result;
		out["receipts"].push_back({{"token", bridge_token_json(token)},
		                           {"action", static_cast<unsigned>(current.action)},
		                           {"request", current.request},
		                           {"kind", static_cast<unsigned>(r.kind)},
		                           {"status", static_cast<unsigned>(r.status)},
		                           {"value", r.value},
		                           {"reason", r.reason},
		                           {"fingerprint", r.fingerprint},
		                           {"handle", r.handle},
		                           {"entities", r.entities}});
	}
	// Preserve the version-one wire representation while ownership and lifecycle
	// use one typed queue. These arrays are serialization adapters only.
	for (const auto *field : {"shield_requests", "modifier_requests", "group_requests"})
		out[field] = Json::array();
	for (const auto &[token, request] : effect_requests_)
	{
		Json row{{"token", bridge_token_json(token)}, {"target", request.target}, {"definition", request.definition}};
		const auto *field = request.kind == ScenarioEffectKind::Shield     ? "shield_requests"
		                    : request.kind == ScenarioEffectKind::Modifier ? "modifier_requests"
		                                                                   : "group_requests";
		if (request.kind == ScenarioEffectKind::Group)
			row["source"] = request.source;
		else
		{
			row["operation"] = static_cast<unsigned>(request.operation);
			row["grant"] = request.grant;
			if (request.kind == ScenarioEffectKind::Modifier)
				row["equipment"] = request.equipment;
		}
		out[field].push_back(std::move(row));
	}
	for (const auto &[id, b] : bodies_)
		out["bodies"].push_back({{"id", id},
		                         {"definition", b.definition},
		                         {"x", b.x},
		                         {"y", b.y},
		                         {"vx", b.vx},
		                         {"vy", b.vy},
		                         {"heading", b.heading}});
	return out;
}
void GameplayScenarioBridge::restore(const Catalog &catalog, const std::string &map, const Json &saved,
                                     ScenarioId event_source, SharedPropertyCatalog properties)
{
	if (!properties)
		properties = compile_property_catalog(catalog);
	GameplayScenarioBridge candidate;
	candidate.load(catalog, map, properties);
	scenario_require(saved.at("version") == 1 && saved.at("map") == map && saved.at("contract") == candidate.contract_,
	                 "incompatible scenario adapter continuation");
	candidate.runtime_.restore(saved.at("runtime"));
	candidate.tick_ = scenario_unsigned(saved.at("tick"));
	scenario_require(saved.at("runtime").at("tick") == candidate.tick_, "adapter/runtime saved tick mismatch");
	if (saved.contains("stepped"))
	{
		candidate.stepped_tick_ = scenario_unsigned(saved.at("stepped"));
		scenario_require(*candidate.stepped_tick_ <= candidate.tick_, "saved controller tick is from future");
	}
	candidate.controllers_.clear();
	candidate.receipts_.clear();
	candidate.bodies_.clear();
	if (event_source)
		scenario_require(candidate.runtime_.instances().contains(event_source), "saved event source missing");
	scenario_require(saved.at("controllers").is_array() && saved.at("controllers").size() + (event_source ? 1 : 0) ==
	                                                           candidate.runtime_.instances().size(),
	                 "saved controller/situation count mismatch");
	for (const auto &j : saved.at("controllers"))
	{
		const auto id = scenario_unsigned(j.at("id"));
		scenario_require(id != event_source, "event source cannot own a scenario controller");
		scenario_require(candidate.runtime_.instances().contains(id), "saved controller situation missing");
		ScenarioTreeController c;
		c.tree = j.at("tree").get<std::string>();
		c.selectors = bridge_selectors(j.at("bindings"));
		candidate.validate_selectors(c.selectors);
		candidate.validate_reader(c.tree, c.selectors);
		c.program = candidate.instantiate(c.tree, candidate.runtime_.instances().at(id).parameters);
		c.continuation = state_tree::load(c.program, j.at("continuation"));
		c.event_cursor = scenario_unsigned(j.at("cursor"));
		if (j.contains("receipt"))
			c.last_receipt = bridge_token(j.at("receipt"));
		scenario_require(candidate.controllers_.emplace(id, std::move(c)).second, "duplicate saved controller");
	}
	scenario_require(saved.at("receipts").is_array() &&
	                     saved.at("receipts").size() <= candidate.runtime_.definition().maximum_actions,
	                 "saved adapter receipt limit exceeded");
	for (const auto &j : saved.at("receipts"))
	{
		ScenarioTreeReceipt r;
		r.token = bridge_token(j.at("token"));
		const auto a = scenario_unsigned(j.at("action")), kind = scenario_unsigned(j.at("kind")),
		           status = scenario_unsigned(j.at("status"));
		scenario_require(a >= static_cast<unsigned>(GameplayTreeAction::SpawnEntity) &&
		                     a < static_cast<unsigned>(GameplayTreeAction::Count) &&
		                     a != static_cast<unsigned>(GameplayTreeAction::SetFollowContact) &&
		                     kind <= static_cast<unsigned>(ScenarioActionKind::Task) &&
		                     status <= static_cast<unsigned>(ScenarioActionStatus::Failed),
		                 "invalid saved scenario receipt vocabulary");
		r.action = static_cast<GameplayTreeAction>(a);
		r.request = j.at("request").get<std::string>();
		r.result = {static_cast<ScenarioActionKind>(kind),
		            static_cast<ScenarioActionStatus>(status),
		            scenario_unsigned(j.at("value")),
		            j.at("reason").get<std::string>(),
		            j.at("fingerprint").get<std::string>(),
		            j.at("handle").get<std::string>(),
		            j.at("entities").get<std::vector<EntityId>>()};
		scenario_require(candidate.controllers_.contains(r.token.instance) && r.request.size() <= 4096 &&
		                     r.result.reason.size() <= 4096 &&
		                     r.result.fingerprint.starts_with(domain_fingerprint_prefix),
		                 "invalid saved receipt origin/text");
		scenario_require(r.result.kind == bridge_action_kind(r.action), "saved receipt action/domain kind mismatch");
		if (const auto *actual = candidate.runtime_.result(r.token))
		{
			scenario_require(actual->kind == r.result.kind && actual->status == r.result.status &&
			                     actual->value == r.result.value && actual->entities == r.result.entities &&
			                     actual->handle == r.result.handle && actual->reason == r.result.reason,
			                 "saved adapter/domain receipt mismatch");
			if ((r.action == GameplayTreeAction::SpawnEntity || r.action == GameplayTreeAction::SpawnGroup ||
			     (r.action == GameplayTreeAction::AddObjective ||
			      r.action == GameplayTreeAction::StartObjectiveSequence)) &&
			    actual->status != ScenarioActionStatus::Failed)
				scenario_require(!actual->handle.empty() && r.request == actual->handle,
				                 "saved request/handle identity mismatch");
		}
		else
			scenario_require(r.result.status == ScenarioActionStatus::Failed || is_effect_action(r.action) ||
			                     r.action == GameplayTreeAction::RequestTask,
			                 "successful saved effect has no domain receipt");
		if (r.action != GameplayTreeAction::SpawnEntity && r.action != GameplayTreeAction::SpawnGroup &&
		    r.action != GameplayTreeAction::AddObjective && r.action != GameplayTreeAction::StartObjectiveSequence)
			scenario_require(r.request == bridge_request(r.token), "saved receipt request identity mismatch");
		scenario_require(candidate.receipts_.emplace(r.token, std::move(r)).second, "duplicate saved effect receipt");
	}
	for (const auto kind : {ScenarioEffectKind::Shield, ScenarioEffectKind::Modifier, ScenarioEffectKind::Group})
	{
		const auto *field = kind == ScenarioEffectKind::Shield     ? "shield_requests"
		                    : kind == ScenarioEffectKind::Modifier ? "modifier_requests"
		                                                           : "group_requests";
		const auto rows = saved.value(field, Json::array());
		scenario_require(rows.is_array() && rows.size() <= candidate.runtime_.definition().maximum_actions,
		                 "invalid pending effect array");
		for (const auto &j : rows)
		{
			ScenarioEffectRequest decoded;
			decoded.kind = kind;
			decoded.token = bridge_token(j.at("token"));
			decoded.target = scenario_unsigned(j.at("target"));
			decoded.definition = j.at("definition").get<std::string>();
			if (kind == ScenarioEffectKind::Group)
				decoded.source = scenario_unsigned(j.at("source"));
			else
			{
				const auto operation = scenario_unsigned(j.at("operation"));
				scenario_require(operation <= 2, "invalid effect request operation");
				decoded.operation = static_cast<ScenarioEffectOperation>(operation);
				decoded.grant = j.at("grant").get<std::string>();
				if (kind == ScenarioEffectKind::Modifier)
					decoded.equipment = j.at("equipment").get<std::string>();
				scenario_require(decoded.operation == ScenarioEffectOperation::Apply || decoded.definition.empty(),
				                 "non-apply request carries an effect definition");
			}
			const auto action = effect_action(kind, decoded.operation);
			const auto arguments = effect_arguments(decoded);
			auto request = effect_request(decoded.token, action, arguments);
			const auto receipt = candidate.receipts_.find(request.token);
			scenario_require(receipt != candidate.receipts_.end() && receipt->second.action == action &&
			                     receipt->second.result.status == ScenarioActionStatus::Pending &&
			                     receipt->second.result.fingerprint ==
			                         domain_fingerprint_prefix + bridge_fingerprint(action, arguments),
			                 "pending effect arguments disagree with receipt");
			// Retain the existing catalogue contract: native application failures are
			// confirmed by World, while operation groups must resolve on restore.
			if (kind == ScenarioEffectKind::Group)
				(void)compile_effect_group(catalog, request.definition, properties);
			scenario_require(candidate.effect_requests_.emplace(request.token, std::move(request)).second,
			                 "duplicate pending effect request");
		}
	}
	for (const auto &[token, receipt] : candidate.receipts_)
		if (receipt.result.status == ScenarioActionStatus::Pending && is_effect_action(receipt.action))
			scenario_require(candidate.effect_requests_.contains(token), "pending effect receipt missing request");
	for (const auto &[id, c] : candidate.controllers_)
	{
		if (c.last_receipt)
			scenario_require(c.last_receipt->instance == id && candidate.receipts_.contains(*c.last_receipt),
			                 "saved last receipt missing");
		ScenarioId last_event = 0;
		for (const auto &name : candidate.runtime_.definition().events)
			for (const auto &e : candidate.runtime_.events(name))
				last_event = std::max(last_event, e.id);
		scenario_require(c.event_cursor <= last_event, "saved event cursor exceeds declared event journal");
	}
	scenario_require(saved.at("bodies").is_array() && saved.at("bodies").size() <= 65536,
	                 "saved sampled body limit exceeded");
	for (const auto &j : saved.at("bodies"))
	{
		BodyView b{};
		b.id = scenario_unsigned(j.at("id"));
		b.definition = j.at("definition").get<std::string>();
		b.x = j.at("x").get<std::int64_t>();
		b.y = j.at("y").get<std::int64_t>();
		b.vx = j.at("vx").get<std::int64_t>();
		b.vy = j.at("vy").get<std::int64_t>();
		b.heading = j.at("heading").get<std::int64_t>();
		scenario_require(b.id > 0 && catalog.entities.contains(b.definition) && b.x >= -scenario_coordinate_limit &&
		                     b.x <= scenario_coordinate_limit && b.y >= -scenario_coordinate_limit &&
		                     b.y <= scenario_coordinate_limit && b.heading >= 0 && b.heading < 360000 &&
		                     candidate.bodies_.emplace(b.id, b).second,
		                 "invalid saved sampled body");
	}
	if (!candidate.runtime_.definition().task_root.empty())
	{
		const auto &plan = saved.at("task_plan");
		candidate.task_seed_ = scenario_unsigned(plan.at("seed"));
		scenario_require(candidate.task_seed_ >= 1 && candidate.task_seed_ <= 2147483647 &&
		                     (candidate.runtime_.definition().task_random_seed ||
		                      candidate.task_seed_ == candidate.runtime_.definition().task_seed),
		                 "invalid saved task seed");
		scenario_require(plan.at("version") == 1 && plan.at("nodes").is_array() &&
		                     plan.at("nodes").size() <= candidate.runtime_.definition().task_nodes,
		                 "incompatible/bounded task continuation required");
		candidate.task_attempt_ = scenario_unsigned(plan.at("attempt"));
		scenario_require(candidate.task_attempt_ <= candidate.runtime_.definition().maximum_instances,
		                 "invalid saved task attempt count");
		candidate.task_diagnostic_ = plan.at("diagnostic").get<std::string>();
		scenario_require(candidate.task_diagnostic_.size() <= 4096, "saved task diagnostic too long");
		auto decode_facts = [&](const Json &rows, ScenarioFacts &facts)
		{
			scenario_require(rows.is_object() && rows.size() <= 8192, "invalid saved task facts");
			for (auto row = rows.begin(); row != rows.end(); ++row)
			{
				const auto type = scenario_unsigned(row.value().at("type"));
				scenario_require(type <= static_cast<unsigned>(state_tree::Type::Message) && row.key().size() <= 256,
				                 "invalid saved task fact type/key");
				state_tree::Value value{static_cast<state_tree::Type>(type),
				                        row.value().at("number").get<std::int64_t>(),
				                        row.value().at("text").get<std::string>()};
				scenario_require(value.text.size() <= 4096, "saved task fact text too long");
				facts.emplace(row.key(), std::move(value));
			}
		};
		decode_facts(plan.at("facts"), candidate.task_facts_);
		decode_facts(plan.at("planned_facts"), candidate.planned_facts_);
		scenario_require(plan.at("instance_facts").is_array() &&
		                     plan.at("instance_facts").size() <= candidate.runtime_.definition().maximum_instances,
		                 "invalid saved task instance facts");
		for (const auto &row : plan.at("instance_facts"))
		{
			const auto id = scenario_unsigned(row.at("id"));
			scenario_require(candidate.runtime_.instances().contains(id) &&
			                     !candidate.task_instance_facts_.contains(id),
			                 "missing/duplicate task fact scope");
			decode_facts(row.at("facts"), candidate.task_instance_facts_[id]);
		}
		scenario_require(plan.at("planned_instance_facts").is_array() &&
		                     plan.at("planned_instance_facts").size() <=
		                         candidate.runtime_.definition().maximum_instances,
		                 "invalid saved planned task instance facts");
		for (const auto &row : plan.at("planned_instance_facts"))
		{
			const auto id = scenario_unsigned(row.at("id"));
			scenario_require(candidate.runtime_.instances().contains(id) &&
			                     !candidate.planned_instance_facts_.contains(id),
			                 "missing/duplicate planned task fact scope");
			decode_facts(row.at("facts"), candidate.planned_instance_facts_[id]);
		}
		std::set<std::string> paths;
		std::set<ScenarioId> situations;
		for (const auto &row : plan.at("nodes"))
		{
			ScenarioTaskNode node;
			node.path = row.at("path").get<std::string>();
			node.parent = row.at("parent").get<std::string>();
			node.scope = scenario_unsigned(row.at("scope"));
			node.release_tick = scenario_unsigned(row.at("release_tick"));
			node.released = row.at("released").get<bool>();
			scenario_require(node.release_tick <= candidate.tick_ && (node.released || !node.release_tick),
			                 "invalid task release clock");
			scenario_require((!node.scope || candidate.runtime_.instances().contains(node.scope)) &&
			                     (node.parent.empty() || paths.contains(node.parent)),
			                 "invalid saved task scope/parent");
			node.task = row.at("task").get<std::string>();
			node.method = row.at("method").get<std::string>();
			node.reason = row.at("reason").get<std::string>();
			node.after = row.at("after").get<std::vector<std::string>>();
			node.situation = scenario_unsigned(row.at("situation"));
			node.attempt = scenario_unsigned(row.at("attempt"));
			node.request_id = scenario_unsigned(row.at("request_id"));
			scenario_require(node.request_id <= candidate.task_attempt_, "invalid saved task request identity");
			const auto status = scenario_unsigned(row.at("status"));
			scenario_require(status <= static_cast<unsigned>(ScenarioTaskStatus::Superseded) &&
			                     valid_identifier(node.path) && paths.insert(node.path).second &&
			                     candidate.runtime_.definition().tasks.contains(node.task) &&
			                     node.reason.size() <= 4096 && node.attempt <= candidate.task_attempt_,
			                 "invalid saved task node");
			node.status = static_cast<ScenarioTaskStatus>(status);
			const auto &methods = candidate.runtime_.definition().tasks.at(node.task).methods;
			scenario_require(std::any_of(methods.begin(), methods.end(),
			                             [&](const auto &m)
			                             {
				                             return m.id == node.method;
			                             }),
			                 "unknown saved task method");
			if (node.situation)
			{
				const auto method = std::find_if(methods.begin(), methods.end(),
				                                 [&](const auto &m)
				                                 {
					                                 return m.id == node.method;
				                                 });
				scenario_require(!method->situation.empty() && candidate.controllers_.contains(node.situation) &&
				                     situations.insert(node.situation).second &&
				                     candidate.controllers_.at(node.situation).tree ==
				                         candidate.situations_.at(method->situation).tree &&
				                     candidate.runtime_.instances().at(node.situation).name ==
				                         "task." + node.path + "." + std::to_string(node.attempt),
				                 "missing/duplicate/mismatched saved task situation");
			}
			for (const auto &dependency : node.after)
				scenario_require(dependency != node.path && paths.contains(dependency),
				                 "saved task dependency must precede its child");
			candidate.tasks_.push_back(std::move(node));
		}
		for (const auto &node : candidate.tasks_)
			for (const auto &dependency : node.after)
				scenario_require(paths.contains(dependency), "saved task dependency missing");
		for (const auto &[token, receipt] : candidate.receipts_)
			if (receipt.action == GameplayTreeAction::RequestTask &&
			    receipt.result.status == ScenarioActionStatus::Succeeded)
			{
				const auto path = "request." + std::to_string(token.instance) + "." + receipt.result.handle;
				const auto node = std::find_if(candidate.tasks_.begin(), candidate.tasks_.end(),
				                               [&](const auto &n)
				                               {
					                               return n.path == path;
				                               });
				scenario_require(node != candidate.tasks_.end() && node->scope == token.instance &&
				                     node->request_id == receipt.result.value,
				                 "saved task admission receipt disagrees with its task");
				const std::vector<Value> arguments{Value::string(node->task), Value::string(receipt.result.handle)};
				scenario_require(receipt.result.fingerprint ==
				                     domain_fingerprint_prefix +
				                         bridge_fingerprint(GameplayTreeAction::RequestTask, arguments),
				                 "saved task admission fingerprint disagrees with its task");
			}
	}
	else
		scenario_require(!saved.contains("task_plan"), "unexpected task continuation");
	*this = std::move(candidate);
}
} // namespace phoenix
