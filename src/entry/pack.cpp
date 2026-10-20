#include <filesystem>
#include <algorithm>
#include <iostream>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

import phoenix.content;

int main(int argc, char **argv)
{
	try
	{
		if (argc < 3)
		{
			std::cerr
			    << "Usage: phoenix_pack source-directory output.zip|output.dat [ordered-dependency.zip|.dat ...]\n";
			return 2;
		}
		const auto root = std::filesystem::canonical(std::filesystem::u8path(argv[1]));
		phoenix::PackDraft pack;
		for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
		{
			if (entry.is_symlink())
				throw std::runtime_error("Pack sources must not contain symbolic links");
			if (!entry.is_regular_file())
				continue;
			auto path = std::filesystem::relative(entry.path(), root).generic_string();
			if (entry.file_size() > 16 * 1024 * 1024 || pack.files.size() >= 1024)
				throw std::runtime_error("Pack source exceeds entry limits");
			auto encoded = entry.path().u8string();
			pack.files[path] = phoenix::read_file(std::string(encoded.begin(), encoded.end()));
		}
		pack.manifest = phoenix::parse_json(pack.files.at("manifest.json"), "manifest.json");
		std::vector<phoenix::Pack> ordered;
		for (int i = 3; i < argc; ++i)
			ordered.push_back(phoenix::read_pack(phoenix::read_file(argv[i])));
		const auto catalog = phoenix::resolve_draft(ordered, pack);
		auto preview = phoenix::launcher_catalog(catalog);
		const auto authored = phoenix::parse_json(pack.files.at("definitions.json"), "definitions.json")
		                          .value("maps", phoenix::Json::array());
		std::erase_if(preview["scenarios"].get_ref<phoenix::Json::array_t &>(),
		              [&](const auto &scene)
		              {
			              return std::none_of(authored.begin(), authored.end(),
			                                  [&](const auto &map)
			                                  {
				                                  return map.at("id") == scene.at("id");
			                                  });
		              });
		preview["name"] = pack.manifest.value("name", pack.manifest.at("id").get<std::string>());
		preview["dependencies"] = pack.manifest.value("dependencies", phoenix::Json::array());
		pack.files["launcher.json"] = phoenix::json_bytes(preview);
		auto archive = phoenix::write_pack(pack, std::string(argv[2]).ends_with(".dat") ? phoenix::PackEncoding::Dat
		                                                                                : phoenix::PackEncoding::Zip);
		ordered.push_back(phoenix::read_pack(archive));
		(void)phoenix::resolve_packs(ordered);
		phoenix::write_file(argv[2], archive);
		preview["id"] = pack.manifest.at("id");
		preview["hash"] = phoenix::sha256(archive);
		phoenix::write_file(std::string(argv[2]) + ".menu.json", phoenix::json_bytes(preview));
		std::cout << "Verified pack: " << argv[2] << '\n';
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
