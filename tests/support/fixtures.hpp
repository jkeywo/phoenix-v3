#pragma once
#include <string>
#include <functional>
#include <nlohmann/json.hpp>
import phoenix.content;
namespace phoenix::test
{
inline PackDraft fixture_pack(const Bytes &definitions, const std::string &id)
{
	PackDraft pack;
	pack.manifest = {{"schema", 1}, {"id", id}};
	// Materialise test builders' shorthand into the production effect catalogue.
	// Runtime and production loaders accept only references.
	auto authored = parse_json(definitions, "synthetic fixture");
	std::size_t shield_index = 0;
	std::function<void(Json &)> materialise = [&](Json &value)
	{
		if (value.is_object() && value.value("kind", std::string{}) == "shield" && value.contains("capacity"))
		{
			Json contribution = Json::object();
			for (const char *key :
			     {"capacity", "initial", "bearing_mdeg", "arc_mdeg", "sustain_energy_per_second", "energy_per_strength",
			      "recharge_per_second", "off_recharge_per_second", "warmup_ticks", "layer", "discharge_per_second",
			      "discharge_efficiency_permille", "pre_damage_permille", "post_damage", "modifiers"})
				if (value.contains(key))
				{
					contribution[key] = value.at(key);
					value.erase(key);
				}
			const auto effect = "fixture.shield." + std::to_string(++shield_index);
			value["shield_effect"] = effect;
			if (!authored.contains("effects"))
				authored["effects"] = Json::array();
			authored["effects"].push_back(
			    {{"id", effect}, {"kind", "shield"}, {"contributions", {{"main", contribution}}}});
		}
		if (value.is_object())
		{
			for (auto &[key, child] : value.items())
				if (key != "effects")
					materialise(child);
		}
		else if (value.is_array())
			for (auto &child : value)
				materialise(child);
	};
	materialise(authored);
	pack.files["definitions.json"] = json_bytes(authored);
	pack.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
	return pack;
}
inline Bytes archive(const Json &definitions, const std::string &id = "test-core")
{
	return write_pack(fixture_pack(json_bytes(definitions), id));
}
inline Catalog load(const Json &definitions, const std::string &id = "test-core")
{
	return resolve_packs({read_pack(archive(definitions, id))});
}
inline void navigation_policy(Json &entity, const std::string &tree)
{
	entity["motion_model"] = "vector";
	entity["control_layout"] = Json::parse(R"json({
	  "schema":1,"default_profile":"direct",
	  "controllers":[{"id":"navigation","console":"main","commands":[
	    {"id":"speed_mm_s","action":"speed_mm_s","consumers":["engine"]},
	    {"id":"heading_mdeg","action":"heading_mdeg","consumers":["steering"]},
	    {"id":"radar_enabled","action":"radar_enabled","initial":1,"consumers":["radar"]},
	    {"id":"engine_enabled","action":"engine_enabled","initial":1,"consumers":["engine"]}
	  ]}],
	  "profiles":[{"id":"direct","consoles":{"main":{"human":["navigation"]}}}]
	})json");
	entity["control_layout"]["controllers"][0]["tree"] = tree;
}
} // namespace phoenix::test
