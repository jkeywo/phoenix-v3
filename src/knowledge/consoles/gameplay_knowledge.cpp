module;
#include <memory>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

module phoenix.knowledge.consoles;
import phoenix.knowledge.decision_combat;
import phoenix.knowledge;
import phoenix.content.knowledge;
import phoenix.content.controllers;

namespace phoenix
{
namespace
{
using namespace knowledge_detail;
Json knowledge_field_json(const KnowledgeValue &value)
{
	return std::visit(
	    [](const auto &v)
	    {
		    return Json(v);
	    },
	    value);
}
template <class Visitor>
void visit_current_evidence(const std::vector<KnowledgeRecord> &records,
                            const std::optional<std::vector<KnowledgeId>> &readers, Visitor visitor)
{
	KnowledgeStreamSelection streams;
	for (auto record = records.rbegin(); record != records.rend(); ++record)
	{
		if (record->recipient_actor && readers &&
		    std::find(readers->begin(), readers->end(), record->recipient_actor) == readers->end())
			continue;
		if (streams.take_latest(*record))
			visitor(*record);
	}
}
Json knowledge_record_json(const CompiledKnowledge &compiled, const KnowledgeRecord &record)
{
	const auto &schema = knowledge_schema(compiled, record.value.schema);
	Json fields = Json::object();
	for (const auto &[key, value] : record.value.fields)
		fields[schema.fields.at(key - 1).name] = knowledge_field_json(value);
	const auto &p = record.value.provenance;
	return {{"id", record.id},
	        {"publication", record.publication},
	        {"recipient_actor", record.recipient_actor},
	        {"expires_tick", record.expires_tick},
	        {"schema", schema.name},
	        {"subject", record.value.subject},
	        {"fields", fields},
	        {"source", p.source},
	        {"observed_tick", p.observed_tick},
	        {"revision", p.revision},
	        {"uncertainty", p.uncertainty},
	        {"kind", static_cast<unsigned>(p.kind)},
	        {"received_tick", record.received_tick},
	        {"withdrawn", record.value.withdrawn},
	        {"stale_after_ticks", schema.stale_after}};
}
std::map<std::string, KnowledgeId> knowledge_saved_bindings(const Json &value)
{
	knowledge_require(value.is_object(), "saved bindings must be an object");
	std::map<std::string, KnowledgeId> result;
	for (auto it = value.begin(); it != value.end(); ++it)
		result.emplace(it.key(), knowledge_unsigned(it.value()));
	return result;
}
Json knowledge_registry_signature(Json value)
{
	Json result{{"consoles", Json::array()}, {"systems", Json::array()}, {"ais", Json::array()}};
	for (const auto &v : value.value("consoles", Json::array()))
		result["consoles"].push_back(
		    {{"id", v.at("id")}, {"ship", v.at("ship")}, {"station", v.at("station")}, {"roles", v.at("roles")}});
	for (const auto &v : value.value("systems", Json::array()))
	{
		Json actor{{"id", v.at("id")}, {"console", v.at("console")}};
		if (v.value("controller", false))
			actor["controller"] = true;
		result["systems"].push_back(std::move(actor));
	}
	for (const auto &v : value.value("memory", Json::array()))
		result["ais"].push_back({{"id", v.at("id")}, {"console", v.at("console")}});
	return result;
}
} // namespace

void GameplayKnowledge::configure(const Catalog &catalog, SharedPropertyCatalog properties)
{
	knowledge_require(!catalog.presentation,
	                  "restricted presentation content cannot initialise authoritative knowledge");
	auto definitions = compile_gameplay_knowledge(catalog, std::move(properties));
	GameplayKnowledge configured;
	configured.compiled_ = std::move(definitions.knowledge);
	configured.runtime_ = KnowledgeRuntime(configured.compiled_);
	configured.runtime_.track_record_changes(true);
	configured.definitions_ = std::move(definitions.ships);
	configured.messages_ = std::move(definitions.messages);
	for (const auto &[name, entity] : catalog.entities)
		for (const auto &seat : resolve_consoles(catalog, name))
			configured.console_labels_[{name, seat.id}] =
			    catalog.consoles.contains(seat.definition) ? catalog.consoles.at(seat.definition).value("name", seat.id)
			                                               : seat.id;
	configured.request_keys_ = std::move(definitions.request_keys);
	configured.telemetry_names_ = std::move(definitions.telemetry_names);
	configured.telemetry_indices_ = std::move(definitions.telemetry_indices);
	for (const auto &[name, entity] : catalog.entities)
		if (entity.contains("systems"))
			for (auto system = entity.at("systems").begin(); system != entity.at("systems").end(); ++system)
				if (system.value().contains("reporters") && !system.value().at("reporters").empty())
					configured.reporter_contract_[name][system.key()] = system.value().at("reporters");
	*this = std::move(configured);
}
KnowledgeId GameplayKnowledge::binding_id()
{
	knowledge_require(next_binding_ > 0 && next_binding_ < std::numeric_limits<KnowledgeId>::max(),
	                  "binding identity exhausted");
	return next_binding_++;
}
void GameplayKnowledge::register_ship(EntityId ship, const std::string &definition, KnowledgeTick tick)
{
	knowledge_require(ship != 0 && !ships_.contains(ship) && definitions_.contains(definition),
	                  "invalid/duplicate ship registration");
	GameplayKnowledgeShipBinding binding;
	binding.definition = definition;
	for (const auto &console : definitions_.at(definition).consoles)
	{
		if (!binding.stations.contains(console.console.station))
			binding.stations.emplace(console.console.station, binding_id());
		binding.consoles.emplace(console.console.id, binding_id());
	}
	for (const auto &[system, console] : definitions_.at(definition).owners)
		binding.systems.emplace(system, binding_id());
	for (const auto &[controller, console] : definitions_.at(definition).controllers)
		binding.controllers.emplace(controller, binding_id());
	install_ship(ship, std::move(binding), tick, true);
	sync_decision_payloads();
}
void GameplayKnowledge::install_ship(EntityId ship, GameplayKnowledgeShipBinding binding, KnowledgeTick tick,
                                     bool create_feeds)
{
	knowledge_require(ship != 0 && !ships_.contains(ship) && definitions_.contains(binding.definition),
	                  "unknown saved ship definition");
	const auto &definition = definitions_.at(binding.definition);
	knowledge_require(binding.consoles.size() == definition.consoles.size() &&
	                      binding.systems.size() == definition.owners.size() &&
	                      binding.controllers.size() == definition.controllers.size(),
	                  "saved ship binding count differs from authored ownership");
	std::set<std::string> expected_stations;
	for (const auto &console : definition.consoles)
	{
		knowledge_require(binding.consoles.contains(console.console.id) &&
		                      binding.stations.contains(console.console.station),
		                  "saved Console binding missing");
		expected_stations.insert(console.console.station);
		const auto id = binding.consoles.at(console.console.id);
		knowledge_require(!addresses_.contains(id), "duplicate saved Console identity");
		runtime_.register_console({id, ship, binding.stations.at(console.console.station), console.readers, false});
		addresses_.emplace(id, ConsoleAddress{ship, console.console.id});
		human_consoles_[id] = false;
	}
	knowledge_require(binding.stations.size() == expected_stations.size(), "unexpected saved Station binding");
	for (const auto &[system, console] : definition.owners)
	{
		knowledge_require(binding.systems.contains(system), "saved System binding missing");
		const auto id = binding.systems.at(system);
		runtime_.register_system(id, binding.consoles.at(console), false);
		human_systems_[id] = false;
	}
	for (const auto &[controller, console] : definition.controllers)
	{
		knowledge_require(binding.controllers.contains(controller), "saved Controller binding missing");
		const auto id = binding.controllers.at(controller);
		runtime_.register_controller(id, binding.consoles.at(console), false);
		runtime_.register_ai(id, binding.consoles.at(console));
		human_systems_[id] = false;
	}
	ships_.emplace(ship, std::move(binding));
	if (create_feeds)
		for (auto directive : definition.standing_directives)
		{
			directive.provenance = {ship, tick, 1, 0, EvidenceKind::Fact};
			runtime_.write_ship(ship, std::move(directive), tick);
		}
	if (create_feeds)
		for (const auto &feed : definition.feeds)
			activate_feed({ship, feed.source}, {ship, feed.target}, feed.schema,
			              feed.subject == 0 ? ship : feed.subject, feed.fields, tick);
}
bool GameplayKnowledge::contains_ship(EntityId ship) const
{
	return ships_.contains(ship);
}
KnowledgeId GameplayKnowledge::console_id(const ConsoleAddress &address) const
{
	knowledge_require(ships_.contains(address.ship) && ships_.at(address.ship).consoles.contains(address.console),
	                  "unknown Console address");
	return ships_.at(address.ship).consoles.at(address.console);
}
KnowledgeId GameplayKnowledge::system_id(EntityId ship, const std::string &system) const
{
	knowledge_require(ships_.contains(ship) && ships_.at(ship).systems.contains(system), "unknown fitted System");
	return ships_.at(ship).systems.at(system);
}
KnowledgeId GameplayKnowledge::controller_id(const ControllerAddress &address) const
{
	const auto key = address.console + "/" + address.controller;
	knowledge_require(ships_.contains(address.entity) && ships_.at(address.entity).controllers.contains(key),
	                  "unknown Controller");
	return ships_.at(address.entity).controllers.at(key);
}
void GameplayKnowledge::set_controller_control(const ControllerAddress &address, bool present, bool human,
                                               bool ai_running)
{
	const auto &definition = definitions_.at(ships_.at(address.entity).definition);
	knowledge_require(definition.controllers.at(address.console + "/" + address.controller) == address.console &&
	                      (!human || present),
	                  "invalid Controller operator binding");
	const auto id = controller_id(address);
	runtime_.set_human_control(console_id({address.entity, address.console}), present);
	human_consoles_[console_id({address.entity, address.console})] = present;
	runtime_.set_system_human_control(id, human);
	human_systems_[id] = human;
	if (ai_running)
		assisted_controllers_.insert(id);
	else
		assisted_controllers_.erase(id);
}
std::vector<CoordinationDelivery> GameplayKnowledge::peek_controller(const ControllerAddress &address)
{
	const auto id = controller_id(address);
	knowledge_require(!human_systems_.at(id) || assisted_controllers_.contains(id),
	                  "human-controlled Controller cannot inspect AI continuation");
	const auto &memory = runtime_.private_memory(id);
	knowledge_require(memory.console == console_id({address.entity, address.console}), "Controller Console mismatch");
	auto result = runtime_.message_history(memory.console);
	const auto readers = controller_readers(address);
	std::erase_if(result,
	              [&](const auto &delivery)
	              {
		              const auto &destination = delivery.message.value.destination;
		              return delivery.sequence <= memory.receive_cursor || !delivery.actionable ||
		                     delivery.offer_only ||
		                     ((destination.kind == MessageDestinationKind::Controller ||
		                       destination.kind == MessageDestinationKind::System) &&
		                      std::find(readers.begin(), readers.end(), destination.id) == readers.end());
	              });
	return result;
}
void GameplayKnowledge::acknowledge_controller(const ControllerAddress &address, KnowledgeId through_message)
{
	const auto messages = peek_controller(address);
	const auto found = std::find_if(messages.begin(), messages.end(),
	                                [&](const auto &delivery)
	                                {
		                                return delivery.message.id == through_message;
	                                });
	knowledge_require(found != messages.end(), "Controller acknowledgement names unavailable message");
	runtime_.private_memory(controller_id(address)).receive_cursor = found->sequence;
}
ConsoleAddress GameplayKnowledge::address(KnowledgeId console) const
{
	knowledge_require(addresses_.contains(console), "unknown Console identity");
	return addresses_.at(console);
}
const CompiledKnowledge &GameplayKnowledge::definition() const
{
	return compiled_;
}
const GameplayKnowledgeMessageDefinition &GameplayKnowledge::message_definition(const std::string &name) const
{
	knowledge_require(messages_.contains(name), "unknown message template");
	return messages_.at(name);
}
KnowledgeId GameplayKnowledge::request_key(const std::string &name) const
{
	if (name.empty())
		return 0;
	knowledge_require(request_keys_.contains(name), "unknown authored request key");
	return request_keys_.at(name);
}
KnowledgeKey GameplayKnowledge::message_producer(const ConsoleAddress &source, const std::string &sender,
                                                 KnowledgeKey key, ControlOrigin origin, bool controller) const
{
	const auto system =
	    controller ? controller_id({source.ship, source.console, sender}) : system_id(source.ship, sender);
	const auto &allowed = console_definition(source).producers;
	// Authored schema order supplies a deterministic preference; only roles held
	// by this exact sending Console are candidates.
	for (const auto role : knowledge_schema(compiled_, key).producers)
		if (knowledge_has(allowed, role))
		{
			admit_sender(source, system, role, origin);
			return role;
		}
	throw std::runtime_error("gameplay knowledge: source Console has no authorized message producer role");
}
const GameplayKnowledgeConsoleDefinition &GameplayKnowledge::console_definition(const ConsoleAddress &address) const
{
	console_id(address);
	const auto &definition = definitions_.at(ships_.at(address.ship).definition);
	for (const auto &console : definition.consoles)
		if (console.console.id == address.console)
			return console;
	throw std::runtime_error("gameplay knowledge: unresolved Console definition");
}
bool GameplayKnowledge::readable(const GameplayKnowledgeConsoleDefinition &console, KnowledgeKey key) const
{
	const auto &schema = knowledge_schema(compiled_, key);
	for (const auto role : console.readers)
		if (knowledge_has(schema.readers, role))
			return true;
	return false;
}
void GameplayKnowledge::set_console_presence(const ConsoleAddress &address, bool present)
{
	const auto id = console_id(address);
	runtime_.set_human_control(id, present);
	human_consoles_[id] = present;
}
void GameplayKnowledge::clear_control()
{
	notifications_.clear();
	for (auto &[id, human] : human_consoles_)
	{
		human = false;
		runtime_.set_human_control(id, false);
	}
	for (auto &[id, human] : human_systems_)
	{
		human = false;
		runtime_.set_system_human_control(id, false);
	}
}
std::size_t GameplayKnowledge::ingest_system(EntityId ship, const std::string &system, KnowledgeRecordInput value,
                                             KnowledgeTick tick)
{
	const auto id = system_id(ship, system);
	const auto &definition = definitions_.at(ships_.at(ship).definition);
	const auto &owner = definition.owners.at(system);
	knowledge_require(value.provenance.source == id &&
	                      knowledge_has(console_definition({ship, owner}).producers, value.producer),
	                  "System observation has invalid producer/source");
	std::size_t updated = 0;
	for (const auto &console : definition.consoles)
		if ((console.console.id == owner ||
		     std::find(console.console.feed_systems.begin(), console.console.feed_systems.end(), system) !=
		         console.console.feed_systems.end()) &&
		    readable(console, value.schema))
			updated += runtime_.write_console(console_id({ship, console.console.id}), value, tick) ? 1 : 0;
	sync_decision_payloads();
	return updated;
}
bool GameplayKnowledge::ingest_console(const ConsoleAddress &address, KnowledgeRecordInput value, KnowledgeTick tick)
{
	knowledge_require(knowledge_has(console_definition(address).producers, value.producer),
	                  "Console observation producer is not permitted");
	const auto committed = runtime_.write_console(console_id(address), std::move(value), tick);
	sync_decision_payloads();
	return committed;
}
const std::vector<std::string> &GameplayKnowledge::telemetry_names() const
{
	return telemetry_names_;
}
void GameplayKnowledge::verify_reporter_fields(const std::string &name, const std::string &system,
                                               const std::map<std::string, std::int64_t> &values) const
{
	const auto &definition = definitions_.at(name);
	if (definition.reporters.contains(system))
		for (const auto &report : definition.reporters.at(system))
			knowledge_require(values.contains(report.field), "reporter " + report.id +
			                                                     " field unavailable on fitted System " + system +
			                                                     ": " + report.field);
}
std::size_t GameplayKnowledge::ingest_telemetry(EntityId ship, const std::string &system,
                                                const std::vector<std::optional<KnowledgeValue>> &values,
                                                KnowledgeTick tick)
{
	const auto source = system_id(ship, system);
	const auto &definition = definitions_.at(ships_.at(ship).definition);
	if (!definition.telemetry.contains(system))
		return 0;
	std::size_t updated = 0;
	for (const auto &binding : definition.telemetry.at(system))
	{
		KnowledgeRecordInput record;
		record.schema = binding.schema;
		record.producer = binding.producer;
		record.subject = binding.subject == 0 ? ship : binding.subject;
		record.provenance = {source, tick, tick, 0, binding.kind};
		for (const auto &field : binding.fields)
		{
			if (field.value >= values.size() || !values[field.value])
			{
				knowledge_require(!field.required,
				                  "required telemetry value is absent: " + telemetry_names_.at(field.value));
				continue;
			}
			auto value = *values[field.value];
			if (field.type == KnowledgeType::Boolean && std::holds_alternative<std::int64_t>(value))
			{
				const auto number = std::get<std::int64_t>(value);
				knowledge_require(number == 0 || number == 1, "Boolean telemetry must be zero or one");
				value = number != 0;
			}
			record.fields.emplace(field.field, std::move(value));
		}
		updated += ingest_system(ship, system, std::move(record), tick);
	}
	sync_decision_payloads();
	return updated;
}
std::size_t GameplayKnowledge::ingest_telemetry(EntityId ship, const std::string &system,
                                                const std::map<std::string, std::int64_t> &values, KnowledgeTick tick)
{
	std::vector<std::optional<KnowledgeValue>> typed(telemetry_names_.size());
	for (const auto &[name, value] : values)
		if (const auto found = telemetry_indices_.find(name); found != telemetry_indices_.end())
			typed[found->second] = value;
	const auto updated = ingest_telemetry(ship, system, typed, tick);
	const auto &definition = definitions_.at(ships_.at(ship).definition);
	if (definition.reporters.contains(system))
	{
		const auto &reports = definition.reporters.at(system);
		for (std::size_t i = 0; i < reports.size(); ++i)
		{
			const auto &report = reports[i];
			knowledge_require(values.contains(report.field), "reporter telemetry is unavailable: " + report.field);
			const auto amount = values.at(report.field);
			const bool warning = amount > report.above;
			if (!status_change(ship, system, static_cast<KnowledgeKey>(i + 1), warning, tick, report.debounce,
			                   report.recovery, report.minimum_gap, false))
				continue;
			const auto &name = warning ? report.message : report.recovery_message;
			if (name.empty())
				continue;
			const ConsoleAddress source{ship, definition.owners.at(system)};
			const auto &message = message_definition(name);
			KnowledgeRecordInput payload;
			payload.schema = message.schema;
			payload.producer = message_producer(source, system, message.schema, ControlOrigin::Reporter);
			payload.subject = ship;
			payload.provenance = {system_id(ship, system), tick, tick, 0, EvidenceKind::Fact};
			GameplayKnowledgeMessageContext context;
			context.amount = amount;
			context.flag = warning;
			context.detail = system;
			const auto bound = evaluate_gameplay_message(message, context);
			payload.fields = bound.fields;
			admit_template(source, system, name, payload, ControlOrigin::Reporter, tick, bound.parameters);
		}
	}
	sync_decision_payloads();
	return updated;
}
void GameplayKnowledge::set_camera(EntityId ship, ShipCameraState state, KnowledgeTick tick)
{
	knowledge_require(state.mode >= 0 && state.mode <= 1 && state.direction >= 0 && state.direction <= 7,
	                  "invalid ship camera state");
	const auto key = compiled_.schema("phoenix.camera");
	KnowledgeRecordInput record;
	record.schema = key;
	record.producer = compiled_.role("phoenix.scenario");
	record.subject = ship;
	std::uint64_t revision = 0;
	const auto board = public_board(ship);
	for (const auto &previous : board.at("shared"))
		if (previous.at("schema") == "phoenix.camera")
			revision = previous.at("revision").get<std::uint64_t>() + 1;
	record.provenance = {ship, tick, revision, 0, EvidenceKind::Fact};
	record.fields = {{compiled_.field(key, "mode"), static_cast<std::int64_t>(state.mode)},
	                 {compiled_.field(key, "direction"), static_cast<std::int64_t>(state.direction)}};
	knowledge_require(runtime_.write_ship(ship, std::move(record), tick), "camera update was not accepted");
	sync_decision_payloads();
}
bool GameplayKnowledge::publish_ship(EntityId ship, KnowledgeRecordInput value, KnowledgeTick tick)
{
	knowledge_require(ships_.contains(ship), "unknown objective/publication recipient ship");
	const auto committed = runtime_.write_ship(ship, std::move(value), tick);
	sync_decision_payloads();
	return committed;
}
bool GameplayKnowledge::publish_scenario_console(const ConsoleAddress &address, KnowledgeRecordInput value,
                                                 KnowledgeTick tick)
{
	// Only the trusted scenario execution path may originate these records.
	// Recipient scope is checked by write_console; recipients gain no producer role.
	const auto committed = runtime_.write_console(console_id(address), std::move(value), tick);
	sync_decision_payloads();
	return committed;
}
std::string GameplayKnowledge::request_key_name(KnowledgeId key) const
{
	if (key == 0)
		return {};
	for (const auto &[name, value] : request_keys_)
		if (value == key)
			return name;
	throw std::runtime_error("gameplay knowledge: unknown request key identity");
}
KnowledgeId GameplayKnowledge::activate_publication(const ConsoleAddress &address, const std::string &name,
                                                    KnowledgeTick tick, bool execute)
{
	const auto &definitions = definitions_.at(ships_.at(address.ship).definition).publications;
	knowledge_require(definitions.contains(name), "unknown publication template");
	const auto &definition = definitions.at(name);
	knowledge_require(definition.source == address.console, "publication template belongs to another Console");
	return publish(address, definition.schema, definition.subject == 0 ? address.ship : definition.subject,
	               definition.fields, definition.live, tick, execute);
}
KnowledgeId GameplayKnowledge::activate_publication(const ConsoleAddress &address, const std::string &name,
                                                    KnowledgeTick tick, KnowledgeId subject, bool live,
                                                    const std::string &request_key)
{
	const auto &definitions = definitions_.at(ships_.at(address.ship).definition).publications;
	knowledge_require(definitions.contains(name), "unknown publication template");
	const auto &definition = definitions.at(name);
	knowledge_require(subject == (definition.subject == 0 ? address.ship : definition.subject),
	                  "publication subject differs from authored template");
	knowledge_require(live == definition.live, "publication live mode differs from authored template");
	knowledge_require(request_key.empty(), "publication request keys are unsupported; stop by publication identity");
	return activate_publication(address, name, tick);
}
KnowledgeId GameplayKnowledge::publish(const ConsoleAddress &address, KnowledgeKey schema, KnowledgeId subject,
                                       std::vector<KnowledgeKey> fields, bool live, KnowledgeTick tick, bool execute)
{
	const auto console = console_id(address);
	const auto id = runtime_.publish(console, schema, subject, std::move(fields), live, tick, execute);
	sync_decision_payloads();
	if (execute)
		publication_owners_.emplace(id, console);
	return id;
}
KnowledgeId GameplayKnowledge::activate_feed(const ConsoleAddress &source, const ConsoleAddress &target,
                                             KnowledgeKey schema, KnowledgeId subject, std::vector<KnowledgeKey> fields,
                                             KnowledgeTick tick)
{
	knowledge_require(source.ship == target.ship, "cross-ship feed refused");
	const auto console = console_id(source);
	const auto id = runtime_.feed(console, console_id(target), schema, subject, std::move(fields), tick);
	sync_decision_payloads();
	publication_owners_.emplace(id, console);
	return id;
}
void GameplayKnowledge::stop_publication(const ConsoleAddress &address, KnowledgeId publication)
{
	knowledge_require(publication_owners_.contains(publication) &&
	                      publication_owners_.at(publication) == console_id(address),
	                  "publication is owned by another Console");
	runtime_.stop_publication(publication);
}
void GameplayKnowledge::admit_sender(const ConsoleAddress &source, KnowledgeId sender, KnowledgeKey producer,
                                     ControlOrigin origin, bool known_record) const
{
	const auto &ship = ships_.at(source.ship);
	const auto &definition = definitions_.at(ship.definition);
	const auto system = std::find_if(ship.systems.begin(), ship.systems.end(),
	                                 [&](const auto &entry)
	                                 {
		                                 return entry.second == sender;
	                                 });
	const auto controller = std::find_if(ship.controllers.begin(), ship.controllers.end(),
	                                     [&](const auto &entry)
	                                     {
		                                     return entry.second == sender;
	                                     });
	knowledge_require(
	    (system != ship.systems.end() && definition.owners.at(system->first) == source.console) ||
	        (controller != ship.controllers.end() && definition.controllers.at(controller->first) == source.console),
	    "sender is not owned by source Console");
	knowledge_require(known_record || knowledge_has(console_definition(source).producers, producer),
	                  "sender producer role forbidden");
	if (origin == ControlOrigin::Reporter)
	{
		knowledge_require(system != ship.systems.end(), "reporter source must be a physical System");
		return;
	}
	const bool human = human_consoles_.at(console_id(source)) && human_systems_.at(sender);
	knowledge_require((origin == ControlOrigin::Human && human) ||
	                      (origin == ControlOrigin::AI && (!human || assisted_controllers_.contains(sender))),
	                  "sender origin does not hold effective control");
}
bool GameplayKnowledge::known_evidence(const ConsoleAddress &source, const KnowledgeRecordInput &value,
                                       const std::optional<std::vector<KnowledgeId>> &readers) const
{
	const auto console = console_id(source);
	const auto matches = [&](const KnowledgeRecord &record)
	{
		if (record.recipient_actor && readers &&
		    std::find(readers->begin(), readers->end(), record.recipient_actor) == readers->end())
			return false;
		const auto &candidate = record.value;
		const auto &a = candidate.provenance;
		const auto &b = value.provenance;
		if (candidate.schema != value.schema || candidate.producer != value.producer ||
		    candidate.subject != value.subject || candidate.withdrawn != value.withdrawn || a.source != b.source ||
		    a.observed_tick != b.observed_tick || a.revision != b.revision || a.uncertainty != b.uncertainty ||
		    a.kind != b.kind)
			return false;
		for (const auto &[key, field] : value.fields)
			if (!candidate.fields.contains(key) || candidate.fields.at(key) != field)
				return false;
		return true;
	};
	bool found = false;
	const auto visit = [&](const KnowledgeRecord &record)
	{
		found |= matches(record);
	};
	visit_current_evidence(runtime_.read_console(console), readers, visit);
	visit_current_evidence(runtime_.read_ship(console), readers, visit);
	return found;
}
KnowledgeId GameplayKnowledge::admit_message(const ConsoleAddress &source, CoordinationInput value, KnowledgeTick tick,
                                             bool execute, bool require_evidence)
{
	console_id(source);
	value.evidence_report = false;
	if (value.origin == ControlOrigin::Reporter && !require_evidence)
	{
		admit_sender(source, value.sender_system, value.payload.producer, value.origin);
		knowledge_require(value.payload.provenance.kind == EvidenceKind::Fact &&
		                      value.payload.provenance.source == value.sender_system,
		                  "reporter can only originate measured facts");
		const auto committed = runtime_.send(std::move(value), tick, execute);
		sync_decision_payloads();
		return committed;
	}
	// An authorised new claim/intent already carries its sender's evidence.
	// Only forwarding other evidence needs to search the permitted record history.
	const bool own_intent = value.payload.provenance.source == value.sender_system &&
	                        value.payload.provenance.kind != EvidenceKind::Fact &&
	                        knowledge_has(console_definition(source).producers, value.payload.producer);
	std::optional<std::vector<KnowledgeId>> readers;
	if (value.origin != ControlOrigin::Human)
	{
		readers = std::vector<KnowledgeId>{value.sender_system};
		for (const auto &[key, actor] : ships_.at(source.ship).controllers)
			if (actor == value.sender_system)
				readers = controller_readers({source.ship, source.console, key.substr(key.find('/') + 1)});
	}
	const auto known = (require_evidence || !own_intent) && known_evidence(source, value.payload, readers);
	knowledge_require(!require_evidence || known, "evidence reports require current permitted source evidence");
	admit_sender(source, value.sender_system, value.payload.producer, value.origin, known);
	knowledge_require(
	    known || (value.payload.provenance.source == value.sender_system &&
	              value.payload.provenance.kind != EvidenceKind::Fact),
	    "new factual messages require permitted observed evidence; new claims/intents name their own source");
	if (known)
	{
		bool valid = false, unbounded = false;
		KnowledgeTick expiry = 0;
		const auto preserve_expiry = [&](const KnowledgeRecord &record)
		{
			const auto &a = record.value.provenance;
			const auto &b = value.payload.provenance;
			if (record.value.schema != value.payload.schema || record.value.producer != value.payload.producer ||
			    record.value.subject != value.payload.subject || record.value.withdrawn != value.payload.withdrawn ||
			    a.source != b.source || a.observed_tick != b.observed_tick || a.revision != b.revision ||
			    a.kind != b.kind || a.uncertainty != b.uncertainty || runtime_.stale(record, tick) ||
			    (record.recipient_actor && readers &&
			     std::find(readers->begin(), readers->end(), record.recipient_actor) == readers->end()))
				return;
			for (const auto &[key, field] : value.payload.fields)
				if (!record.value.fields.contains(key) || record.value.fields.at(key) != field)
					return;
			valid = true;
			unbounded |= record.expires_tick == 0;
			expiry = std::max(expiry, record.expires_tick);
		};
		visit_current_evidence(runtime_.read_console(console_id(source)), readers, preserve_expiry);
		visit_current_evidence(runtime_.read_ship(console_id(source)), readers, preserve_expiry);
		knowledge_require(valid, "cannot forward expired or stale evidence");
		if (!unbounded && (!value.expires_tick || value.expires_tick > expiry))
			value.expires_tick = expiry;
	}
	value.evidence_report = known;
	const auto committed = runtime_.send(std::move(value), tick, execute);
	sync_decision_payloads();
	return committed;
}
KnowledgeId GameplayKnowledge::admit_template(const ConsoleAddress &source, const std::string &sender,
                                              const std::string &name, KnowledgeRecordInput payload,
                                              ControlOrigin origin, KnowledgeTick tick,
                                              std::map<std::string, std::string> parameters, bool execute,
                                              bool controller, const std::string &speaker_name,
                                              const Bytes &speaker_avatar, const std::string &speaker_portrait)
{
	const auto &definition = message_definition(name);
	knowledge_require(origin != ControlOrigin::Human || definition.player_command ||
	                      (controller && definition.operator_report),
	                  "automatic officer report is not a player message command");
	knowledge_require(payload.schema == definition.schema, "payload does not match message template");
	for (const auto &[key, value] : definition.literal_fields)
		payload.fields.try_emplace(key, value);
	for (const auto field : definition.bound_fields)
		knowledge_require(payload.fields.contains(field), "message template binding was not evaluated");
	for (const auto &name : definition.bound_parameters)
		knowledge_require(parameters.contains(name), "message presentation binding was not evaluated");
	const auto &ship = ships_.at(source.ship);
	CoordinationInput value;
	value.sender_system =
	    controller ? controller_id({source.ship, source.console, sender}) : system_id(source.ship, sender);
	value.origin = origin;
	value.payload = std::move(payload);
	value.destination.kind = definition.destination_kind;
	switch (definition.destination_kind)
	{
	case MessageDestinationKind::Controller:
		value.destination.id =
		    controller_id({source.ship, definition.destination_console, definition.destination_name});
		break;
	case MessageDestinationKind::System:
		value.destination.id = system_id(source.ship, definition.destination_name);
		break;
	case MessageDestinationKind::Console:
		value.destination.id = console_id({source.ship, definition.destination_name == "self" ? source.console
		                                                : ship.consoles.contains(definition.destination_name)
		                                                    ? definition.destination_name
		                                                    : definition.destination_fallback});
		break;
	case MessageDestinationKind::Station:
		knowledge_require(ship.stations.contains(definition.destination_name), "unknown template destination Station");
		value.destination.id = ship.stations.at(definition.destination_name);
		break;
	case MessageDestinationKind::Ship:
		value.destination.id = source.ship;
		break;
	}
	value.presentation = definition.presentation;
	if (origin == ControlOrigin::Human || (origin == ControlOrigin::AI && controller))
	{
		knowledge_require(speaker_name.size() <= 64 && speaker_avatar.size() <= 12288, "speaker profile exceeds limit");
		value.presentation.speaker_name = speaker_name;
		value.presentation.speaker_avatar = speaker_avatar;
		value.presentation.speaker_portrait = speaker_portrait;
	}
	for (auto &[key, parameter] : parameters)
		value.presentation.parameters[key] = std::move(parameter);
	value.delay = definition.delay;
	value.request_key = definition.request_key;
	value.human_seeking = definition.human_seeking;
	if (definition.expires_after != 0)
	{
		knowledge_require(tick <= std::numeric_limits<KnowledgeTick>::max() - definition.expires_after,
		                  "message expiry overflow");
		value.expires_tick = tick + definition.expires_after;
	}
	return admit_message(source, std::move(value), tick, execute, definition.evidence_report);
}
KnowledgeId GameplayKnowledge::admit_evidence_template(const ConsoleAddress &source, const std::string &sender,
                                                       const std::string &name, const KnowledgeRecord &record,
                                                       ControlOrigin origin, KnowledgeTick tick, bool execute,
                                                       bool controller, const std::string &speaker_name,
                                                       const Bytes &speaker_avatar, const std::string &speaker_portrait)
{
	const auto &message = message_definition(name);
	knowledge_require(message.evidence_report && message.schema == record.value.schema,
	                  "evidence report requires its declared source schema");
	knowledge_require(!stale(record, tick), "cannot report stale evidence");
	GameplayKnowledgeMessageContext context;
	context.evidence = &record;
	const auto presentation = evaluate_gameplay_message(message, context);
	return admit_template(source, sender, name, record.value, origin, tick, presentation.parameters, execute,
	                      controller, speaker_name, speaker_avatar, speaker_portrait);
}
KnowledgeId GameplayKnowledge::publish_scenario_message(const ConsoleAddress &source, CoordinationInput value,
                                                        KnowledgeTick tick)
{
	knowledge_require(value.origin == ControlOrigin::Scenario, "scenario message requires scenario origin");
	const auto &ship = ships_.at(source.ship);
	const auto system = std::find_if(ship.systems.begin(), ship.systems.end(),
	                                 [&](const auto &entry)
	                                 {
		                                 return entry.second == value.sender_system;
	                                 });
	knowledge_require(system != ship.systems.end() &&
	                      definitions_.at(ship.definition).owners.at(system->first) == source.console,
	                  "invalid scenario sender binding");
	const auto committed = runtime_.send(std::move(value), tick);
	sync_decision_payloads();
	return committed;
}
bool GameplayKnowledge::withdraw_request(const ConsoleAddress &source, const std::string &system, KnowledgeId key,
                                         ControlOrigin origin, bool controller)
{
	const auto sender =
	    controller ? controller_id({source.ship, source.console, system}) : system_id(source.ship, system);
	const auto &console = console_definition(source);
	knowledge_require(!console.producers.empty(), "request sender has no producer role");
	admit_sender(source, sender, console.producers.front(), origin);
	return runtime_.withdraw_request(sender, key);
}
std::vector<CoordinationDelivery> GameplayKnowledge::tick(KnowledgeTick tick)
{
	auto delivered = runtime_.advance(tick);
	sync_decision_payloads();
	for (const auto &delivery : delivered)
		if (delivery.notify_human)
		{
			auto &notifications = notifications_[delivery.console];
			notifications.push_back(delivery);
			if (notifications.size() > compiled_.history_limit)
				notifications.erase(notifications.begin(),
				                    notifications.begin() +
				                        static_cast<std::ptrdiff_t>(notifications.size() - compiled_.history_limit));
		}
	return delivered;
}
std::vector<CoordinationMessage> GameplayKnowledge::pending_messages(std::size_t limit) const
{
	return runtime_.pending_messages(limit);
}
bool GameplayKnowledge::status_change(EntityId ship, const std::string &system, KnowledgeKey status, bool value,
                                      KnowledgeTick tick, KnowledgeTick debounce, KnowledgeTick recovery_hysteresis,
                                      KnowledgeTick minimum_gap, bool report_initial)
{
	return runtime_.status_change(system_id(ship, system), status, value, tick, debounce, recovery_hysteresis,
	                              minimum_gap, report_initial);
}
bool GameplayKnowledge::stale(const KnowledgeRecord &record, KnowledgeTick now) const
{
	return runtime_.stale(record, now);
}
GameplayKnowledgeProjection GameplayKnowledge::project(const ConsoleAddress &address, bool public_only) const
{
	const auto id = console_id(address);
	auto shared = runtime_.read_ship(id);
	KnowledgeId camera = 0;
	for (const auto &record : shared)
		if (compiled_.schemas.at(record.value.schema - 1).name == "phoenix.camera")
			camera = std::max(camera, record.id);
	std::erase_if(shared,
	              [&](const auto &record)
	              {
		              return record.id != camera &&
		                     compiled_.schemas.at(record.value.schema - 1).name == "phoenix.camera";
	              });
	GameplayKnowledgeProjection result{
	    id, public_only ? std::vector<KnowledgeRecord>{} : runtime_.read_console(id), std::move(shared),
	    runtime_.message_history(id),
	    !public_only && notifications_.contains(id) ? notifications_.at(id) : std::vector<CoordinationDelivery>{}};
	for (const auto *records : {&result.records, &result.shared})
		for (const auto &record : *records)
		{
			const auto key = decision_evidence_key(record);
			const auto at = decision_payloads_.find(key);
			if (at != decision_payloads_.end())
				result.decision_payloads.emplace(key, at->second);
		}
	return result;
}
std::vector<KnowledgeId> GameplayKnowledge::controller_readers(const ControllerAddress &address) const
{
	return {controller_id(address)};
}
std::optional<GameplayKnowledgeProjection>
GameplayKnowledge::restrict_projection(const GameplayKnowledgeProjection &projection,
                                       const std::vector<KnowledgeId> &readers) const
{
	const auto private_record = [&](const KnowledgeRecord &record)
	{
		return record.recipient_actor &&
		       std::find(readers.begin(), readers.end(), record.recipient_actor) == readers.end();
	};
	const auto private_delivery = [&](const CoordinationDelivery &delivery)
	{
		if (delivery.public_copy)
			return false;
		if (delivery.offer_only)
			return true;
		const auto &destination = delivery.message.value.destination;
		return (destination.kind == MessageDestinationKind::Controller ||
		        destination.kind == MessageDestinationKind::System) &&
		       std::find(readers.begin(), readers.end(), destination.id) == readers.end();
	};
	if (std::none_of(projection.records.begin(), projection.records.end(), private_record) &&
	    std::none_of(projection.shared.begin(), projection.shared.end(), private_record) &&
	    std::none_of(projection.messages.begin(), projection.messages.end(), private_delivery) &&
	    std::none_of(projection.notifications.begin(), projection.notifications.end(), private_delivery))
		return {};
	auto result = projection;
	std::erase_if(result.records, private_record);
	std::erase_if(result.shared, private_record);
	std::set<DecisionEvidenceKey> visible;
	for (const auto *records : {&result.records, &result.shared})
		for (const auto &record : *records)
			visible.insert(decision_evidence_key(record));
	std::erase_if(result.decision_payloads,
	              [&](const auto &entry)
	              {
		              return !visible.contains(entry.first);
	              });
	std::erase_if(result.messages, private_delivery);
	std::erase_if(result.notifications, private_delivery);
	return result;
}
std::vector<const KnowledgeRecord *> GameplayKnowledge::sensor_records(const ConsoleAddress &address) const
{
	const auto id = console_id(address);
	std::vector<const KnowledgeRecord *> result;
	for (const bool shared : {false, true})
		for (const auto *record : runtime_.record_views(id, shared))
		{
			if (record->recipient_actor)
				continue;
			const auto *payload = decision_payload(*record);
			if (payload && (std::get_if<ContactReportDecision>(payload) || std::get_if<SensorReportDecision>(payload)))
				result.push_back(record);
		}
	return result;
}
void GameplayKnowledge::sync_decision_payloads()
{
	for (auto &[id, changed] : runtime_.take_record_changes())
	{
		DecisionEvidenceKey first_key{};
		std::get<0>(first_key) = id;
		const auto first = decision_payloads_.lower_bound(first_key);
		auto last = first;
		while (last != decision_payloads_.end() && std::get<0>(last->first) == id)
			++last;
		decision_payloads_.erase(first, last);
		if (!changed)
			continue;
		auto payload = decode_decision_evidence(changed->value, compiled_);
		if (!std::holds_alternative<std::monostate>(payload))
			decision_payloads_.emplace(decision_evidence_key(*changed),
			                           std::make_shared<const DecisionEvidencePayload>(std::move(payload)));
	}
}
const DecisionEvidencePayload *GameplayKnowledge::decision_payload(const KnowledgeRecord &record) const
{
	const auto at = decision_payloads_.find(decision_evidence_key(record));
	return at == decision_payloads_.end() ? nullptr : at->second.get();
}
std::shared_ptr<const Json> GameplayKnowledge::decoded_field(const KnowledgeRecord &record, KnowledgeKey field) const
{
	return runtime_.decoded_field(record, field);
}
Json GameplayKnowledge::project_json(const ConsoleAddress &address, bool public_only,
                                     std::optional<std::vector<KnowledgeId>> readers) const
{
	const auto original = project(address, public_only);
	const auto restricted = readers ? restrict_projection(original, *readers) : std::nullopt;
	const auto &projection = restricted ? *restricted : original;
	Json result{{"console", address.console}, {"ship", address.ship},      {"records", Json::array()},
	            {"shared", Json::array()},    {"messages", Json::array()}, {"notifications", Json::array()}};
	for (const auto &record : projection.records)
		result["records"].push_back(knowledge_record_json(compiled_, record));
	for (const auto &record : projection.shared)
		result["shared"].push_back(knowledge_record_json(compiled_, record));
	for (const auto &delivery : projection.messages)
	{
		if (public_only && !delivery.public_copy)
			continue;
		const auto &message = delivery.message;
		const auto &p = message.value.presentation;
		const auto source = this->address(message.sender_console);
		const auto label = console_labels_.find({ships_.at(source.ship).definition, source.console});
		const auto &actors = ships_.at(source.ship).controllers;
		const bool controller_source = std::any_of(actors.begin(), actors.end(),
		                                           [&](const auto &actor)
		                                           {
			                                           return actor.second == message.value.sender_system;
		                                           });
		result["messages"].push_back(
		    {{"id", message.id},
		     {"sequence", delivery.sequence},
		     {"sender_system", message.value.sender_system},
		     {"sender_actor", message.value.sender_system},
		     {"sender_kind", controller_source ? "controller" : "system"},
		     {"sender_console", message.sender_console},
		     {"sender_console_key", source.console},
		     {"sender_console_label", label == console_labels_.end() ? source.console : label->second},
		     {"speaker_name", p.speaker_name},
		     {"speaker_portrait", p.speaker_portrait},
		     {"speaker_avatar", p.speaker_avatar},
		     {"origin", static_cast<unsigned>(message.value.origin)},
		     {"evidence_report", message.value.evidence_report},
		     {"destination_kind", static_cast<unsigned>(message.value.destination.kind)},
		     {"destination", message.value.destination.id},
		     {"sent_tick", message.sent_tick},
		     {"delivered_tick", delivery.delivered_tick},
		     {"actionable", delivery.actionable},
		     {"offer_only", delivery.offer_only},
		     {"public", delivery.public_copy},
		     {"human_seeking", message.value.human_seeking},
		     {"title_key", p.title_key},
		     {"body_key", p.body_key},
		     {"parameters", p.parameters},
		     {"urgency", p.urgency},
		     {"emergency", p.emergency},
		     {"human_visible", message.value.origin == ControlOrigin::AI},
		     {"recipient_console", address.console},
		     {"payload", knowledge_record_json(compiled_, {message.id, projection.console, KnowledgeScope::Console,
		                                                   message.value.payload, delivery.delivered_tick})}});
		if (!message.value.evidence_report &&
		    result["messages"].back().at("payload").value("schema", std::string{}) == "phoenix.coordination")
		{
			const auto field = compiled_.field(message.value.payload.schema, "request");
			if (const auto found = message.value.payload.fields.find(field);
			    found != message.value.payload.fields.end() && std::get<std::int64_t>(found->second) == 0)
				result["messages"].back()["parameters"]["request"] = std::to_string(message.id);
		}
	}
	for (const auto &delivery : projection.notifications)
	{
		for (const auto &message : result["messages"])
			if (message.at("sequence") == delivery.sequence)
			{
				result["notifications"].push_back(message);
				break;
			}
	}
	return result;
}
Json GameplayKnowledge::public_board(EntityId entity) const
{
	Json result{{"shared", Json::array()}, {"messages", Json::array()}, {"omitted", Json::object()}};
	if (!ships_.contains(entity))
		return result;
	std::map<KnowledgeId, Json> records, messages;
	for (const auto &[name, id] : ships_.at(entity).consoles)
	{
		// The board only consumes shared records and public message copies.
		// Private console telemetry histories can be large; do not copy and
		// serialize them just to discard them below.
		const auto projection = project_json({entity, name}, true);
		for (const auto &record : projection.at("shared"))
			records.emplace(record.at("id").get<KnowledgeId>(), record);
		for (auto message : projection.at("messages"))
			if (message.value("public", false))
			{
				message["actionable"] = false;
				message["offer_only"] = true;
				messages.emplace(message.at("id").get<KnowledgeId>(), std::move(message));
			}
	}
	auto append = [&](const char *name, const auto &values, std::size_t limit)
	{
		const auto omitted = values.size() > limit ? values.size() - limit : 0;
		result["omitted"][name] = omitted;
		std::size_t index = 0;
		for (const auto &[id, value] : values)
			if (index++ >= omitted)
				result[name].push_back(value);
	};
	append("shared", records, 128);
	// Camera settings are live ship state, not expendable report history.
	for (const auto &[id, record] : records)
		if (record.at("schema") == "phoenix.camera" && std::none_of(result["shared"].begin(), result["shared"].end(),
		                                                            [&](const auto &row)
		                                                            {
			                                                            return row.at("id") == id;
		                                                            }))
		{
			if (result["shared"].size() == 128)
				result["shared"].erase(0);
			result["shared"].push_back(record);
		}
	append("messages", messages, 64);
	return result;
}
Json GameplayKnowledge::save() const
{
	Json result{{"version", 1},
	            {"next_binding", next_binding_},
	            {"ships", Json::array()},
	            {"publications", Json::array()},
	            {"runtime", runtime_.save()}};
	for (const auto &[ship, binding] : ships_)
		result["ships"].push_back({{"ship", ship},
		                           {"definition", binding.definition},
		                           {"consoles", binding.consoles},
		                           {"stations", binding.stations},
		                           {"systems", binding.systems},
		                           {"controllers", binding.controllers}});
	for (const auto &[id, console] : publication_owners_)
		result["publications"].push_back({{"id", id}, {"console", console}});
	if (!reporter_contract_.empty())
	{
		result["version"] = 2;
		result["reporter_contract"] = reporter_contract_;
	}
	return result;
}
void GameplayKnowledge::restore(const Json &state)
{
	knowledge_require(state.at("version") == 1 || state.at("version") == 2, "unsupported adapter save version");
	knowledge_require(state.value("reporter_contract", Json::object()) == reporter_contract_,
	                  "saved reporter contract differs from loaded content");
	GameplayKnowledge restored;
	restored.compiled_ = compiled_;
	restored.runtime_ = KnowledgeRuntime(compiled_);
	restored.runtime_.track_record_changes(true);
	restored.definitions_ = definitions_;
	restored.messages_ = messages_;
	restored.console_labels_ = console_labels_;
	restored.request_keys_ = request_keys_;
	restored.telemetry_names_ = telemetry_names_;
	restored.telemetry_indices_ = telemetry_indices_;
	restored.reporter_contract_ = reporter_contract_;
	restored.next_binding_ = knowledge_unsigned(state.at("next_binding"));
	knowledge_require(restored.next_binding_ > 0 && restored.next_binding_ < std::numeric_limits<KnowledgeId>::max(),
	                  "invalid saved binding counter");
	std::set<KnowledgeId> ids;
	for (const auto &row : state.at("ships"))
	{
		GameplayKnowledgeShipBinding binding{
		    row.at("definition").get<std::string>(), knowledge_saved_bindings(row.at("consoles")),
		    knowledge_saved_bindings(row.at("stations")), knowledge_saved_bindings(row.at("systems")),
		    knowledge_saved_bindings(row.value("controllers", Json::object()))};
		for (const auto *bindings : {&binding.consoles, &binding.stations, &binding.systems, &binding.controllers})
			for (const auto &[name, id] : *bindings)
				knowledge_require(id > 0 && id < restored.next_binding_ && ids.insert(id).second,
				                  "invalid/duplicate saved named binding");
		restored.install_ship(knowledge_unsigned(row.at("ship")), std::move(binding), 0, false);
	}
	const auto expected = knowledge_registry_signature(restored.runtime_.save());
	auto saved_runtime = state.at("runtime");
	restored.runtime_.restore(saved_runtime);
	restored.sync_decision_payloads();
	knowledge_require(expected == knowledge_registry_signature(restored.runtime_.save()),
	                  "saved runtime does not match authored Console/System/Controller bindings");
	const auto runtime_state = restored.runtime_.save();
	if (!reporter_contract_.empty())
	{
		std::map<KnowledgeId, std::size_t> reporter_counts;
		std::set<KnowledgeId> legacy_sources;
		for (const auto &[ship, binding] : restored.ships_)
		{
			if (restored.definitions_.at(binding.definition).controllers.empty())
				for (const auto &[system, source] : binding.systems)
					legacy_sources.insert(source);
			for (const auto &[system, reports] : restored.definitions_.at(binding.definition).reporters)
				reporter_counts.emplace(binding.systems.at(system), reports.size());
		}
		for (const auto &status : runtime_state.value("status", Json::array()))
		{
			const auto producer = knowledge_unsigned(status.at("producer")), key = knowledge_unsigned(status.at("key"));
			knowledge_require(key > 0 && (legacy_sources.contains(producer) ||
			                              (reporter_counts.contains(producer) && key <= reporter_counts.at(producer))),
			                  "saved reporter latch is not owned by a configured physical reporter");
		}
	}
	std::map<KnowledgeId, KnowledgeId> expected_publications;
	for (const auto &row : runtime_state.value("publications", Json::array()))
		expected_publications.emplace(knowledge_unsigned(row.at("id")), knowledge_unsigned(row.at("console")));
	for (const auto &row : state.at("publications"))
	{
		const auto id = knowledge_unsigned(row.at("id")), console = knowledge_unsigned(row.at("console"));
		knowledge_require(restored.addresses_.contains(console) &&
		                      restored.publication_owners_.emplace(id, console).second,
		                  "invalid saved publication owner");
	}
	knowledge_require(expected_publications == restored.publication_owners_,
	                  "saved publication ownership differs from runtime");
	restored.clear_control();
	for (const auto &[ship, binding] : restored.ships_)
		if (!binding.consoles.empty())
		{
			const auto board = restored.public_board(ship);
			ship_camera_state(board); // Reject malformed saved camera values before committing restoration.
			if (std::none_of(board.at("shared").begin(), board.at("shared").end(),
			                 [](const auto &record)
			                 {
				                 return record.at("schema") == "phoenix.camera";
			                 }))
				restored.set_camera(ship, {}, saved_runtime.at("tick").get<KnowledgeTick>());
		}
	*this = std::move(restored);
}
} // namespace phoenix
