#pragma once
// Body fields shared by Console observations and public display allowlists.
namespace phoenix::projection
{
// Authored radar range is at most 1e9 mm. Up to 128 officers, each with
// sixteen +10000-permille traits, can contribute to the authoritative range.
inline constexpr std::int64_t sensor_range_limit = 1000000000LL * (1 + 128 * 16 * 10);
inline Json body_json(const BodyView &b)
{
	return {{"id", b.id},
	        {"definition", b.definition},
	        {"x", b.x},
	        {"y", b.y},
	        {"vx", b.vx},
	        {"vy", b.vy},
	        {"heading", b.heading},
	        {"altitude", b.altitude},
	        {"vz", b.vz},
	        {"radius", b.radius},
	        {"radar_icon", std::string(radar_icon_name(b.radar_icon))},
	        {"radar_sprite", b.radar_sprite},
	        {"faction", b.faction},
	        {"faction_label", b.faction_label},
	        {"disposition", b.disposition}};
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
inline Json bearing_json(const BearingContactView &b)
{
	return {{"id", b.id}, {"bearing_mdeg", b.bearing_mdeg}, {"observed_tick", b.observed_tick}};
}
inline BearingContactView read_bearing(const Json &v)
{
	return {static_cast<EntityId>(number(v.at("id"), 1, 9007199254740991LL)), number(v.at("bearing_mdeg"), 0, 359999),
	        static_cast<std::uint64_t>(number(v.at("observed_tick"), 0, 9007199254740991LL))};
}
inline std::vector<BearingContactView> read_bearings(const Json &rows)
{
	if (!rows.is_array() || rows.size() > 4096)
		throw std::runtime_error("Invalid bearing contact rows");
	std::vector<BearingContactView> out;
	std::vector<EntityId> ids;
	for (const auto &row : rows)
	{
		auto b = read_bearing(row);
		if (std::find(ids.begin(), ids.end(), b.id) != ids.end())
			throw std::runtime_error("Duplicate bearing contact");
		ids.push_back(b.id);
		out.push_back(b);
	}
	return out;
}
enum class BodyReadPolicy
{
	SharedDisplay,
	NetworkCompatible
};
inline BodyView read_body(const Json &v, BodyReadPolicy policy)
{
	const auto &id = v.at("id");
	if (!id.is_number_integer() || id < 1)
		throw std::runtime_error("Invalid projected body identity");
	const auto identity = id.get<EntityId>();
	// Shared display IDs also pass through browser presentation coordinates.
	// Older Console snapshots retain the full unsigned entity identity range.
	if (policy == BodyReadPolicy::SharedDisplay && identity > 9007199254740991ULL)
		throw std::runtime_error("Invalid shared display body identity");
	BodyView body{
	    identity,           text(v.at("definition")),          number(v.at("x")), number(v.at("y")), number(v.at("vx")),
	    number(v.at("vy")), number(v.at("heading"), 0, 359999)};
	const auto icon_value =
	    policy == BodyReadPolicy::NetworkCompatible ? v.value("radar_icon", Json("contact")) : v.at("radar_icon");
	body.altitude = number(v.value("altitude", Json(0)), -1000000, 1000000);
	body.vz = number(v.value("vz", Json(0)), -1000000, 1000000);
	body.radius = number(v.value("radius", Json(0)), 0, 1000000000);
	const auto icon = radar_icon_named(text(icon_value));
	if (!icon)
		throw std::runtime_error("Invalid projected radar icon");
	body.radar_icon = *icon;
	body.faction = text(v.value("faction", Json("")), 128);
	body.faction_label = text(v.value("faction_label", Json("")));
	body.disposition = text(v.value("disposition", Json("neutral")));
	if (body.disposition != "friendly" && body.disposition != "neutral" && body.disposition != "hostile" &&
	    !(body.disposition == "unknown" && body.faction.empty()))
		throw std::runtime_error("Invalid projected disposition");
	body.radar_sprite = policy == BodyReadPolicy::NetworkCompatible ? v.value("radar_sprite", std::string{})
	                                                                : text(v.at("radar_sprite"));
	const std::string prefix = "console-assets:assets/radar/";
	if (!body.radar_sprite.empty() &&
	    (!body.radar_sprite.starts_with(prefix) || !body.radar_sprite.ends_with(".png") ||
	     body.radar_sprite.size() != prefix.size() + 68 ||
	     body.radar_sprite.substr(prefix.size(), 64).find_first_not_of("0123456789abcdef") != std::string::npos))
		throw std::runtime_error("Invalid projected radar sprite");
	return body;
}
} // namespace phoenix::projection
