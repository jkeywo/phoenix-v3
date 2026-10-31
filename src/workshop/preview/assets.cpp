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
import phoenix.workshop.authoring_core;
import phoenix.content.image_codec;
import phoenix.graphics.assets;
import phoenix.application.preferences;

namespace phoenix
{
struct WorkshopAssetPreview::State
{
	TextureCache textures;
};
WorkshopAssetPreview::WorkshopAssetPreview() : state_(std::make_unique<State>())
{
}
WorkshopAssetPreview::~WorkshopAssetPreview() = default;
void WorkshopAssetPreview::clear()
{
	state_->textures.clear();
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
	ImGui::TextWrapped("%s", localise(label).c_str());
	ImGui::SetNextItemWidth(-1);
	if (ImGui::BeginCombo((std::string("##") + label).c_str(),
	                      current.empty() ? localise(skybox ? "None" : "Choose PNG").c_str() : current.c_str()))
	{
		if (skybox && ImGui::Selectable(localise("None").c_str(), current.empty()))
			value = "";
		for (const auto &reference : workshop_asset_choices(document, skybox))
			if (ImGui::Selectable(reference.get_ref<const std::string &>().c_str(), reference == current))
				value = reference;

		ImGui::EndCombo();
	}
}
} // namespace phoenix
