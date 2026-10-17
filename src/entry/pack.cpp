#include <phoenix/content.hpp>
#include <filesystem>
#include <iostream>
int main(int argc, char **argv)
{
	try
	{
		if (argc < 3)
		{
			std::cerr << "Usage: phoenix_pack source-directory output.zip [ordered-dependency.zip ...]\n";
			return 2;
		}
		const auto root = std::filesystem::canonical(std::filesystem::u8path(argv[1]));
		phoenix::Pack pack;
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
		auto archive = phoenix::write_pack(pack);
		std::vector<phoenix::Pack> ordered;
		for (int i = 3; i < argc; ++i)
			ordered.push_back(phoenix::read_pack(phoenix::read_file(argv[i])));
		ordered.push_back(phoenix::read_pack(archive));
		phoenix::resolve_packs(ordered);
		phoenix::write_file(argv[2], archive);
		std::cout << "Verified pack: " << argv[2] << '\n';
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
