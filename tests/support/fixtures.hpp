#pragma once
#include <algorithm>
#include <string>
#include <functional>
#include <map>
#include <set>
#include <utility>
#include <vector>
#include <stdexcept>
#include <nlohmann/json.hpp>
import phoenix.content;
#include "directed_materialisation.hpp"
#include "rule_materialisation.hpp"
#include "process_materialisation.hpp"
namespace phoenix::test
{
// Legacy shorthand is confined to synthetic fixture construction. Production packs
// must declare every identity and native stock binding explicitly.
inline void materialise_fixture_resources(Json &authored)
{
	std::map<std::string, Json> resources;
	for (const auto &r : authored.value("resources", Json::array()))
		resources.emplace(r.at("id").get<std::string>(), r);
	auto resource = [&](const std::string &id, const std::string &tag, std::int64_t size = 1) -> Json &
	{
		if (!resources.contains(id))
			resources[id] = {{"id", id},
			                 {"label", id},
			                 {"tags", Json::array()},
			                 {"sizes", Json::object()},
			                 {"effects", Json::array()}};
		auto &r = resources.at(id);
		if (r["sizes"].contains(tag) && r["sizes"].at(tag) != size)
			throw std::runtime_error("fixture resource size conflict: " + id);
		bool tagged = false;
		for (const auto &t : r["tags"])
			tagged = tagged || t == tag;
		if (!tagged)
			r["tags"].push_back(tag);
		r["sizes"][tag] = size;
		return r;
	};
	std::function<void(Json &)> materials = [&](Json &v)
	{
		if (v.is_object())
		{
			if (v.contains("logistics") && v["logistics"].is_object())
				for (const auto &r : v["logistics"].value("materials", Json::array()))
					resource(r.at("id").get<std::string>(), "cargo", r.value("volume", std::int64_t(1)));
			for (auto &[key, child] : v.items())
				materials(child);
		}
		else if (v.is_array())
			for (auto &child : v)
				materials(child);
	};
	materials(authored);
	std::size_t ammo_index = 0, recipe_index = 0;
	std::function<void(Json &)> convert = [&](Json &v)
	{
		if (v.is_object())
		{
			if (v.value("kind", std::string{}) == "magazine" && !v.contains("stock"))
			{
				const auto id = v.value("ammunition_type", "fixture.ammunition." + std::to_string(++ammo_index));
				auto &r = resource(id, "ammo");
				resource(id, "cargo");
				Json profile = Json::object();
				for (const auto *key : {"fire_ticks", "load_ticks", "projectile"})
					if (v.contains(key))
					{
						profile[key] = v.at(key);
						v.erase(key);
					}
				if (r.contains("ammunition") && r.at("ammunition") != profile)
					throw std::runtime_error("fixture ammunition profile conflict: " + id);
				r["ammunition"] = profile;
				const auto capacity = v.value("capacity", std::int64_t(10));
				v["stock"] = {{"capacity_tag", "ammo"},
				              {"capacity", capacity},
				              {"filter", {{"resources", Json::array({id})}}},
				              {"single_resource", true},
				              {"selection", id},
				              {"initial", {{id, v.value("initial", capacity)}}}};
				const auto reload = v.value("reload", Json::object());
				if (reload.value("strategy", std::string("none")) != "none")
				{
					const auto recipe = "fixture.recipe." + std::to_string(++recipe_index);
					if (!authored.contains("recipes"))
						authored["recipes"] = Json::array();
					for (const auto &existing : authored["recipes"])
						if (existing.at("id") == recipe)
							throw std::runtime_error("fixture recipe identity conflict");
					authored["recipes"].push_back({{"id", recipe},
					                               {"label", recipe},
					                               {"tags", Json::array({"ammunition"})},
					                               {"inputs", Json::object()},
					                               {"outputs", {{id, reload.value("amount", capacity)}}},
					                               {"work", reload.value("ticks", 20)}});
					v["processor"] = {{"recipe", recipe},
					                  {"mode", "batch"},
					                  {"work_per_second", 20},
					                  {"energy_per_second", reload.value("energy_per_second", std::int64_t(0))}};
				}
				for (const auto *key : {"capacity", "initial", "reload", "ammunition_type"})
					v.erase(key);
			}
			if (v.contains("resources") && v["resources"].is_object())
			{
				auto &network = v["resources"];
				for (auto &store : network.contains("stores") ? network["stores"] : (network["stores"] = Json::array()))
					if (!store.contains("resource"))
					{
						resource("fixture.resource.energy", "energy");
						store["resource"] = "fixture.resource.energy";
					}
				for (auto &coolant : network.contains("coolant_sources") ? network["coolant_sources"]
				                                                         : (network["coolant_sources"] = Json::array()))
					if (!coolant.contains("resource"))
					{
						resource("fixture.resource.coolant", "coolant");
						coolant["resource"] = "fixture.resource.coolant";
						if (!coolant.contains("capacity"))
							coolant["capacity"] = std::max<std::int64_t>(1, coolant.value("initial", std::int64_t(0)));
					}
			}
			for (auto &[key, child] : v.items())
				if (key != "resources" && key != "recipes")
					convert(child);
		}
		else if (v.is_array())
			for (auto &child : v)
				convert(child);
	};
	convert(authored);
	std::function<void(Json &)> stores = [&](Json &v)
	{
		if (v.is_object())
		{
			if (v.contains("logistics") && v["logistics"].is_object())
			{
				auto &l = v["logistics"];
				l.erase("materials");
				for (auto &store : l["stores"])
					if (!store.contains("stock"))
					{
						const auto system = store.value("system", std::string{});
						if (!system.empty() && v.contains("systems") && v["systems"].contains(system) &&
						    v["systems"][system].value("kind", std::string{}) == "magazine")
							store["stock"] = v["systems"][system].at("stock");
						else
							store["stock"] = {{"capacity_tag", "cargo"},
							                  {"capacity", store.value("capacity", 1)},
							                  {"filter", {{"resources", store.value("accepts", Json::array())}}},
							                  {"initial", store.value("initial", Json::object())}};
						for (const auto *key : {"capacity", "initial", "accepts"})
							store.erase(key);
					}
			}
			for (auto &[key, child] : v.items())
				stores(child);
		}
		else if (v.is_array())
			for (auto &child : v)
				stores(child);
	};
	stores(authored);
	if (!resources.empty())
	{
		authored["resources"] = Json::array();
		for (const auto &[id, row] : resources)
			authored["resources"].push_back(row);
	}
}
inline PackDraft fixture_pack(const Bytes &definitions, const std::string &id)
{
	PackDraft pack;
	pack.manifest = {{"schema", 1}, {"id", id}};
	// Materialise test builders' shorthand into the production effect catalogue.
	// Runtime and production loaders accept only references.
	auto authored = parse_json(definitions, "synthetic fixture");
	materialise_fixture_resources(authored);
	materialise_fixture_directed(authored);
	std::size_t shield_index = 0, modifier_index = 0;
	std::function<void(Json &)> materialise = [&](Json &value)
	{
		if (value.is_object() && value.contains("personnel") && value.at("personnel").is_object())
		{
			auto &personnel = value["personnel"];
			for (auto &trait : personnel["traits"])
				if (trait.contains("bonuses"))
				{
					trait["effects"] = Json::array();
					for (const auto &[property, delta] : trait.at("bonuses").items())
					{
						const auto effect = "fixture.modifier." + std::to_string(++modifier_index);
						if (!authored.contains("effects"))
							authored["effects"] = Json::array();
						Json contributions = {{"main", {{"property", property}, {"delta_permille", delta}}}};
						if (property == "recharge")
							contributions["off"] = {
							    {"property", "off_recharge"}, {"role", "recharge"}, {"delta_permille", delta}};
						authored["effects"].push_back(
						    {{"id", effect}, {"kind", "modifier"}, {"contributions", contributions}});
						trait["effects"].push_back(effect);
					}
					trait.erase("bonuses");
				}
			for (auto &duty : personnel["duties"])
				if (duty.contains("effects"))
				{
					duty["bindings"] = duty.at("effects");
					duty.erase("effects");
				}
		}
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
	materialise_fixture_processes(authored);
	materialise_fixture_rules(authored);
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
