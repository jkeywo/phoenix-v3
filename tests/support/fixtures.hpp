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
} // namespace phoenix::test
