#pragma once
namespace phoenix::test
{
// Keep old test builders readable while passing only the current, explicit
// catalogue schema to production. This helper is never linked into the game.
inline void materialise_fixture_directed(Json &authored)
{
	Json additions = Json::array();
	std::size_t index = 0;
	const std::string designated = "fixture.targeting.designated";
	const std::string intercept = "fixture.targeting.intercept";
	const std::string off = "fixture.targeting.off";
	bool needed = false;
	std::function<void(Json &)> convert = [&](Json &value)
	{
		if (value.is_object())
		{
			const auto kind = value.value("kind", std::string{});
			if ((kind == "weapon" || kind == "radar") && !value.contains("directed"))
			{
				needed = true;
				const auto id = "fixture.directed." + std::to_string(++index);
				const bool defence = value.value("point_defence", false);
				value.erase("point_defence");
				const Json targeting = defence ? Json::array({off, intercept, designated}) : Json::array({designated});
				Json mode = {{"id", id}, {"kind", "directed_mode"}, {"label", id}, {"targeting", targeting}};
				if (kind == "weapon")
				{
					mode["operation"] = value.value("family", std::string("projectile")) == "beam" ? "beam" : "launch";
					value.erase("family");
					for (const char *key : {"fire_ticks", "ammo_use", "load_ticks", "range_mm", "damage_per_second",
					                        "auto_energy_per_second", "energy_per_second", "discrete_energy",
					                        "projectile", "penetration", "damage_per_m", "shield_bypass_permille"})
						if (value.contains(key))
						{
							mode[key] = value.at(key);
							value.erase(key);
						}
				}
				else
				{
					mode["operation"] = "scan";
					mode["capabilities"] = value.value("capabilities", Json{{"basic_identity", 0}, {"own_arcs", 0}});
					value["broad_capabilities"] = Json::object();
					for (const auto &[capability, duration] : mode["capabilities"].items())
						if (duration == 0)
							value["broad_capabilities"][capability] = 0;
					value.erase("capabilities");
					for (const char *key : {"range_mm"})
						if (value.contains(key))
							mode[key] = value.at(key);
				}
				value["directed"] = {{"modes", Json::array({id})},
				                     {"targeting", targeting},
				                     {"initial_mode", id},
				                     {"initial_targeting", defence ? intercept : designated}};
				additions.push_back(std::move(mode));
			}
			for (auto &[key, child] : value.items())
				if (key != "effects" && key != "directed")
					convert(child);
		}
		else if (value.is_array())
			for (auto &child : value)
				convert(child);
	};
	convert(authored);
	if (needed)
	{
		if (!authored.contains("effects"))
			authored["effects"] = Json::array();
		for (const auto &[id, policy] : std::vector<std::pair<std::string, std::string>>{
		         {designated, "designated"}, {intercept, "intercept"}, {off, "off"}})
			additions.push_back({{"id", id}, {"kind", "targeting_policy"}, {"label", policy}, {"policy", policy}});
		for (const auto &effect : additions)
			authored["effects"].push_back(effect);
	}
}
} // namespace phoenix::test
