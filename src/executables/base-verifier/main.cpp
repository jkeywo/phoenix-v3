#include <emscripten.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

import phoenix.content.catalog;
import phoenix.content.distribution;
import phoenix.content.packs;
import phoenix.content.verification;
import phoenix.menu;

namespace
{
std::vector<phoenix::Bytes> blobs;
std::size_t blob_bytes = 0;
std::string response;
} // namespace
extern "C" EMSCRIPTEN_KEEPALIVE const char *phoenix_verify_launch_base(const std::uint8_t *bytes, unsigned size)
{
	try
	{
		if (!bytes || !size || size > phoenix::content_archive_limit)
			throw std::runtime_error("Invalid launch base archive size");
		const auto pack = phoenix::read_pack(phoenix::Bytes(bytes, bytes + size));
		const auto base = phoenix::base_configuration(pack);
		const auto catalog = phoenix::resolve_packs({pack});
		auto preview = phoenix::launcher_catalog(catalog);
		preview["id"] = base.id;
		preview["hash"] = base.hash;
		preview["name"] = pack.manifest().value("name", base.id);
		preview["base"] = {{"render_mode", base.render_mode}, {"interface_style", base.interface_style}};
		preview["dependencies"] = phoenix::Json::array();
		response = phoenix::Json{{"ok", true}, {"preview", preview}}.dump();
	}
	catch (const std::exception &error)
	{
		response = phoenix::Json{{"ok", false}, {"error", error.what()}}.dump();
	}
	return response.c_str();
}
extern "C" EMSCRIPTEN_KEEPALIVE int phoenix_menu_add_blob(const std::uint8_t *data, unsigned size)
{
	if (!data || !size || size > phoenix::content_archive_limit || blobs.size() >= 65 ||
	    blob_bytes + size > 128 * 1024 * 1024)
		return 0;
	try
	{
		blobs.emplace_back(data, data + size);
		blob_bytes += size;
		return static_cast<int>(blobs.size());
	}
	catch (...)
	{
		return 0;
	}
}
extern "C" EMSCRIPTEN_KEEPALIVE const char *phoenix_migrate_bundled_menu()
{
	try
	{
		if (blobs.size() < 3 || blobs.size() > 4)
			throw std::runtime_error("Invalid bundled migration input");
		phoenix::MenuLibrary library("/menu-migration");
		library.install_default(blobs[2]);
		if (blobs.size() == 4)
			library.install(blobs[3]);
		library.install_bundled(blobs[0], blobs[1]);
		std::vector<phoenix::Bytes> result;
		std::vector<phoenix::Pack> packs;
		phoenix::Json metadata = phoenix::Json::array();
		for (const auto &entry : library.packs)
		{
			result.push_back(library.read_pack_archive(entry.hash));
			packs.push_back(phoenix::read_pack(result.back()));
			metadata.push_back(phoenix::pack_menu(packs));
		}
		blobs = std::move(result);
		response = phoenix::Json{{"ok", true}, {"preview", metadata}}.dump();
	}
	catch (const std::exception &error)
	{
		response = phoenix::Json{{"ok", false}, {"error", error.what()}}.dump();
	}
	return response.c_str();
}
extern "C" EMSCRIPTEN_KEEPALIVE unsigned phoenix_menu_blob_size(unsigned index)
{
	return index < blobs.size() ? static_cast<unsigned>(blobs[index].size()) : 0;
}
extern "C" EMSCRIPTEN_KEEPALIVE const std::uint8_t *phoenix_menu_blob_data(unsigned index)
{
	return index < blobs.size() ? blobs[index].data() : nullptr;
}
extern "C" EMSCRIPTEN_KEEPALIVE const char *phoenix_verify_menu_packs()
{
	try
	{
		std::vector<phoenix::Pack> packs;
		for (const auto &bytes : blobs)
			packs.push_back(phoenix::read_pack(bytes));
		if (packs.empty())
			throw std::runtime_error("No selected packs");
		(void)phoenix::base_configuration(packs.front());
		response = phoenix::Json{{"ok", true}, {"preview", phoenix::pack_menu(packs)}}.dump();
	}
	catch (const std::exception &error)
	{
		response = phoenix::Json{{"ok", false}, {"error", error.what()}}.dump();
	}
	return response.c_str();
}
int main()
{
	return 0;
}
