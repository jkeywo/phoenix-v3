#pragma once
namespace
{
void require(bool valid, const std::string &message)
{
	if (!valid)
		throw std::runtime_error("scenario: " + message);
}
std::uint64_t unsigned_number(const Json &v, const char *description)
{
	require(v.is_number_integer() && !v.is_boolean() && v >= 0,
	        std::string(description) + " must be an unsigned integer");
	return v.get<std::uint64_t>();
}
std::int64_t number(const Json &v, const char *field, std::int64_t fallback = 0,
                    std::int64_t minimum = -scenario_coordinate_limit, std::int64_t maximum = scenario_coordinate_limit)
{
	if (!v.contains(field))
		return fallback;
	const auto &n = v.at(field);
	require(n.is_number_integer() && !n.is_boolean() && n >= minimum && n <= maximum,
	        std::string(field) + " must be a bounded integer");
	return n.get<std::int64_t>();
}
std::string text(const Json &v, const char *field, bool optional = false)
{
	if (optional && !v.contains(field))
		return {};
	require(v.contains(field) && v.at(field).is_string(), std::string(field) + " must be text");
	auto s = v.at(field).get<std::string>();
	require(!s.empty() && s.size() <= 256, std::string(field) + " must contain 1..256 characters");
	return s;
}
std::string identifier(const Json &v, const char *field)
{
	auto s = text(v, field);
	require(valid_identifier(s), std::string(field) + " is not an authored identifier: " + s);
	return s;
}
const Json &list(const Json &v, const char *field, std::size_t maximum = 4096)
{
	static const Json empty = Json::array();
	if (!v.contains(field))
		return empty;
	require(v.at(field).is_array() && v.at(field).size() <= maximum, std::string(field) + " exceeds its array limit");
	return v.at(field);
}
ScenarioPoint point(const Json &v)
{
	return {number(v, "x_mm"), number(v, "y_mm")};
}
bool valid_point(ScenarioPoint p)
{
	return p.x >= -scenario_coordinate_limit && p.x <= scenario_coordinate_limit && p.y >= -scenario_coordinate_limit &&
	       p.y <= scenario_coordinate_limit;
}
ScenarioParameter parameter(const Json &v)
{
	if (v.is_boolean())
		return v.get<bool>();
	if (v.is_number_integer())
	{
		require(v >= -1000000000000LL && v <= 1000000000000LL, "parameter integer out of range");
		return v.get<std::int64_t>();
	}
	require(v.is_string() && v.get_ref<const std::string &>().size() <= 4096,
	        "parameter must be integer, boolean or bounded text");
	return v.get<std::string>();
}
std::map<std::string, ScenarioParameter> parameters(const Json &v)
{
	require(v.is_object() && v.size() <= 128, "parameters/payload must be an object of at most 128 values");
	std::map<std::string, ScenarioParameter> out;
	for (auto i = v.begin(); i != v.end(); ++i)
	{
		require(valid_identifier(i.key()), "invalid parameter name " + i.key());
		out.emplace(i.key(), parameter(i.value()));
	}
	return out;
}
Json encode_parameters(const std::map<std::string, ScenarioParameter> &values)
{
	Json out = Json::object();
	for (const auto &[key, value] : values)
		std::visit(
		    [&](const auto &v)
		    {
			    out[key] = v;
		    },
		    value);
	return out;
}
void validate_parameters(const std::map<std::string, ScenarioParameter> &values)
{
	require(values.size() <= 128, "parameter/payload has more than 128 values");
	for (const auto &[name, value] : values)
	{
		require(valid_identifier(name), "invalid parameter/payload name " + name);
		if (const auto *n = std::get_if<std::int64_t>(&value))
			require(*n >= -1000000000000LL && *n <= 1000000000000LL, "parameter integer out of range");
		if (const auto *s = std::get_if<std::string>(&value))
			require(s->size() <= 4096, "parameter text exceeds limit");
	}
}
ScenarioActor actor(const Json &v)
{
	require(v.is_object() && v.size() == 1, "actor needs exactly one of entity, slot, handle");
	if (v.contains("entity"))
	{
		require(v.at("entity").is_number_integer() && v.at("entity") > 0, "actor entity must be positive identity");
		return {ScenarioActorKind::Entity, v.at("entity").get<EntityId>(), {}};
	}
	if (v.contains("slot"))
		return {ScenarioActorKind::ShipSlot, 0, identifier(v, "slot")};
	return {ScenarioActorKind::Handle, 0, identifier(v, "handle")};
}
Json encode_actor(const ScenarioActor &a)
{
	if (a.kind == ScenarioActorKind::Entity)
		return {{"entity", a.entity}};
	return {{a.kind == ScenarioActorKind::ShipSlot ? "slot" : "handle", a.name}};
}
ScenarioRecipients recipients(const Json &v)
{
	require(v.is_object() && v.contains("ship"), "recipients requires ship actor");
	ScenarioRecipients out;
	out.ship = actor(v.at("ship"));
	for (const auto &console : list(v, "consoles"))
	{
		require(console.is_string() && valid_identifier(console.get<std::string>()), "invalid recipient Console");
		auto id = console.get<std::string>();
		require(std::find(out.consoles.begin(), out.consoles.end(), id) == out.consoles.end(),
		        "duplicate recipient Console");
		out.consoles.push_back(std::move(id));
	}
	out.audience = out.consoles.empty() ? ScenarioAudience::Ship : ScenarioAudience::Consoles;
	return out;
}
Json encode_recipients(const ScenarioRecipients &r)
{
	return {{"ship", encode_actor(r.ship)}, {"consoles", r.consoles}};
}
std::string part(std::string_view value)
{
	return std::to_string(value.size()) + ":" + std::string(value);
}
std::string fingerprint_parameters(const std::map<std::string, ScenarioParameter> &values)
{
	std::string out;
	for (const auto &[key, value] : values)
	{
		out += part(key) + std::to_string(value.index()) + ":";
		std::visit(
		    [&](const auto &v)
		    {
			    using T = std::decay_t<decltype(v)>;
			    if constexpr (std::is_same_v<T, std::string>)
				    out += part(v);
			    else
				    out += part(std::to_string(v));
		    },
		    value);
	}
	return out;
}
std::string fingerprint_actor(const ScenarioActor &a)
{
	return std::to_string(static_cast<unsigned>(a.kind)) + ":" + std::to_string(a.entity) + ":" + part(a.name);
}
std::string fingerprint_recipients(const ScenarioRecipients &r)
{
	auto out = fingerprint_actor(r.ship) + ":" + std::to_string(static_cast<unsigned>(r.audience));
	for (const auto &c : r.consoles)
		out += part(c);
	return out;
}
// Portable exact 64x64 -> 128 comparison, also supported by MSVC and WASM.
// Geometry coordinates are bounded so signed dot/cross products fit int64.
struct Wide
{
	std::uint64_t high, low;
};
Wide product(std::uint64_t a, std::uint64_t b)
{
	const auto a0 = static_cast<std::uint32_t>(a);
	const auto a1 = a >> 32;
	const auto b0 = static_cast<std::uint32_t>(b);
	const auto b1 = b >> 32;
	const std::uint64_t p0 = static_cast<std::uint64_t>(a0) * b0;
	const std::uint64_t p1 = static_cast<std::uint64_t>(a0) * b1;
	const std::uint64_t p2 = a1 * b0, p3 = a1 * b1;
	const auto middle = (p0 >> 32) + static_cast<std::uint32_t>(p1) + static_cast<std::uint32_t>(p2);
	return {p3 + (p1 >> 32) + (p2 >> 32) + (middle >> 32), (middle << 32) | static_cast<std::uint32_t>(p0)};
}
bool less_equal(Wide a, Wide b)
{
	return a.high < b.high || (a.high == b.high && a.low <= b.low);
}
std::int64_t cross(ScenarioPoint a, ScenarioPoint b, ScenarioPoint c)
{
	return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
bool on_segment(ScenarioPoint a, ScenarioPoint b, ScenarioPoint c)
{
	return cross(a, b, c) == 0 && c.x >= std::min(a.x, b.x) && c.x <= std::max(a.x, b.x) && c.y >= std::min(a.y, b.y) &&
	       c.y <= std::max(a.y, b.y);
}
bool intersect(ScenarioPoint a, ScenarioPoint b, ScenarioPoint c, ScenarioPoint d)
{
	const auto x = cross(a, b, c), y = cross(a, b, d), z = cross(c, d, a), w = cross(c, d, b);
	if ((x == 0 && on_segment(a, b, c)) || (y == 0 && on_segment(a, b, d)) || (z == 0 && on_segment(c, d, a)) ||
	    (w == 0 && on_segment(c, d, b)))
		return true;
	return ((x < 0 && y > 0) || (x > 0 && y < 0)) && ((z < 0 && w > 0) || (z > 0 && w < 0));
}
bool contains_name(const std::vector<std::string> &names, std::string_view name)
{
	return std::find(names.begin(), names.end(), name) != names.end();
}
Json encode_token(const ScenarioActionToken &t)
{
	return Json::array({t.instance, t.activation, t.task, t.call_path, t.call_activations});
}
ScenarioActionToken token(const Json &v)
{
	require(v.is_array() && v.size() == 5, "invalid action token");
	const auto task = unsigned_number(v.at(2), "task identity");
	require(task <= std::numeric_limits<std::uint32_t>::max(), "task identity out of range");
	ScenarioActionToken out{unsigned_number(v.at(0), "instance identity"), unsigned_number(v.at(1), "activation"),
	                        static_cast<std::uint32_t>(task)};
	require(v.at(3).is_array() && v.at(4).is_array(), "action ancestry must be arrays");
	for (const auto &entry : v.at(3))
	{
		const auto value = unsigned_number(entry, "call task identity");
		require(value <= std::numeric_limits<std::uint32_t>::max(), "call task identity out of range");
		out.call_path.push_back(static_cast<std::uint32_t>(value));
	}
	for (const auto &entry : v.at(4))
		out.call_activations.push_back(unsigned_number(entry, "call activation"));
	require(out.call_path.size() == out.call_activations.size() && out.call_path.size() <= 64,
	        "invalid action call ancestry");
	for (auto activation : out.call_activations)
		require(activation > 0, "invalid ancestor activation");
	return out;
}
Json encode_placement(const ScenarioSpawnPlacement &p)
{
	return {{"definition", p.definition}, {"x_mm", p.position.x}, {"y_mm", p.position.y}, {"heading_mdeg", p.heading}};
}
} // namespace
