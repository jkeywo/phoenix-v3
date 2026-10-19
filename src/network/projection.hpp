#pragma once
namespace phoenix::network
{
static inline Json body_json(const BodyView &body)
{
	return {{"id", body.id},
	        {"definition", body.definition},
	        {"x", body.x},
	        {"y", body.y},
	        {"vx", body.vx},
	        {"vy", body.vy},
	        {"heading", body.heading},
	        {"radar_icon", std::string(radar_icon_name(body.radar_icon))},
	        {"radar_sprite", body.radar_sprite}};
}
static inline BodyView read_body(const Json &value)
{
	BodyView body{value.at("id"), value.at("definition"), value.at("x"),      value.at("y"),
	              value.at("vx"), value.at("vy"),         value.at("heading")};
	body.radar_icon = compile_radar_icon(value.value("radar_icon", Json("contact")), "projected body.radar_icon");
	body.radar_sprite = value.value("radar_sprite", std::string{});
	const std::string radar_prefix = "console-assets:assets/radar/";
	if (!body.radar_sprite.empty() &&
	    (!body.radar_sprite.starts_with("console-assets:assets/radar/") || !body.radar_sprite.ends_with(".png") ||
	     body.radar_sprite.size() != radar_prefix.size() + 64 + 4 ||
	     body.radar_sprite.substr(radar_prefix.size(), 64).find_first_not_of("0123456789abcdef") != std::string::npos))
		throw std::runtime_error("Invalid projected radar sprite");
	if (!body.id || body.heading < 0 || body.heading >= 360000 || body.definition.size() > 256)
		throw std::runtime_error("Invalid projected body");
	for (auto coordinate : {body.x, body.y, body.vx, body.vy})
		if (coordinate < -1000000000 || coordinate > 1000000000)
			throw std::runtime_error("Projected coordinate exceeds supported bounds");
	return body;
}
static inline Json observation_json(const Observation &observation)
{
	Json contacts = Json::array();
	for (const auto &contact : observation.contacts)
		contacts.push_back(body_json(contact));
	Json systems = Json::array();
	for (const auto &system : observation.systems)
		systems.push_back({{"id", system.id},
		                   {"kind", std::string(operating_system_name(system.kind))},
		                   {"requested", system.requested},
		                   {"maximum", system.maximum},
		                   {"enabled", system.enabled},
		                   {"controlled", system.controlled}});
	return {{"own", body_json(observation.own)},
	        {"contacts", contacts},
	        {"requested_speed", observation.requested_speed},
	        {"requested_heading", observation.requested_heading},
	        {"engine_enabled", observation.engine_enabled},
	        {"radar_enabled", observation.radar_enabled},
	        {"console", observation.console},
	        {"controls", observation.controls},
	        {"feeds", observation.feeds},
	        {"systems", systems}};
}
static inline Observation read_observation(const Json &value, const Catalog &catalog)
{
	Observation result{read_body(value.at("own")), {}};
	if (!catalog.entities.contains(result.own.definition) || !value.at("contacts").is_array() ||
	    value.at("contacts").size() > 4096)
		throw std::runtime_error("Projected observation does not match verified content");
	for (const auto &contact : value.at("contacts"))
	{
		auto body = read_body(contact);
		if (!catalog.presentation && !catalog.entities.contains(body.definition))
			throw std::runtime_error("Unknown projected contact definition");
		result.contacts.push_back(std::move(body));
	}
	result.requested_speed = value.at("requested_speed");
	result.requested_heading = value.at("requested_heading");
	result.engine_enabled = value.at("engine_enabled");
	result.radar_enabled = value.at("radar_enabled");
	result.console = value.value("console", std::string{"main"});
	const auto controls = value.value("controls", 7), feeds = value.value("feeds", 7);
	if (controls < 0 || controls > 7 || feeds < 0 || feeds > 7 || (controls & feeds) != controls ||
	    result.console.size() > 128)
		throw std::runtime_error("Invalid projected Console permissions");
	result.controls = static_cast<SystemMask>(controls);
	result.feeds = static_cast<SystemMask>(feeds);
	const auto specs = resolve_systems(catalog, result.own.definition);
	const auto seats = resolve_consoles(catalog, result.own.definition);
	const auto seat = std::find_if(seats.begin(), seats.end(),
	                               [&](const auto &entry)
	                               {
		                               return entry.id == result.console;
	                               });
	if (seat == seats.end() || seat->controls != result.controls || seat->feeds != result.feeds)
		throw std::runtime_error("Projected Console permissions disagree with verified content");
	const auto &projected = value.at("systems");
	if (!projected.is_array() || projected.size() != seat->feed_systems.size())
		throw std::runtime_error("Projected System roster mismatch");
	std::vector<std::string> seen;
	for (const auto &entry : projected)
	{
		const auto id = entry.at("id").get<std::string>();
		const auto spec = std::find_if(specs.begin(), specs.end(),
		                               [&](const auto &item)
		                               {
			                               return item.id == id;
		                               });
		if (spec == specs.end() ||
		    std::find(seat->feed_systems.begin(), seat->feed_systems.end(), id) == seat->feed_systems.end() ||
		    std::find(seen.begin(), seen.end(), id) != seen.end() ||
		    entry.at("kind") != std::string(operating_system_name(spec->kind)))
			throw std::runtime_error("Unpermitted projected System");
		seen.push_back(id);
		const bool controlled = std::find(seat->controlled_systems.begin(), seat->controlled_systems.end(), id) !=
		                        seat->controlled_systems.end();
		SystemView state{
		    id, spec->kind, entry.at("requested"), entry.at("maximum"), entry.at("enabled"), entry.at("controlled")};
		const auto maximum = spec->kind == OperatingSystem::Steering ? 359999 : spec->maximum;
		if (state.controlled != controlled || state.maximum != maximum || state.requested < 0 ||
		    (spec->kind != OperatingSystem::Radar && state.requested > maximum) ||
		    (spec->kind == OperatingSystem::Radar && state.requested != 0))
			throw std::runtime_error("Invalid projected System state");
		result.systems.push_back(std::move(state));
	}
	if (result.requested_speed < 0 || result.requested_speed > 1000000000 || result.requested_heading < 0 ||
	    result.requested_heading >= 360000)
		throw std::runtime_error("Invalid projected operating request");
	return result;
}
static inline std::uint64_t counter(const Json &value)
{
	if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 0))
		throw std::runtime_error("Invalid session counter");
	auto result = value.get<std::uint64_t>();
	if (result > 9007199254740991ULL)
		throw std::runtime_error("Session counter exceeds supported bounds");
	return result;
}
} // namespace phoenix::network
