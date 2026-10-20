#pragma once
#include <string>
#include <nlohmann/json.hpp>
import phoenix.content;
namespace phoenix::test
{
inline PackDraft fixture_pack(const Bytes &definitions, const std::string &id)
{
	PackDraft pack;
	pack.manifest = {{"schema", 1}, {"id", id}};
	pack.files["definitions.json"] = definitions;
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
