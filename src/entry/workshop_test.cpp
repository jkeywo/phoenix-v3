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

import phoenix.workshop_test;
import phoenix.content;
import phoenix.menu;

namespace
{
phoenix::GameplayTestHost host;
std::vector<phoenix::Bytes> blobs;
std::size_t blob_bytes = 0;
std::string response;
const char *failure(const std::string &message)
{
	response = phoenix::Json{
	    {"status", message},
	    {"host_diagnostics", phoenix::Json::array({message})},
	    {"request_succeeded",
	     false}}.dump();
	return response.c_str();
}
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
extern "C" EMSCRIPTEN_KEEPALIVE int phoenix_workshop_test_blob(const std::uint8_t *data, unsigned size)
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
extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_workshop_test_reset_blobs()
{
	blobs.clear();
	blob_bytes = 0;
}
extern "C" EMSCRIPTEN_KEEPALIVE int phoenix_workshop_test_begin_blob(unsigned size)
{
	if (!size || size > phoenix::content_archive_limit || blobs.size() >= 65 || blob_bytes + size > 128 * 1024 * 1024)
		return 0;
	try
	{
		blobs.emplace_back(size);
		blob_bytes += size;
		return static_cast<int>(blobs.size());
	}
	catch (...)
	{
		return 0;
	}
}
extern "C" EMSCRIPTEN_KEEPALIVE int phoenix_workshop_test_write_blob(unsigned identity, unsigned offset,
                                                                     const std::uint8_t *data, unsigned size)
{
	if (!identity || identity > blobs.size() || !data || size > 65536)
		return 0;
	auto &blob = blobs[identity - 1];
	if (offset > blob.size() || size > blob.size() - offset)
		return 0;
	std::copy_n(data, size, blob.begin() + offset);
	return 1;
}
extern "C" EMSCRIPTEN_KEEPALIVE const char *phoenix_workshop_test_request(const char *text)
{
	try
	{
		if (!text)
			throw std::runtime_error("Missing disposable test request.");
		const auto request = phoenix::Json::parse(text);
		phoenix::WorkshopTestRequest command;
		const auto action = request.at("action").get<std::string>();
		if (action == "start")
			command.action = phoenix::WorkshopTestAction::Start;
		else if (action == "pause")
			command.action = phoenix::WorkshopTestAction::Pause;
		else if (action == "step")
			command.action = phoenix::WorkshopTestAction::Step;
		else if (action == "restart")
			command.action = phoenix::WorkshopTestAction::Restart;
		else if (action == "stop")
			command.action = phoenix::WorkshopTestAction::Stop;
		else
			throw std::runtime_error("Unknown disposable test action.");
		if (command.action == phoenix::WorkshopTestAction::Start)
		{
			const auto index = request.at("pack").get<std::size_t>();
			if (!index || index > blobs.size())
				throw std::runtime_error("Missing disposable test archive.");
			command.pack = std::move(blobs[index - 1]);
			if (!request.at("dependencies").is_array() || request.at("dependencies").size() > 64)
				throw std::runtime_error("Invalid dependency archive list.");
			for (const auto &entry : request.at("dependencies"))
			{
				const auto dependency = entry.get<std::size_t>();
				if (!dependency || dependency > blobs.size() || blobs[dependency - 1].empty())
					throw std::runtime_error("Invalid dependency archive reference.");
				command.dependencies.push_back(std::move(blobs[dependency - 1]));
			}
			command.mission = request.at("mission").get<std::string>();
			command.slot = request.at("slot").get<std::string>();
			command.ship = request.at("ship").get<std::string>();
		}
		blobs.clear();
		blob_bytes = 0;
		const auto succeeded = host.submit(std::move(command));
		auto feedback = host.feedback();
		feedback["request_succeeded"] = succeeded;
		response = feedback.dump();
		return response.c_str();
	}
	catch (const std::exception &error)
	{
		blobs.clear();
		blob_bytes = 0;
		return failure(error.what());
	}
}
extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_workshop_test_advance(unsigned elapsed_ms)
{
	host.advance(elapsed_ms);
}
extern "C" EMSCRIPTEN_KEEPALIVE const char *phoenix_workshop_test_feedback()
{
	try
	{
		response = host.feedback().dump();
		return response.c_str();
	}
	catch (const std::exception &error)
	{
		return failure(error.what());
	}
}
int main()
{
	return 0;
}
