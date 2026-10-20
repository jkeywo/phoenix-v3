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
static inline Json bounded_gameplay(const Json &source)
{
	if (!source.is_object())
		return Json::object();
	constexpr std::size_t budget = 1024 * 1024;
	// Controls and fresh notifications take precedence over historical reports.
	// Entries remain whole, and retained history keeps its original order.
	const char *categories[] = {"controls", "notifications", "controllers", "objectives",
	                            "shared",   "records",       "messages"};
	Json result = source;
	Json omitted = Json::object();
	for (const auto *category : categories)
	{
		const auto entries = source.find(category);
		omitted[category] = entries != source.end() && entries->is_array() ? entries->size() : 0;
		if (entries != source.end())
			result[category] = Json::array();
	}
	result["omitted"] = omitted;
	// Reserving metadata at the original (largest) counts also covers every
	// count's digits as entries are retained. One separator byte is conservative.
	std::size_t bytes = result.dump().size();
	const auto keep = [&](const char *category, const Json &entry)
	{
		const auto size = entry.dump().size() + 1;
		if (bytes <= budget && size <= budget - bytes)
		{
			result[category].push_back(entry);
			bytes += size;
			omitted[category] = omitted[category].get<std::size_t>() - 1;
		}
	};
	// World emits Away/resume and complexity profiles before optional actuator
	// descriptors. Preserve that priority so truncation cannot strand control.
	if (const auto controls = source.find("controls"); controls != source.end() && controls->is_array())
		for (const auto &control : *controls)
			keep("controls", control);
	if (const auto notifications = source.find("notifications");
	    notifications != source.end() && notifications->is_array())
		for (auto entry = notifications->rbegin(); entry != notifications->rend(); ++entry)
			keep("notifications", *entry);
	std::size_t history = 0;
	for (const auto *category : {"controllers", "objectives", "shared", "records", "messages"})
		if (const auto entries = source.find(category); entries != source.end() && entries->is_array())
			history = std::max(history, entries->size());
	// Round-robin recent entries so an old record stream cannot starve the
	// newest message/report in another permitted stream.
	for (std::size_t depth = 0; depth < history; ++depth)
		for (const auto *category : {"controllers", "objectives", "shared", "records", "messages"})
			if (const auto entries = source.find(category);
			    entries != source.end() && entries->is_array() && depth < entries->size())
				keep(category, (*entries)[entries->size() - 1 - depth]);
	for (const auto *category : categories)
		if (category != categories[0] && result.contains(category) && result.at(category).is_array())
			std::reverse(result[category].begin(), result[category].end());
	result["omitted"] = std::move(omitted);
	return result;
}
static inline Json public_board_json(const Json &source)
{
	Json selected{{"shared", Json::array()}, {"messages", Json::array()}};
	if (!source.is_object())
		return selected;
	for (const auto *key : {"shared", "messages"})
	{
		const auto rows = source.value(key, Json::array());
		if (!rows.is_array())
			throw std::runtime_error("Invalid public board rows");
		for (auto row : rows)
		{
			if (!row.is_object())
				throw std::runtime_error("Invalid public board entry");
			if (std::string(key) == "messages")
			{
				if (!row.value("public", false))
					continue;
				row["actionable"] = false;
				row["offer_only"] = true;
			}
			selected[key].push_back(std::move(row));
		}
	}
	auto result = bounded_gameplay(selected);
	const auto omissions = source.value("omitted", Json::object());
	if (!omissions.is_object())
		throw std::runtime_error("Invalid public board omission counts");
	for (const auto *key : {"shared", "messages"})
		if (omissions.contains(key))
		{
			const auto &count = omissions.at(key);
			if (!count.is_number_integer() || count < 0 || count > 1000000000)
				throw std::runtime_error("Invalid public board omission count");
			result["omitted"][key] = result["omitted"][key].get<std::uint64_t>() + count.get<std::uint64_t>();
		}
	return result;
}
static inline Json observation_json(const Observation &observation)
{
	Json additional = Json::array();
	for (const auto &console : observation.additional_consoles)
		additional.push_back(observation_json(console));
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
		                   {"controlled", system.controlled},
		                   {"telemetry", system.telemetry},
		                   {"status", system.status},
		                   {"effective_range", system.effective_range},
		                   {"radial_ranges", system.radial_ranges},
		                   {"shows", system.shows},
		                   {"contact_ids", system.contact_ids}});
	return {{"own", body_json(observation.own)},
	        {"contacts", contacts},
	        {"observed_tick", observation.observed_tick},
	        {"requested_speed", observation.requested_speed},
	        {"requested_heading", observation.requested_heading},
	        {"engine_enabled", observation.engine_enabled},
	        {"radar_enabled", observation.radar_enabled},
	        {"console", observation.console},
	        {"controls", observation.controls},
	        {"feeds", observation.feeds},
	        {"systems", systems},
	        {"gameplay", bounded_gameplay(observation.gameplay)},
	        {"additional_consoles", additional},
	        {"regions", observation.regions}};
}
static inline Observation read_observation(const Json &value, const Catalog &catalog)
{
	Observation result{read_body(value.at("own")), {}};
	const auto additional = value.value("additional_consoles", Json::array());
	if (!additional.is_array() || additional.size() > 31)
		throw std::runtime_error("Invalid additional Console count");
	for (const auto &entry : additional)
	{
		if (!entry.value("additional_consoles", Json::array()).empty())
			throw std::runtime_error("Nested additional Consoles are unsupported");
		auto console = read_observation(entry, catalog);
		if (console.own.id != result.own.id)
			throw std::runtime_error("Additional Console belongs to another ship");
		result.additional_consoles.push_back(std::move(console));
	}
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
	if (value.contains("observed_tick") &&
	    (!value.at("observed_tick").is_number_integer() || value.at("observed_tick") < 0 ||
	     value.at("observed_tick") > ~std::uint64_t{0}))
		throw std::runtime_error("Invalid observation tick");
	result.observed_tick = value.value("observed_tick", std::uint64_t{0});
	result.requested_speed = value.at("requested_speed");
	result.requested_heading = value.at("requested_heading");
	result.engine_enabled = value.at("engine_enabled");
	result.radar_enabled = value.at("radar_enabled");
	result.console = value.value("console", std::string{"main"});
	const auto controls = value.value("controls", 7), feeds = value.value("feeds", 7);
	if (controls < 0 || controls > 1023 || feeds < 0 || feeds > 1023 || (controls & feeds) != controls ||
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
		if ((state.controlled && !controlled) || state.maximum != maximum || state.requested < -maximum ||
		    (spec->kind != OperatingSystem::Radar &&
		     (state.requested > 1000000000000LL || state.requested < -1000000000)) ||
		    (spec->kind == OperatingSystem::Radar && state.requested != 0))
			throw std::runtime_error("Invalid projected System state");
		state.telemetry = entry.value("telemetry", std::map<std::string, std::int64_t>{});
		state.status = entry.value("status", std::string{});
		state.effective_range =
		    entry.value("effective_range", state.kind == OperatingSystem::Radar && state.enabled ? state.maximum : 0LL);
		state.shows = entry.value("shows", std::vector<std::string>{"contact"});
		state.contact_ids = entry.value("contact_ids", std::vector<EntityId>{});
		if (!entry.contains("contact_ids"))
			for (const auto &body : result.contacts)
				state.contact_ids.push_back(body.id);
		if (state.shows.size() > 32 || state.contact_ids.size() > result.contacts.size())
			throw std::runtime_error("Invalid radar tag projection");
		for (const auto &tag : state.shows)
			if (tag.empty() || tag.size() > 128)
				throw std::runtime_error("Invalid radar tag");
		for (const auto id : state.contact_ids)
			if (std::none_of(result.contacts.begin(), result.contacts.end(),
			                 [&](const auto &b)
			                 {
				                 return b.id == id;
			                 }))
				throw std::runtime_error("Unknown radar contact");
		state.radial_ranges = entry.value("radial_ranges", std::vector<std::int64_t>{});
		if (state.effective_range < 0 || state.effective_range > state.maximum ||
		    (!state.radial_ranges.empty() &&
		     (state.kind != OperatingSystem::Radar || state.radial_ranges.size() != 128)) ||
		    std::any_of(state.radial_ranges.begin(), state.radial_ranges.end(),
		                [&](auto n)
		                {
			                return n < 0 || n > state.effective_range;
		                }))
			throw std::runtime_error("Invalid projected sensor envelope");
		if (state.telemetry.size() > 128 || state.status.size() > 256)
			throw std::runtime_error("Invalid projected telemetry bounds");
		result.systems.push_back(std::move(state));
	}
	if (result.requested_speed < -1000000000 || result.requested_speed > 1000000000 || result.requested_heading < 0 ||
	    result.requested_heading >= 360000)
		throw std::runtime_error("Invalid projected operating request");
	result.regions = value.value("regions", Json::array());
	if (!result.regions.is_array() || result.regions.size() > 256)
		throw std::runtime_error("Invalid region marker count");
	for (const auto &r : result.regions)
	{
		if (!r.is_object() || r.at("name").get<std::string>().size() > 128 || !r.at("navigation").is_boolean() ||
		    !r.at("radar").is_boolean() ||
		    (r.at("shape") != "circle" && r.at("shape") != "rectangle" && r.at("shape") != "annulus"))
			throw std::runtime_error("Invalid region marker");
		for (const auto *key : {"x", "y", "radius", "inner_radius", "half_width", "half_height"})
			if (!r.at(key).is_number_integer() || r.at(key) < -1000000000 || r.at(key) > 1000000000)
				throw std::runtime_error("Invalid region marker dimensions");
		if (!r.at("colour").is_array() || r.at("colour").size() != 3)
			throw std::runtime_error("Invalid region colour");
		for (const auto &c : r.at("colour"))
			if (!c.is_number() || c < 0 || c > 1)
				throw std::runtime_error("Invalid region colour");
		if (!r.at("tags").is_array() || r.at("tags").size() > 32)
			throw std::runtime_error("Invalid region tags");
		for (const auto &tag : r.at("tags"))
			if (!tag.is_string() || tag.get<std::string>().size() > 128)
				throw std::runtime_error("Invalid region tag");
	}
	result.gameplay = value.value("gameplay", Json::object());
	// StateSource/StateTransfer already authenticate and bound the complete
	// snapshot to 4 MiB. Permitted records and history may exceed one packet;
	// applying a smaller sub-envelope limit would reject valid chunked snapshots.
	if (!result.gameplay.is_object() || result.gameplay.dump().size() > 4 * 1024 * 1024)
		throw std::runtime_error("Invalid knowledge projection bounds");
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
