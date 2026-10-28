module;
#include <imgui.h>
#include <raylib.h>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

module phoenix.workshop.asset_preview;
import phoenix.content.documents;
import phoenix.content.image_codec;
import phoenix.graphics.assets;
import phoenix.application.preferences;

namespace phoenix
{
struct WorkshopAssetPreview::State
{
	TextureCache textures;
	std::map<std::string, bool> skybox_images;
};
WorkshopAssetPreview::WorkshopAssetPreview() : state_(std::make_unique<State>())
{
}
WorkshopAssetPreview::~WorkshopAssetPreview() = default;
void WorkshopAssetPreview::clear()
{
	state_->textures.clear();
	state_->skybox_images.clear();
}
Texture2D WorkshopAssetPreview::texture(const WorkshopDocument &document, const std::string &id)
{
	auto prefix = document.pack().manifest.at("id").get<std::string>() + ":";
	const Bytes *bytes = nullptr;
	if (id.starts_with(prefix))
	{
		auto found = document.pack().files.find(id.substr(prefix.size()));
		if (found != document.pack().files.end())
			bytes = &found->second;
	}
	else
	{
		auto found = document.libraries().assets.find(id);
		if (found != document.libraries().assets.end())
			bytes = &found->second;
	}
	if (!bytes)
		return {};
	return state_->textures.load(id, *bytes);
}
void WorkshopAssetPreview::pick(const WorkshopDocument &document, const char *label, Json &value, bool skybox)
{
	std::string current = value.is_string() ? value.get<std::string>() : "";
	const auto compatible = [&](const Bytes &bytes)
	{
		if (!skybox)
			return true;
		const auto hash = sha256(bytes);
		if (const auto found = state_->skybox_images.find(hash); found != state_->skybox_images.end())
			return found->second;
		bool valid = false;
		try
		{
			const auto image = decode_asset_image(bytes);
			valid = image.width == image.height * 2 || image.height == image.width * 6;
		}
		catch (const std::exception &)
		{
		}
		state_->skybox_images[hash] = valid;
		return valid;
	};
	ImGui::TextWrapped("%s", localise(label).c_str());
	ImGui::SetNextItemWidth(-1);
	if (ImGui::BeginCombo((std::string("##") + label).c_str(),
	                      current.empty() ? localise(skybox ? "None" : "Choose PNG").c_str() : current.c_str()))
	{
		if (skybox && ImGui::Selectable(localise("None").c_str(), current.empty()))
			value = "";
		for (const auto &[path, bytes] : document.pack().files)
			if (path.starts_with("assets/") && path.ends_with(".png") && compatible(bytes))
			{
				auto reference = document.pack().manifest.at("id").get<std::string>() + ":" + path;
				if (ImGui::Selectable(reference.c_str(), reference == current))
					value = reference;
			}
		for (const auto &[reference, bytes] : document.libraries().assets)
			if (reference.ends_with(".png") && !reference.starts_with("console-assets:assets/radar/") &&
			    compatible(bytes))
				if (ImGui::Selectable(reference.c_str(), reference == current))
					value = reference;
		ImGui::EndCombo();
	}
}
} // namespace phoenix
