#pragma once
// Shared visual projection only: no Console controls, telemetry, inbox or private records.
namespace phoenix::display
{
inline Json body_json(const BodyView &b)
{
	return {{"id", b.id},
	        {"definition", b.definition},
	        {"x", b.x},
	        {"y", b.y},
	        {"vx", b.vx},
	        {"vy", b.vy},
	        {"heading", b.heading},
	        {"radar_icon", std::string(radar_icon_name(b.radar_icon))},
	        {"radar_sprite", b.radar_sprite}};
}
inline std::int64_t number(const Json &v, std::int64_t lo = -1000000000, std::int64_t hi = 1000000000)
{
	if (!v.is_number_integer() || v < lo || v > hi)
		throw std::runtime_error("Invalid shared display number");
	return v.get<std::int64_t>();
}
inline std::string text(const Json &v, std::size_t limit = 256)
{
	if (!v.is_string() || v.get_ref<const std::string &>().size() > limit)
		throw std::runtime_error("Invalid shared display text");
	return v.get<std::string>();
}
inline BodyView read_body(const Json &v)
{
	BodyView b{static_cast<EntityId>(number(v.at("id"), 1, 9007199254740991LL)),
	           text(v.at("definition")),
	           number(v.at("x")),
	           number(v.at("y")),
	           number(v.at("vx")),
	           number(v.at("vy")),
	           number(v.at("heading"), 0, 359999)};
	const auto icon = radar_icon_named(text(v.at("radar_icon")));
	if (!icon)
		throw std::runtime_error("Invalid shared display icon");
	b.radar_icon = *icon;
	b.radar_sprite = text(v.at("radar_sprite"));
	const std::string prefix = "console-assets:assets/radar/";
	if (!b.radar_sprite.empty() &&
	    (!b.radar_sprite.starts_with(prefix) || !b.radar_sprite.ends_with(".png") ||
	     b.radar_sprite.size() != prefix.size() + 68 ||
	     b.radar_sprite.substr(prefix.size(), 64).find_first_not_of("0123456789abcdef") != std::string::npos))
		throw std::runtime_error("Invalid shared display sprite");
	return b;
}
inline Json radar_json(const SystemView &s)
{
	return {{"id", s.id},
	        {"maximum", s.maximum},
	        {"enabled", s.enabled},
	        {"effective_range", s.effective_range},
	        {"radial_ranges", s.radial_ranges},
	        {"reach_profile", sensor_reach_json(s)},
	        {"shows", s.shows}};
}
inline SystemView read_radar(const Json &v)
{
	SystemView s;
	s.kind = OperatingSystem::Radar;
	s.id = text(v.at("id"));
	s.maximum = number(v.at("maximum"), 0);
	s.effective_range = number(v.at("effective_range"), 0, s.maximum);
	s.enabled = v.at("enabled").get<bool>();
	read_sensor_reach(s, v.value("reach_profile", Json::array()));
	if (!v.at("radial_ranges").is_array() || (!v.at("radial_ranges").empty() && v.at("radial_ranges").size() != 128) ||
	    !v.at("shows").is_array() || v.at("shows").size() > 256)
		throw std::runtime_error("Invalid shared radar envelope");
	for (const auto &r : v.at("radial_ranges"))
		s.radial_ranges.push_back(number(r, 0, s.maximum));
	for (const auto &tag : v.at("shows"))
		s.shows.push_back(text(tag, 128));
	return s;
}
// Decode and reconstruct the allowlist at both network boundaries. Never relay arbitrary nested fields.
inline Json sanitise(const Json &v)
{
	if (v.is_null())
		return nullptr;
	if (!v.is_object() || v.dump().size() > 4 * 1024 * 1024)
		throw std::runtime_error("Invalid shared display bounds");
	Json result{{"revision", number(v.at("revision"), 1, 9007199254740991LL)},
	            {"console", text(v.at("console"))},
	            {"widget", text(v.at("widget"))},
	            {"kind", text(v.at("kind"))},
	            {"own", body_json(read_body(v.at("own")))},
	            {"contacts", Json::array()},
	            {"regions", Json::array()}};
	const bool radar = result.at("kind") == "radar";
	if (!radar && result.at("kind") != "navigation")
		throw std::runtime_error("Invalid shared display kind");
	if (!v.at("camera").is_array() || v.at("camera").size() != 3)
		throw std::runtime_error("Invalid shared camera");
	result["camera"] = {number(v.at("camera")[0]), number(v.at("camera")[1]), number(v.at("camera")[2], 1)};
	if (!v.at("contacts").is_array() || v.at("contacts").size() > 4096 || !v.at("regions").is_array() ||
	    v.at("regions").size() > 256)
		throw std::runtime_error("Invalid shared display rows");
	if (radar)
	{
		result["radar"] = radar_json(read_radar(v.at("radar")));
		const auto settings = v.value("sweep", Json::object());
		const auto speed = settings.value("speed_degrees_s", 60.0), duration = settings.value("duration_seconds", 3.0);
		if (!(speed > 0 && speed <= 3600 && duration > 0 && duration <= 60))
			throw std::runtime_error("Invalid shared sweep settings");
		result["sweep"] = {{"enabled", settings.value("enabled", true)},
		                   {"speed_degrees_s", speed},
		                   {"duration_seconds", duration},
		                   {"colour", settings.value("colour", Json::array({.1, .9, .65, .85}))}};
		if (!result["sweep"]["colour"].is_array() || result["sweep"]["colour"].size() != 4)
			throw std::runtime_error("Invalid shared sweep colour");
		for (const auto &c : result["sweep"]["colour"])
			if (!c.is_number() || c < 0 || c > 1)
				throw std::runtime_error("Invalid shared sweep colour");

		for (const auto &b : v.at("contacts"))
			result["contacts"].push_back(body_json(read_body(b)));
	}
	for (const auto &r : v.at("regions"))
	{
		Json row{{"name", text(r.at("name"), 128)},
		         {"shape", text(r.at("shape"))},
		         {"radar", radar},
		         {"navigation", !radar},
		         {"tags", Json::array()}};
		if (row.at("shape") != "circle" && row.at("shape") != "rectangle" && row.at("shape") != "annulus")
			throw std::runtime_error("Invalid shared region shape");
		for (const auto *key : {"x", "y", "radius", "inner_radius", "half_width", "half_height"})
			row[key] = number(r.at(key));
		if (!r.at("colour").is_array() || r.at("colour").size() != 3 || !r.at("tags").is_array() ||
		    r.at("tags").size() > 32)
			throw std::runtime_error("Invalid shared region appearance");
		row["colour"] = Json::array();
		for (const auto &c : r.at("colour"))
		{
			if (!c.is_number() || c < 0 || c > 1)
				throw std::runtime_error("Invalid shared region colour");
			row["colour"].push_back(c);
		}
		for (const auto &tag : r.at("tags"))
		{
			const auto name = text(tag, 128);
			if (radar && row["tags"].empty())
				row["tags"].push_back(name);
		}
		result["regions"].push_back(std::move(row));
	}
	result["waypoint"] = Json::object();
	if (v.at("waypoint").value("active", false))
		result["waypoint"] = {
		    {"active", true}, {"x", number(v.at("waypoint").at("x"))}, {"y", number(v.at("waypoint").at("y"))}};
	result["objectives"] = Json::array();
	if (!v.at("objectives").is_array() || v.at("objectives").size() > 128)
		throw std::runtime_error("Invalid shared objectives");
	if (!radar)
		for (const auto &o : v.at("objectives"))
			result["objectives"].push_back({{"id", number(o.at("id"), 1, 9007199254740991LL)},
			                                {"title", text(o.at("title"))},
			                                {"destination_x", number(o.at("destination_x"))},
			                                {"destination_y", number(o.at("destination_y"))},
			                                {"radius", number(o.value("radius", Json(0)), 0)}});
	// Keep the public envelope bounded without turning a crowded valid sensor feed into a runtime failure.
	const auto omitted = number(v.value("omitted_contacts", Json(0)), 0, 4096);
	if (omitted + result.at("contacts").size() > 4096)
		throw std::runtime_error("Invalid shared contact omission count");
	auto contacts = std::move(result["contacts"]);
	result["contacts"] = Json::array();
	result["omitted_contacts"] = omitted;
	result["omitted_contacts"] = 4096; // Reserve the largest count before selecting whole entries.
	std::size_t bytes = result.dump().size() + 1024;
	result["omitted_contacts"] = omitted;
	if (bytes > 1024 * 1024)
		throw std::runtime_error("Shared display metadata exceeds bounds");
	for (auto &contact : contacts)
	{
		const auto size = contact.dump().size() + 1;
		if (bytes + size <= 1024 * 1024)
		{
			result["contacts"].push_back(std::move(contact));
			bytes += size;
		}
		else
			result["omitted_contacts"] = result["omitted_contacts"].get<std::int64_t>() + 1;
	}
	return result;
}
} // namespace phoenix::display
