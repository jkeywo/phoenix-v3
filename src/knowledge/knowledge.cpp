module;
#include <span>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

module phoenix.knowledge;

namespace phoenix
{
namespace
{
void require(bool condition, const std::string &message)
{
	if (!condition)
		throw std::runtime_error("knowledge: " + message);
}
template <typename T> T read_unsigned(const Json &value)
{
	return static_cast<T>(checked_unsigned(value, 0, std::numeric_limits<T>::max(), "knowledge: identity/key/tick"));
}
template <typename T> std::vector<T> read_unsigned_vector(const Json &value)
{
	require(value.is_array(), "expected an identity/key array");
	std::vector<T> result;
	for (const auto &item : value)
		result.push_back(read_unsigned<T>(item));
	return result;
}
bool contains(const std::vector<KnowledgeKey> &items, KnowledgeKey value)
{
	return std::find(items.begin(), items.end(), value) != items.end();
}
Json encode_value(const KnowledgeValue &value)
{
	return std::visit(
	    [](const auto &item)
	    {
		    return Json(item);
	    },
	    value);
}
KnowledgeValue decode_value(const Json &value)
{
	if (value.is_boolean())
		return value.get<bool>();
	if (value.is_number_integer())
	{
		return checked_integer(value, std::numeric_limits<std::int64_t>::min(),
		                       std::numeric_limits<std::int64_t>::max(), "knowledge: integer field");
	}
	if (value.is_string())
		return value.get<std::string>();
	throw std::runtime_error("knowledge: unsupported scalar value");
}
Json contract_signature(const CompiledKnowledge &compiled)
{
	Json result{{"roles", compiled.roles}, {"schemas", Json::array()}};
	for (const auto &schema : compiled.schemas)
	{
		Json fields = Json::array();
		for (const auto &field : schema.fields)
			fields.push_back({{"name", field.name},
			                  {"type", static_cast<unsigned>(field.type)},
			                  {"required", field.required},
			                  {"publishable", field.publishable}});
		result["schemas"].push_back({{"name", schema.name},
		                             {"producers", schema.producers},
		                             {"readers", schema.readers},
		                             {"ship", schema.ship_publishable},
		                             {"stale", schema.stale_after},
		                             {"fields", fields}});
	}
	return result;
}
Json encode_input(const KnowledgeRecordInput &input)
{
	Json fields = Json::array();
	for (const auto &[key, value] : input.fields)
		fields.push_back({{"key", key}, {"value", encode_value(value)}});
	return {{"schema", input.schema},
	        {"producer", input.producer},
	        {"subject", input.subject},
	        {"source", input.provenance.source},
	        {"observed_tick", input.provenance.observed_tick},
	        {"revision", input.provenance.revision},
	        {"uncertainty", input.provenance.uncertainty},
	        {"kind", static_cast<unsigned>(input.provenance.kind)},
	        {"fields", fields},
	        {"withdrawn", input.withdrawn}};
}
KnowledgeRecordInput decode_input(const Json &value)
{
	require(value.at("uncertainty").is_number_integer(), "uncertainty must be an integer");
	KnowledgeRecordInput input;
	input.schema = read_unsigned<KnowledgeKey>(value.at("schema"));
	input.producer = read_unsigned<KnowledgeKey>(value.at("producer"));
	input.subject = read_unsigned<KnowledgeId>(value.at("subject"));
	input.provenance = {read_unsigned<KnowledgeId>(value.at("source")),
	                    read_unsigned<KnowledgeTick>(value.at("observed_tick")),
	                    read_unsigned<std::uint64_t>(value.at("revision")), value.at("uncertainty").get<std::int64_t>(),
	                    static_cast<EvidenceKind>(read_unsigned<unsigned>(value.at("kind")))};
	input.withdrawn = value.at("withdrawn").get<bool>();
	for (const auto &field : value.at("fields"))
		require(
		    input.fields.emplace(read_unsigned<KnowledgeKey>(field.at("key")), decode_value(field.at("value"))).second,
		    "duplicate saved field");
	return input;
}
Json encode_message(const CoordinationMessage &message)
{
	const auto &v = message.value;
	return {{"id", message.id},
	        {"sender_console", message.sender_console},
	        {"sender_ship", message.sender_ship},
	        {"sender_system", v.sender_system},
	        {"origin", static_cast<unsigned>(v.origin)},
	        {"destination_kind", static_cast<unsigned>(v.destination.kind)},
	        {"destination", v.destination.id},
	        {"payload", encode_input(v.payload)},
	        {"title", v.presentation.title_key},
	        {"body", v.presentation.body_key},
	        {"parameters", v.presentation.parameters},
	        {"urgency", v.presentation.urgency},
	        {"automatic", v.presentation.automatic},
	        {"speaker_name", v.presentation.speaker_name},
	        {"speaker_portrait", v.presentation.speaker_portrait},
	        {"speaker_avatar", v.presentation.speaker_avatar},
	        {"delay", v.delay},
	        {"expires", v.expires_tick},
	        {"request", v.request_key},
	        {"human_seeking", v.human_seeking},
	        {"sent", message.sent_tick},
	        {"delivery", message.delivery_tick},
	        {"recipients", message.recipients}};
}
CoordinationMessage decode_message(const Json &j)
{
	CoordinationMessage m;
	m.id = read_unsigned<KnowledgeId>(j.at("id"));
	m.sender_console = read_unsigned<KnowledgeId>(j.at("sender_console"));
	m.sender_ship = read_unsigned<KnowledgeId>(j.at("sender_ship"));
	m.value.sender_system = read_unsigned<KnowledgeId>(j.at("sender_system"));
	m.value.origin = static_cast<ControlOrigin>(read_unsigned<unsigned>(j.at("origin")));
	m.value.destination = {static_cast<MessageDestinationKind>(read_unsigned<unsigned>(j.at("destination_kind"))),
	                       read_unsigned<KnowledgeId>(j.at("destination"))};
	m.value.payload = decode_input(j.at("payload"));
	m.value.presentation = {j.at("title").get<std::string>(), j.at("body").get<std::string>(),
	                        j.at("parameters").get<std::map<std::string, std::string>>(),
	                        read_unsigned<std::uint8_t>(j.at("urgency")), j.at("automatic").get<bool>()};
	m.value.presentation.speaker_portrait = j.value("speaker_portrait", std::string{});
	require(m.value.presentation.speaker_portrait.empty() || m.value.presentation.speaker_portrait == "generic-crew" ||
	            (m.value.presentation.speaker_portrait.starts_with("console-assets:assets/comms/") &&
	             m.value.presentation.speaker_portrait.size() < 128),
	        "Invalid speaker portrait");
	m.value.presentation.speaker_name = j.value("speaker_name", std::string{});
	m.value.presentation.speaker_avatar = j.value("speaker_avatar", Bytes{});
	require(m.value.presentation.speaker_name.size() <= 64 && m.value.presentation.speaker_avatar.size() <= 12288,
	        "message speaker profile exceeds limit");
	m.value.delay = read_unsigned<KnowledgeTick>(j.at("delay"));
	m.value.expires_tick = read_unsigned<KnowledgeTick>(j.at("expires"));
	m.value.request_key = read_unsigned<KnowledgeId>(j.at("request"));
	m.value.human_seeking = j.value("human_seeking", false);
	m.sent_tick = read_unsigned<KnowledgeTick>(j.at("sent"));
	m.delivery_tick = read_unsigned<KnowledgeTick>(j.at("delivery"));
	m.recipients = read_unsigned_vector<KnowledgeId>(j.at("recipients"));
	return m;
}
} // namespace

KnowledgeRuntime::KnowledgeRuntime(CompiledKnowledge schema) : schema_(std::move(schema))
{
}
const CompiledKnowledge &KnowledgeRuntime::definition() const
{
	return schema_;
}
const KnowledgeSchema &KnowledgeRuntime::schema(KnowledgeKey key) const
{
	require(key > 0 && key <= schema_.schemas.size(), "unknown schema key");
	return schema_.schemas[key - 1];
}
void KnowledgeRuntime::validate(const KnowledgeRecordInput &input, bool publication, bool subset) const
{
	const auto &s = schema(input.schema);
	require(contains(s.producers, input.producer), s.name + ": producer is not permitted");
	require(input.provenance.source != 0 && input.provenance.uncertainty >= 0, s.name + ": invalid provenance");
	require(static_cast<unsigned>(input.provenance.kind) <= static_cast<unsigned>(EvidenceKind::Prediction),
	        "invalid evidence kind");
	for (const auto &[key, value] : input.fields)
	{
		require(key > 0 && key <= s.fields.size(), s.name + ": unknown field key");
		const auto &field = s.fields[key - 1];
		require((field.type == KnowledgeType::Integer && std::holds_alternative<std::int64_t>(value)) ||
		            (field.type == KnowledgeType::Boolean && std::holds_alternative<bool>(value)) ||
		            (field.type == KnowledgeType::Text && std::holds_alternative<std::string>(value)),
		        s.name + ": wrong field type: " + field.name);
		if (publication)
			require(field.publishable, s.name + ": field publication forbidden: " + field.name);
	}
	if (!publication && !subset && !input.withdrawn)
		for (const auto &field : s.fields)
			if (field.required)
				require(input.fields.contains(field.key), s.name + ": missing field: " + field.name);
}
void KnowledgeRuntime::register_console(ConsoleKnowledgeBinding binding)
{
	require(binding.id != 0 && binding.ship != 0 && binding.station != 0, "invalid Console binding");
	for (const auto key : binding.reader_roles)
		require(key > 0 && key <= schema_.roles.size(), "unknown reader role");
	if (const auto it = consoles_.find(binding.id); it != consoles_.end())
		require(it->second.ship == binding.ship && it->second.station == binding.station,
		        "cannot move a live Console between ships/stations");
	consoles_[binding.id] = std::move(binding);
}
void KnowledgeRuntime::register_system(KnowledgeId system, KnowledgeId console, bool human)
{
	require(system != 0 && consoles_.contains(console), "invalid System binding");
	if (const auto it = systems_.find(system); it != systems_.end())
		require(it->second == console, "conflicting System ownership");
	systems_[system] = console;
	system_human_[system] = human;
}
void KnowledgeRuntime::register_ai(KnowledgeId ai, KnowledgeId console)
{
	require(ai != 0 && consoles_.contains(console), "invalid AI binding");
	if (const auto it = memory_.find(ai); it != memory_.end())
		require(it->second.console == console, "conflicting AI ownership");
	else
		memory_.emplace(ai, KnowledgePrivateMemory{console, 0, {}});
}
void KnowledgeRuntime::register_controller(KnowledgeId controller, KnowledgeId console, bool human)
{
	require(!systems_.contains(controller) || controllers_.contains(controller),
	        "Controller identity conflicts with physical System");
	register_system(controller, console, human);
	controllers_.insert(controller);
}
void KnowledgeRuntime::set_human_control(KnowledgeId console, bool human)
{
	require(consoles_.contains(console), "unknown Console");
	consoles_.at(console).human_controlled = human;
}
void KnowledgeRuntime::set_system_human_control(KnowledgeId system, bool human)
{
	require(systems_.contains(system), "unknown System");
	system_human_.at(system) = human;
}
bool KnowledgeRuntime::readable(KnowledgeId console, KnowledgeKey key) const
{
	const auto it = consoles_.find(console);
	if (it == consoles_.end())
		return false;
	for (const auto role : it->second.reader_roles)
		if (contains(schema(key).readers, role))
			return true;
	return false;
}
bool KnowledgeRuntime::write(KnowledgeScope scope, KnowledgeId owner, KnowledgeRecordInput input, KnowledgeTick tick,
                             KnowledgeId publication)
{
	require(tick >= current_tick_, "cannot write evidence in the past");
	require(input.provenance.observed_tick <= tick, "observation is from the future");
	current_tick_ = tick;
	for (auto it = records_.rbegin(); it != records_.rend(); ++it)
		if (it->scope == scope && it->owner == owner && it->value.schema == input.schema &&
		    it->value.subject == input.subject && it->value.provenance.source == input.provenance.source &&
		    it->publication == publication)
		{
			const auto &old = it->value.provenance;
			if (publication != 0 && std::pair{input.provenance.observed_tick, input.provenance.revision} ==
			                            std::pair{old.observed_tick, old.revision})
			{
				require(it->value.withdrawn == input.withdrawn && it->value.producer == input.producer &&
				            old.kind == input.provenance.kind && old.uncertainty == input.provenance.uncertainty,
				        "conflicting equal-version published evidence");
				for (const auto &[key, value] : input.fields)
					if (const auto existing = it->value.fields.find(key); existing != it->value.fields.end())
						require(existing->second == value, "conflicting equal-version publication field");
				bool changed = false;
				for (auto &[key, value] : input.fields)
					changed |= it->value.fields.emplace(key, std::move(value)).second;
				return changed;
			}
			if (std::pair{input.provenance.observed_tick, input.provenance.revision} <=
			    std::pair{old.observed_tick, old.revision})
				return false;
			break;
		}
	require(next_id_ < std::numeric_limits<KnowledgeId>::max(), "record identity exhausted");
	records_.push_back({next_id_++, owner, scope, std::move(input), tick, publication});
	trim();
	return true;
}
bool KnowledgeRuntime::write_console(KnowledgeId console, KnowledgeRecordInput input, KnowledgeTick tick)
{
	validate(input);
	require(readable(console, input.schema), "record is not permitted on recipient Console");
	if (!write(KnowledgeScope::Console, console, std::move(input), tick))
		return false;
	const auto latest = records_.back();
	for (const auto &p : publications_)
		if (p.active && p.live && p.source_console == console && p.schema == latest.value.schema &&
		    p.subject == latest.value.subject)
		{
			auto copy = latest.value;
			std::erase_if(copy.fields,
			              [&](const auto &field)
			              {
				              return !contains(p.fields, field.first);
			              });
			validate(copy, true);
			if (p.target_console == 0)
				write(KnowledgeScope::Ship, consoles_.at(console).ship, std::move(copy), tick, p.id);
			else if (readable(p.target_console, copy.schema))
				write(KnowledgeScope::Console, p.target_console, std::move(copy), tick, p.id);
		}
	return true;
}
bool KnowledgeRuntime::write_ship(KnowledgeId ship, KnowledgeRecordInput input, KnowledgeTick tick)
{
	require(ship != 0, "invalid ship");
	require(schema(input.schema).ship_publishable, "schema forbids shipwide publication");
	validate(input, true);
	return write(KnowledgeScope::Ship, ship, std::move(input), tick);
}
KnowledgeId KnowledgeRuntime::publish(KnowledgeId console, KnowledgeKey key, KnowledgeId subject,
                                      std::vector<KnowledgeKey> fields, bool live, KnowledgeTick tick, bool execute)
{
	require(readable(console, key), "publisher cannot read source schema");
	require(!fields.empty(), "publication requires an explicit field subset");
	std::sort(fields.begin(), fields.end());
	require(std::adjacent_find(fields.begin(), fields.end()) == fields.end(), "duplicate publication field");
	const auto &s = schema(key);
	require(s.ship_publishable, s.name + ": ship publication forbidden");
	for (const auto field : fields)
		require(field > 0 && field <= s.fields.size() && s.fields[field - 1].publishable, "invalid publication field");
	require(tick >= current_tick_, "cannot publish in the past");
	const bool found = std::any_of(records_.begin(), records_.end(),
	                               [&](const auto &record)
	                               {
		                               return record.scope == KnowledgeScope::Console && record.owner == console &&
		                                      record.value.schema == key && record.value.subject == subject;
	                               });
	require(found || live, "no permitted source record to publish");
	require(next_id_ < std::numeric_limits<KnowledgeId>::max(), "publication identity exhausted");
	if (!execute)
	{
		for (const auto &record : records_)
			if (record.scope == KnowledgeScope::Console && record.owner == console && record.value.schema == key &&
			    record.value.subject == subject)
			{
				auto copy = record.value;
				std::erase_if(copy.fields,
				              [&](const auto &field)
				              {
					              return !contains(fields, field.first);
				              });
				validate(copy, true);
			}
		return 0;
	}
	const auto source = records_;
	const auto id = next_id_++;
	publications_.push_back({id, console, subject, key, fields, live, true});
	for (const auto &record : source)
		if (record.scope == KnowledgeScope::Console && record.owner == console && record.value.schema == key &&
		    record.value.subject == subject)
		{
			auto copy = record.value;
			std::erase_if(copy.fields,
			              [&](const auto &field)
			              {
				              return !contains(fields, field.first);
			              });
			validate(copy, true);
			write(KnowledgeScope::Ship, consoles_.at(console).ship, std::move(copy), tick, id);
		}
	current_tick_ = tick;
	return id;
}
KnowledgeId KnowledgeRuntime::feed(KnowledgeId source_console, KnowledgeId target_console, KnowledgeKey key,
                                   KnowledgeId subject, std::vector<KnowledgeKey> fields, KnowledgeTick tick)
{
	require(readable(source_console, key) && readable(target_console, key), "scoped feed reader permission mismatch");
	require(source_console != target_console && consoles_.at(source_console).ship == consoles_.at(target_console).ship,
	        "scoped feeds require distinct Consoles on the same ship");
	const auto &s = schema(key);
	require(!fields.empty(), "scoped feed requires explicit fields");
	std::sort(fields.begin(), fields.end());
	require(std::adjacent_find(fields.begin(), fields.end()) == fields.end(), "duplicate feed field");
	for (const auto field : fields)
		require(field > 0 && field <= s.fields.size() && s.fields[field - 1].publishable, "invalid feed field");
	require(tick >= current_tick_, "cannot create feed in the past");
	require(next_id_ < std::numeric_limits<KnowledgeId>::max(), "feed identity exhausted");
	const auto id = next_id_++;
	publications_.push_back({id, source_console, subject, key, std::move(fields), true, true, target_console});
	// A feed copies only dated permitted evidence; registration creates no world observation.
	const auto source = records_;
	for (const auto &record : source)
		if (record.scope == KnowledgeScope::Console && record.owner == source_console && record.value.schema == key &&
		    record.value.subject == subject)
		{
			auto copy = record.value;
			std::erase_if(copy.fields,
			              [&](const auto &field)
			              {
				              return !contains(publications_.back().fields, field.first);
			              });
			write(KnowledgeScope::Console, target_console, std::move(copy), tick, id);
		}
	current_tick_ = tick;
	return id;
}
void KnowledgeRuntime::stop_publication(KnowledgeId publication)
{
	for (auto &p : publications_)
		if (p.id == publication)
		{
			p.active = false;
			return;
		}
	throw std::runtime_error("knowledge: unknown publication");
}
std::vector<KnowledgeRecord> KnowledgeRuntime::read_console(KnowledgeId console) const
{
	require(consoles_.contains(console), "unknown Console");
	std::vector<KnowledgeRecord> result;
	for (const auto &record : records_)
		if (record.scope == KnowledgeScope::Console && record.owner == console &&
		    readable(console, record.value.schema))
			result.push_back(record);
	return result;
}
std::vector<KnowledgeRecord> KnowledgeRuntime::read_ship(KnowledgeId console) const
{
	require(consoles_.contains(console), "unknown Console");
	std::vector<KnowledgeRecord> result;
	for (const auto &record : records_)
		if (record.scope == KnowledgeScope::Ship && record.owner == consoles_.at(console).ship)
			result.push_back(record);
	return result;
}
bool KnowledgeRuntime::stale(const KnowledgeRecord &record, KnowledgeTick now) const
{
	const auto duration = schema(record.value.schema).stale_after;
	return duration != 0 && now > record.value.provenance.observed_tick &&
	       now - record.value.provenance.observed_tick > duration;
}
KnowledgePrivateMemory &KnowledgeRuntime::private_memory(KnowledgeId ai)
{
	require(memory_.contains(ai), "unknown AI");
	return memory_.at(ai);
}

std::vector<KnowledgeId> KnowledgeRuntime::resolve_recipients(KnowledgeId ship, const MessageDestination &destination,
                                                              KnowledgeKey key) const
{
	std::vector<KnowledgeId> recipients;
	switch (destination.kind)
	{
	case MessageDestinationKind::System:
		require(systems_.contains(destination.id) && !controllers_.contains(destination.id),
		        "unknown destination System");
		recipients.push_back(systems_.at(destination.id));
		break;
	case MessageDestinationKind::Controller:
		require(controllers_.contains(destination.id), "unknown destination Controller");
		recipients.push_back(systems_.at(destination.id));
		break;
	case MessageDestinationKind::Console:
		require(consoles_.contains(destination.id), "unknown destination Console");
		recipients.push_back(destination.id);
		break;
	case MessageDestinationKind::Station:
		for (const auto &[id, binding] : consoles_)
			if (binding.ship == ship && binding.station == destination.id)
				recipients.push_back(id);
		require(!recipients.empty(), "unknown destination Station");
		break;
	case MessageDestinationKind::Ship:
		require(destination.id == ship, "inter-ship Coordination is unsupported");
		for (const auto &[id, binding] : consoles_)
			if (binding.ship == ship)
				recipients.push_back(id);
		break;
	default:
		require(false, "invalid destination kind");
	}
	for (const auto console : recipients)
	{
		require(consoles_.at(console).ship == ship, "inter-ship Coordination is unsupported");
		require((destination.kind == MessageDestinationKind::Ship && schema(key).ship_publishable) ||
		            readable(console, key),
		        "message payload forbidden to destination Console");
	}
	require(!recipients.empty(), "destination has no eligible recipient");
	return recipients;
}
bool KnowledgeRuntime::public_message_candidate(const CoordinationInput &input) const
{
	if (input.destination.kind == MessageDestinationKind::Ship)
		return true;
	return controllers_.contains(input.sender_system) && input.origin == ControlOrigin::AI &&
	       input.destination.kind == MessageDestinationKind::Controller && input.presentation.urgency >= 2;
}
KnowledgeId KnowledgeRuntime::send(CoordinationInput input, KnowledgeTick tick, bool execute)
{
	validate(input.payload);
	require(tick >= current_tick_, "cannot send a message in the past");
	require(input.payload.provenance.observed_tick <= tick, "message evidence is from the future");
	require(systems_.contains(input.sender_system), "unknown sender System");
	require(static_cast<unsigned>(input.origin) <= static_cast<unsigned>(ControlOrigin::Reporter),
	        "invalid sender origin");
	if (input.origin == ControlOrigin::Reporter)
		require(!controllers_.contains(input.sender_system) && input.payload.provenance.kind == EvidenceKind::Fact &&
		            input.payload.provenance.source == input.sender_system,
		        "physical reporters can only emit their own facts");
	require(input.presentation.urgency <= 3, "message urgency must be 0..3");
	const auto sender_console = systems_.at(input.sender_system);
	const auto ship = consoles_.at(sender_console).ship;
	auto recipients = resolve_recipients(ship, input.destination, input.payload.schema);
	if (public_message_candidate(input))
	{
		require(schema(input.payload.schema).ship_publishable, "public message schema is not publishable");
		validate(input.payload, true);
	}
	const auto delay = std::max(KnowledgeTick{1}, input.delay);
	require(tick <= std::numeric_limits<KnowledgeTick>::max() - delay, "message delivery tick overflow");
	require(input.expires_tick == 0 || input.expires_tick > tick + delay, "message expires before delivery");
	require(next_id_ < std::numeric_limits<KnowledgeId>::max(), "message identity exhausted");
	if (!execute)
		return 0;
	if (input.request_key != 0)
		withdraw_request(input.sender_system, input.request_key);
	const auto id = next_id_++;
	if ((controllers_.contains(input.sender_system) && input.payload.provenance.kind != EvidenceKind::Fact) ||
	    input.origin == ControlOrigin::Reporter)
		input.payload.provenance.revision = id;
	pending_.push_back({id, sender_console, ship, std::move(input), tick, tick + delay, std::move(recipients)});
	current_tick_ = tick;
	return id;
}
bool KnowledgeRuntime::withdraw_request(KnowledgeId sender, KnowledgeId key)
{
	require(systems_.contains(sender) && key != 0, "invalid request withdrawal");
	bool changed = false;
	for (const auto &delivery : deliveries_)
		if (delivery.message.value.sender_system == sender && delivery.message.value.request_key == key)
			changed |= inactive_requests_.insert(delivery.message.id).second;
	std::erase_if(pending_,
	              [&](const auto &message)
	              {
		              const bool matches = message.value.sender_system == sender && message.value.request_key == key;
		              changed |= matches;
		              return matches;
	              });
	return changed;
}
std::vector<CoordinationDelivery> KnowledgeRuntime::advance(KnowledgeTick tick)
{
	require(tick >= current_tick_, "cannot reverse delivery time");
	current_tick_ = tick;
	std::sort(pending_.begin(), pending_.end(),
	          [](const auto &a, const auto &b)
	          {
		          return std::pair{a.delivery_tick, a.id} < std::pair{b.delivery_tick, b.id};
	          });
	std::vector<CoordinationDelivery> result;
	for (const auto &message : pending_)
	{
		if (message.delivery_tick > tick)
			continue;
		if (message.value.expires_tick != 0 && tick >= message.value.expires_tick)
			continue;
		std::map<KnowledgeId, bool> targets;
		const bool publish = public_message_candidate(message.value) &&
		                     (message.value.destination.kind == MessageDestinationKind::Ship ||
		                      !system_human_.at(message.value.destination.id));
		if (publish)
			write_ship(message.sender_ship, message.value.payload, tick);
		for (const auto console : message.recipients)
			targets.emplace(console, false);
		if (publish)
			for (const auto &[id, binding] : consoles_)
				if (binding.ship == message.sender_ship)
					targets.try_emplace(id, true);
		if (message.value.human_seeking)
			for (const auto &[id, binding] : consoles_)
				if (binding.ship == message.sender_ship && binding.human_controlled &&
				    readable(id, message.value.payload.schema))
					targets.try_emplace(id, true);
		for (const auto &[console, offer_only] : targets)
		{
			const bool private_reader = readable(console, message.value.payload.schema);
			if (!publish && !private_reader)
				continue;
			if (!offer_only && private_reader)
				write_console(console, message.value.payload, tick);
			const auto &v = message.value;
			const auto addressed_human = (v.destination.kind == MessageDestinationKind::System ||
			                              v.destination.kind == MessageDestinationKind::Controller)
			                                 ? system_human_.at(v.destination.id)
			                                 : consoles_.at(console).human_controlled;
			const auto notify = (addressed_human || v.human_seeking || publish) &&
			                    consoles_.at(console).human_controlled &&
			                    !(v.origin == ControlOrigin::Human && v.presentation.automatic);
			require(next_delivery_ < std::numeric_limits<KnowledgeId>::max(), "delivery identity exhausted");
			CoordinationDelivery delivery{next_delivery_++, console, message, tick, notify};
			delivery.offer_only = offer_only;
			delivery.public_copy = publish;
			deliveries_.push_back(delivery);
			result.push_back(std::move(delivery));
		}
	}
	std::erase_if(pending_,
	              [&](const auto &message)
	              {
		              return message.delivery_tick <= tick ||
		                     (message.value.expires_tick != 0 && tick >= message.value.expires_tick);
	              });
	trim();
	return result;
}
std::vector<CoordinationDelivery> KnowledgeRuntime::message_history(KnowledgeId console) const
{
	require(consoles_.contains(console), "unknown Console");
	std::vector<CoordinationDelivery> result;
	for (const auto &delivery : deliveries_)
		if (delivery.console == console &&
		    (delivery.public_copy || readable(console, delivery.message.value.payload.schema)))
		{
			auto copy = delivery;
			copy.notify_human = false;
			copy.actionable = !(copy.public_copy && copy.offer_only) && !inactive_requests_.contains(copy.message.id) &&
			                  (copy.message.value.expires_tick == 0 || current_tick_ < copy.message.value.expires_tick);
			result.push_back(std::move(copy));
		}
	return result;
}
std::vector<CoordinationDelivery> KnowledgeRuntime::receive(KnowledgeId ai)
{
	auto &memory = private_memory(ai);
	auto result = message_history(memory.console);
	std::erase_if(result,
	              [&](const auto &delivery)
	              {
		              return delivery.sequence <= memory.receive_cursor;
	              });
	if (!result.empty())
		memory.receive_cursor = result.back().sequence;
	std::erase_if(result,
	              [ai](const auto &delivery)
	              {
		              return !delivery.actionable || delivery.offer_only ||
		                     ((delivery.message.value.destination.kind == MessageDestinationKind::Controller ||
		                       delivery.message.value.destination.kind == MessageDestinationKind::System) &&
		                      delivery.message.value.destination.id != ai);
	              });
	return result;
}
std::vector<CoordinationMessage> KnowledgeRuntime::pending_messages(std::size_t limit) const
{
	limit = std::min(limit, std::size_t{256});
	std::vector<CoordinationMessage> result;
	if (limit == 0)
		return result;
	result.reserve(std::min(limit, pending_.size()));
	const auto before = [](const auto &a, const auto &b)
	{
		return std::pair{a.delivery_tick, a.id} < std::pair{b.delivery_tick, b.id};
	};
	for (const auto &message : pending_)
	{
		const auto position =
		    static_cast<std::size_t>(std::lower_bound(result.begin(), result.end(), message, before) - result.begin());
		if (position >= limit)
			continue;
		if (result.size() == limit)
			result.pop_back();
		result.insert(result.begin() + static_cast<std::ptrdiff_t>(position), message);
	}
	return result;
}
bool KnowledgeRuntime::status_change(KnowledgeId producer, KnowledgeKey status, bool value, KnowledgeTick tick,
                                     KnowledgeTick debounce, KnowledgeTick recovery_hysteresis,
                                     KnowledgeTick minimum_gap, bool report_initial)
{
	require(producer != 0 && status != 0, "invalid status producer/key");
	require(tick >= current_tick_, "cannot report status in the past");
	current_tick_ = tick;
	auto &latch = status_[{producer, status}];
	if (!latch.initialised)
	{
		latch = {true, report_initial ? value : false, value, tick, tick, report_initial};
		if (report_initial)
			return true;
	}
	require(tick >= latch.last_report && tick >= latch.candidate_since, "status time reversed");
	if (value != latch.candidate)
	{
		latch.candidate = value;
		latch.candidate_since = tick;
	}
	const auto wait = value ? debounce : recovery_hysteresis;
	if (value == latch.reported || tick - latch.candidate_since < wait ||
	    (latch.reported_once && tick - latch.last_report < minimum_gap))
		return false;
	latch.reported = value;
	latch.last_report = tick;
	latch.reported_once = true;
	return true;
}
void KnowledgeRuntime::trim()
{
	// Retain a bounded history per board and Console; this is storage policy, never send bandwidth.
	std::map<std::pair<unsigned, KnowledgeId>, std::size_t> counts;
	using RecordKey = std::tuple<unsigned, KnowledgeId, KnowledgeKey, KnowledgeId, KnowledgeId, KnowledgeId>;
	std::set<RecordKey> current;
	for (std::size_t i = records_.size(); i > 0; --i)
	{
		const auto &record = records_[i - 1];
		const auto scope = static_cast<unsigned>(record.scope);
		// Keep the latest observation or withdrawal for every record key. Otherwise
		// history eviction could make an old report authoritative on its next arrival.
		if (current
		        .insert({scope, record.owner, record.value.schema, record.value.subject, record.value.provenance.source,
		                 record.publication})
		        .second)
			continue;
		if (++counts[{scope, record.owner}] > schema_.history_limit)
			records_.erase(records_.begin() + static_cast<std::ptrdiff_t>(i - 1));
	}
	std::map<KnowledgeId, std::size_t> messages;
	for (std::size_t i = deliveries_.size(); i > 0; --i)
		if (++messages[deliveries_[i - 1].console] > schema_.history_limit)
			deliveries_.erase(deliveries_.begin() + static_cast<std::ptrdiff_t>(i - 1));
	std::set<KnowledgeId> retained;
	for (const auto &delivery : deliveries_)
		retained.insert(delivery.message.id);
	std::erase_if(inactive_requests_,
	              [&](const auto id)
	              {
		              return !retained.contains(id);
	              });
}

Json KnowledgeRuntime::save() const
{
	Json j{{"version", 2},          {"contract", contract_signature(schema_)},
	       {"next_id", next_id_},   {"next_delivery", next_delivery_},
	       {"tick", current_tick_}, {"inactive_requests", inactive_requests_}};
	for (const auto &[id, console] : consoles_)
		j["consoles"].push_back({{"id", id},
		                         {"ship", console.ship},
		                         {"station", console.station},
		                         {"roles", console.reader_roles},
		                         {"human", console.human_controlled}});
	for (const auto &[id, console] : systems_)
	{
		Json actor{{"id", id}, {"console", console}, {"human", system_human_.at(id)}};
		if (controllers_.contains(id))
			actor["controller"] = true;
		j["systems"].push_back(std::move(actor));
	}
	if (!controllers_.empty())
		j["version"] = 3;
	for (const auto &[id, memory] : memory_)
	{
		Json values = Json::array();
		for (const auto &[key, value] : memory.values)
			values.push_back({{"key", key}, {"value", encode_value(value)}});
		j["memory"].push_back(
		    {{"id", id}, {"console", memory.console}, {"cursor", memory.receive_cursor}, {"values", values}});
	}
	for (const auto &record : records_)
		j["records"].push_back({{"id", record.id},
		                        {"owner", record.owner},
		                        {"scope", static_cast<unsigned>(record.scope)},
		                        {"received", record.received_tick},
		                        {"publication", record.publication},
		                        {"value", encode_input(record.value)}});
	for (const auto &p : publications_)
		j["publications"].push_back({{"id", p.id},
		                             {"console", p.source_console},
		                             {"subject", p.subject},
		                             {"schema", p.schema},
		                             {"fields", p.fields},
		                             {"live", p.live},
		                             {"active", p.active},
		                             {"target", p.target_console}});
	for (const auto &message : pending_)
		j["pending"].push_back(encode_message(message));
	for (const auto &delivery : deliveries_)
		j["deliveries"].push_back({{"sequence", delivery.sequence},
		                           {"console", delivery.console},
		                           {"tick", delivery.delivered_tick},
		                           {"offer_only", delivery.offer_only},
		                           {"public_copy", delivery.public_copy},
		                           {"message", encode_message(delivery.message)}});
	for (const auto &[key, latch] : status_)
		j["status"].push_back({{"producer", key.first},
		                       {"key", key.second},
		                       {"initialised", latch.initialised},
		                       {"reported", latch.reported},
		                       {"reported_once", latch.reported_once},
		                       {"candidate", latch.candidate},
		                       {"since", latch.candidate_since},
		                       {"last", latch.last_report}});
	return j;
}
void KnowledgeRuntime::restore(const Json &j)
{
	require(j.at("version") == 2 || j.at("version") == 3, "unsupported save version");
	auto loaded_contract = contract_signature(schema_);
	auto saved_contract = j.at("contract");
	if (saved_contract.is_object() && saved_contract.contains("schemas") && saved_contract.at("schemas").is_array() &&
	    saved_contract.at("schemas").size() == loaded_contract.at("schemas").size())
		for (std::size_t i = 0; i < loaded_contract.at("schemas").size(); ++i)
		{
			auto &saved = saved_contract["schemas"][i];
			const auto &loaded = loaded_contract.at("schemas")[i];
			if (saved.contains("fields") && saved.at("fields").is_array() &&
			    saved.at("fields").size() == loaded.at("fields").size())
				for (std::size_t f = 0; f < loaded.at("fields").size(); ++f)
					if (loaded.at("fields")[f].at("required") == false && saved["fields"][f].value("required", true))
						saved["fields"][f]["required"] = false;
		}
	require(saved_contract == loaded_contract, "saved schema contract differs from loaded content");
	KnowledgeRuntime restored(schema_);
	restored.next_id_ = read_unsigned<KnowledgeId>(j.at("next_id"));
	restored.next_delivery_ = read_unsigned<KnowledgeId>(j.at("next_delivery"));
	restored.current_tick_ = read_unsigned<KnowledgeTick>(j.at("tick"));
	require(restored.next_id_ > 0 && restored.next_id_ < std::numeric_limits<KnowledgeId>::max() &&
	            restored.next_delivery_ > 0 && restored.next_delivery_ < std::numeric_limits<KnowledgeId>::max(),
	        "invalid saved identity counters");
	for (const auto &v : j.value("consoles", Json::array()))
	{
		const auto id = read_unsigned<KnowledgeId>(v.at("id"));
		require(!restored.consoles_.contains(id), "duplicate saved Console");
		restored.register_console({id, read_unsigned<KnowledgeId>(v.at("ship")),
		                           read_unsigned<KnowledgeId>(v.at("station")),
		                           read_unsigned_vector<KnowledgeKey>(v.at("roles")), v.at("human").get<bool>()});
	}
	for (const auto &v : j.value("systems", Json::array()))
	{
		const auto id = read_unsigned<KnowledgeId>(v.at("id"));
		require(!restored.systems_.contains(id), "duplicate saved System");
		if (v.value("controller", false))
		{
			require(j.at("version") == 3, "Controller identity requires save version 3");
			restored.register_controller(id, read_unsigned<KnowledgeId>(v.at("console")), v.at("human").get<bool>());
		}
		else
			restored.register_system(id, read_unsigned<KnowledgeId>(v.at("console")), v.at("human").get<bool>());
	}
	for (const auto &v : j.value("memory", Json::array()))
	{
		const auto id = read_unsigned<KnowledgeId>(v.at("id"));
		require(!restored.memory_.contains(id), "duplicate saved AI");
		restored.register_ai(id, read_unsigned<KnowledgeId>(v.at("console")));
		auto &memory = restored.memory_.at(id);
		memory.receive_cursor = read_unsigned<KnowledgeId>(v.at("cursor"));
		require(memory.receive_cursor < restored.next_delivery_, "invalid saved AI receive cursor");
		for (const auto &field : v.at("values"))
		{
			const auto key = read_unsigned<KnowledgeKey>(field.at("key"));
			require(key != 0 && memory.values.emplace(key, decode_value(field.at("value"))).second,
			        "invalid or duplicate private memory key");
		}
	}
	std::set<KnowledgeId> ids;
	auto check_id = [&](KnowledgeId id)
	{
		require(id > 0 && id < restored.next_id_ && ids.insert(id).second, "invalid or duplicate saved identity");
	};
	using RecordKey = std::tuple<unsigned, KnowledgeId, KnowledgeKey, KnowledgeId, KnowledgeId, KnowledgeId>;
	std::map<RecordKey, std::pair<KnowledgeTick, std::uint64_t>> versions;
	for (const auto &v : j.value("records", Json::array()))
	{
		KnowledgeRecord r{read_unsigned<KnowledgeId>(v.at("id")),
		                  read_unsigned<KnowledgeId>(v.at("owner")),
		                  static_cast<KnowledgeScope>(read_unsigned<unsigned>(v.at("scope"))),
		                  decode_input(v.at("value")),
		                  read_unsigned<KnowledgeTick>(v.at("received")),
		                  read_unsigned<KnowledgeId>(v.at("publication"))};
		check_id(r.id);
		require(r.scope == KnowledgeScope::Console || r.scope == KnowledgeScope::Ship, "invalid saved record scope");
		restored.validate(r.value, r.scope == KnowledgeScope::Ship, r.publication != 0);
		if (r.scope == KnowledgeScope::Ship)
			require(r.owner != 0 && restored.schema(r.value.schema).ship_publishable,
			        "saved ship publication permission mismatch");
		else
			require(restored.readable(r.owner, r.value.schema), "saved record Console permission mismatch");
		require(r.value.provenance.observed_tick <= r.received_tick && r.received_tick <= restored.current_tick_,
		        "saved observation from future");
		const RecordKey key{static_cast<unsigned>(r.scope), r.owner,      r.value.schema, r.value.subject,
		                    r.value.provenance.source,      r.publication};
		const auto version = std::pair{r.value.provenance.observed_tick, r.value.provenance.revision};
		if (const auto old = versions.find(key); old != versions.end())
			require(old->second < version, "saved evidence versions are not increasing");
		versions[key] = version;
		restored.records_.push_back(std::move(r));
	}
	for (const auto &v : j.value("publications", Json::array()))
	{
		KnowledgePublication p{read_unsigned<KnowledgeId>(v.at("id")),
		                       read_unsigned<KnowledgeId>(v.at("console")),
		                       read_unsigned<KnowledgeId>(v.at("subject")),
		                       read_unsigned<KnowledgeKey>(v.at("schema")),
		                       read_unsigned_vector<KnowledgeKey>(v.at("fields")),
		                       v.at("live").get<bool>(),
		                       v.at("active").get<bool>(),
		                       read_unsigned<KnowledgeId>(v.at("target"))};
		check_id(p.id);
		require(restored.readable(p.source_console, p.schema), "saved publication permission mismatch");
		const auto &schema = restored.schema(p.schema);
		require((p.target_console != 0 || schema.ship_publishable) && !p.fields.empty(), "invalid saved publication");
		if (p.target_console != 0)
			require(p.target_console != p.source_console && p.live && restored.readable(p.target_console, p.schema) &&
			            restored.consoles_.at(p.target_console).ship == restored.consoles_.at(p.source_console).ship,
			        "invalid saved scoped feed");
		require(std::is_sorted(p.fields.begin(), p.fields.end()) &&
		            std::adjacent_find(p.fields.begin(), p.fields.end()) == p.fields.end(),
		        "invalid saved publication field order");
		for (const auto field : p.fields)
			require(field > 0 && field <= schema.fields.size() && schema.fields[field - 1].publishable,
			        "invalid saved publication field");
		restored.publications_.push_back(std::move(p));
	}
	for (const auto &r : restored.records_)
		if (r.publication != 0)
		{
			const auto p = std::find_if(restored.publications_.begin(), restored.publications_.end(),
			                            [&](const auto &item)
			                            {
				                            return item.id == r.publication;
			                            });
			require(p != restored.publications_.end() && p->schema == r.value.schema && p->subject == r.value.subject,
			        "saved record publication mismatch");
			require(p->target_console == 0
			            ? r.scope == KnowledgeScope::Ship && r.owner == restored.consoles_.at(p->source_console).ship
			            : r.scope == KnowledgeScope::Console && r.owner == p->target_console,
			        "saved publication destination mismatch");
			for (const auto &[field, value] : r.value.fields)
				require(contains(p->fields, field), "saved publication field scope mismatch");
		}
	auto check_message = [&](const CoordinationMessage &m)
	{
		restored.validate(m.value.payload);
		require(m.id > 0 && m.id < restored.next_id_, "saved message identity exceeds counter");
		require(restored.systems_.contains(m.value.sender_system) &&
		            restored.systems_.at(m.value.sender_system) == m.sender_console &&
		            restored.consoles_.at(m.sender_console).ship == m.sender_ship,
		        "saved message sender mismatch");
		if (restored.public_message_candidate(m.value))
		{
			require(restored.schema(m.value.payload.schema).ship_publishable, "saved public message schema is private");
			restored.validate(m.value.payload, true);
		}
		const auto delay = std::max(KnowledgeTick{1}, m.value.delay);
		require(m.sent_tick <= restored.current_tick_ &&
		            m.sent_tick <= std::numeric_limits<KnowledgeTick>::max() - delay &&
		            m.delivery_tick == m.sent_tick + delay && m.value.payload.provenance.observed_tick <= m.sent_tick &&
		            static_cast<unsigned>(m.value.origin) <= static_cast<unsigned>(ControlOrigin::Reporter) &&
		            m.value.presentation.urgency <= 3,
		        "invalid saved delivery timing/origin/urgency");
		if (m.value.origin == ControlOrigin::Reporter)
			require(!restored.controllers_.contains(m.value.sender_system) &&
			            m.value.payload.provenance.kind == EvidenceKind::Fact &&
			            m.value.payload.provenance.source == m.value.sender_system,
			        "invalid saved physical report");
		require(m.value.expires_tick == 0 || m.value.expires_tick > m.delivery_tick,
		        "saved message expires before delivery");
		require(m.recipients == restored.resolve_recipients(m.sender_ship, m.value.destination, m.value.payload.schema),
		        "saved destination does not match resolved recipients");
	};
	std::set<KnowledgeId> pending_ids;
	for (const auto &v : j.value("pending", Json::array()))
	{
		auto m = decode_message(v);
		check_id(m.id);
		check_message(m);
		pending_ids.insert(m.id);
		restored.pending_.push_back(std::move(m));
	}
	std::map<KnowledgeId, Json> message_versions;
	std::set<std::pair<KnowledgeId, KnowledgeId>> delivered_recipients;
	KnowledgeId previous_sequence = 0;
	for (const auto &v : j.value("deliveries", Json::array()))
	{
		CoordinationDelivery d{read_unsigned<KnowledgeId>(v.at("sequence")),
		                       read_unsigned<KnowledgeId>(v.at("console")), decode_message(v.at("message")),
		                       read_unsigned<KnowledgeTick>(v.at("tick")), false};
		d.offer_only = v.value("offer_only", false);
		d.public_copy = v.value("public_copy", false);
		check_message(d.message);
		require(!d.public_copy || restored.public_message_candidate(d.message.value),
		        "invalid saved public message copy");
		require(delivered_recipients.insert({d.message.id, d.console}).second,
		        "duplicate saved message delivery to Console");
		require(!pending_ids.contains(d.message.id), "message is both pending and delivered");
		if (const auto old = message_versions.find(d.message.id); old != message_versions.end())
			require(old->second == encode_message(d.message), "conflicting saved message copies");
		else
		{
			check_id(d.message.id);
			message_versions[d.message.id] = encode_message(d.message);
		}
		const bool addressed = std::find(d.message.recipients.begin(), d.message.recipients.end(), d.console) !=
		                       d.message.recipients.end();
		require(restored.consoles_.contains(d.console), "saved delivery Console is unknown");
		const bool valid_offer = d.offer_only && !addressed && (d.message.value.human_seeking || d.public_copy) &&
		                         restored.consoles_.at(d.console).ship == d.message.sender_ship &&
		                         (d.public_copy || restored.readable(d.console, d.message.value.payload.schema));
		require(d.sequence > previous_sequence && d.sequence < restored.next_delivery_ &&
		            (d.offer_only ? valid_offer : addressed) && d.delivered_tick >= d.message.delivery_tick &&
		            d.delivered_tick <= restored.current_tick_ &&
		            (d.message.value.expires_tick == 0 || d.delivered_tick < d.message.value.expires_tick),
		        "invalid saved delivery");
		previous_sequence = d.sequence;
		restored.deliveries_.push_back(std::move(d));
	}
	for (const auto id : read_unsigned_vector<KnowledgeId>(j.at("inactive_requests")))
	{
		require(message_versions.contains(id) && message_versions.at(id).at("request") != 0 &&
		            restored.inactive_requests_.insert(id).second,
		        "invalid or duplicate saved inactive request");
	}
	std::map<std::pair<KnowledgeId, KnowledgeId>, KnowledgeId> active_requests;
	auto check_request = [&](const CoordinationMessage &m)
	{
		if (m.value.request_key == 0 || restored.inactive_requests_.contains(m.id) ||
		    (m.value.expires_tick != 0 && m.value.expires_tick <= restored.current_tick_))
			return;
		const auto key = std::pair{m.value.sender_system, m.value.request_key};
		if (const auto old = active_requests.find(key); old != active_requests.end())
			require(old->second == m.id, "multiple active saved requests share a replacement key");
		else
			active_requests.emplace(key, m.id);
	};
	for (const auto &m : restored.pending_)
		check_request(m);
	for (const auto &d : restored.deliveries_)
		check_request(d.message);
	for (const auto &v : j.value("status", Json::array()))
	{
		const auto key =
		    std::pair{read_unsigned<KnowledgeId>(v.at("producer")), read_unsigned<KnowledgeKey>(v.at("key"))};
		KnowledgeStatusLatch latch{v.at("initialised").get<bool>(),
		                           v.at("reported").get<bool>(),
		                           v.at("candidate").get<bool>(),
		                           read_unsigned<KnowledgeTick>(v.at("since")),
		                           read_unsigned<KnowledgeTick>(v.at("last")),
		                           v.value("reported_once", true)};
		require(key.first != 0 && key.second != 0 && latch.initialised &&
		            latch.candidate_since <= restored.current_tick_ && latch.last_report <= restored.current_tick_ &&
		            restored.status_.emplace(key, latch).second,
		        "invalid or duplicate saved status latch");
	}
	restored.trim();
	*this = std::move(restored);
}
} // namespace phoenix
