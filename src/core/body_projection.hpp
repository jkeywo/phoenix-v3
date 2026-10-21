#pragma once
// Body fields shared by Console observations and public display allowlists.
namespace phoenix::projection
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
	const auto icon = radar_icon_named(text(icon_value));
	if (!icon)
		throw std::runtime_error("Invalid projected radar icon");
	body.radar_icon = *icon;
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
