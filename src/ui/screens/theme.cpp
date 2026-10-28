module;
#include <RmlUi/Core.h>
#include <RmlUi/Core/StyleSheetContainer.h>
#include <RmlUi/Core/StreamMemory.h>
#include <raylib.h>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>
module phoenix.ui.theme;
import phoenix.content.catalog;
import phoenix.content.image_codec;
import phoenix.content.ui;

namespace phoenix
{
UiThemeRenderer::~UiThemeRenderer()
{
	retain_menu_images({}, this);
}
void UiThemeRenderer::reset(Rml::ElementDocument &document)
{
	if (signature_.empty())
		return;
	if (original_)
		document.SetStyleSheetContainer(original_);
	document.SetClass("theme", false);
	signature_.clear();
	retain_menu_images({}, this);
}
void UiThemeRenderer::apply(Rml::ElementDocument &document, const Catalog &catalog, const std::string &id,
                            const Json &overrides)
{
	std::string signature = catalog.ui_theme + "/" + id + overrides.dump();
	for (const auto &[pack, hash] : catalog.ordered_packs)
		signature += pack + hash;
	if (catalog.themes.contains(catalog.ui_theme))
		signature += catalog.themes.at(catalog.ui_theme).dump();
	if (catalog.themes.contains(id))
		signature += catalog.themes.at(id).dump();
	if (signature == signature_)
		return;
	const auto theme = resolved_ui_theme(catalog, id, overrides);
	Rml::StyleSheetContainer empty;
	if (!original_)
		original_ = document.GetStyleSheetContainer()->CombineStyleSheetContainer(empty);
	const auto &t = theme.at("tokens");
	auto token = [&](const char *key)
	{
		return t.at(key).get<std::string>();
	};
	std::string css = ".theme {color:" + token("text") + ";font-family:" + token("font_family") +
	                  ";font-size:" + std::to_string(t.at("font_size").get<float>()) + "dp;}\n";
	css += ".theme button,.theme input,.theme select {color:" + token("text") + ";background-color:" + token("panel") +
	       ";border-color:" + token("border") + ";border-radius:" + std::to_string(t.at("radius").get<float>()) +
	       "dp;}\n";
	css +=
	    ".theme button,.theme input,.theme select {border-width:" + std::to_string(t.at("border_width").get<float>()) +
	    "dp;}\n";
	css +=
	    ".theme #gameplay button,.theme .menu-column button {padding:" + std::to_string(t.at("gap").get<float>() / 2) +
	    "dp;}\n";
	css += ".theme .throttle {background-color:transparent;border-width:0dp;}\n";
	css += ".theme button:hover,.theme button:focus,.theme input:focus,.theme .pad-selected {border-color:" +
	       token("focus") + ";}\n";
	css += ".theme .selected,.theme button:checked {border-color:" + token("accent") + ";}\n";
	css += ".theme button:disabled,.theme input:disabled {color:" + token("disabled") + ";}\n";
	css += ".theme .error {color:" + token("error") + ";}\n";
	css += ".theme #gameplay,.theme #sensor-panel,.theme #comms-layer,.theme "
	       "#settings-panel,.theme .command-card,.theme .menu-column {background-color:" +
	       token("panel") + ";color:" + token("text") + ";}\n";
	css += ".theme .station-panel {padding:" + std::to_string(t.at("gap").get<float>() * 2 / 3) + "dp;}\n";
	std::set<std::string> sources;
	for (const auto &[slot, reference] : theme.at("assets").items())
	{
		const auto ref = reference.get<std::string>();
		if (slot == "font" || slot.starts_with("fallback-"))
		{
			// The backend requires font bytes to outlive RmlUi; the registry owns them.
			register_theme_font(catalog.assets.at(ref), token("font_family"), slot != "font");
			continue;
		}
		const auto slots = ui_theme_artwork_slots();
		if (std::find(slots.begin(), slots.end(), slot) == slots.end())
			continue;
		const auto source = register_menu_image(catalog.assets.at(ref));
		sources.insert(source);
		std::string decoration = "image(" + source + " cover)";
		if (theme.contains("slices") && theme.at("slices").contains(slot))
		{
			const auto image = decode_asset_image(catalog.assets.at(ref));
			const auto &s = theme.at("slices").at(slot);
			MenuSkin skin;
			skin.width = image.width;
			skin.height = image.height;
			skin.top = s.at(0);
			skin.right = s.at(1);
			skin.bottom = s.at(2);
			skin.left = s.at(3);
			skin.scale = theme.value("asset_scale", Json::object()).value(slot, 1.f);
			const auto name = "theme-" + slot;
			css += skin.sprite_sheet(name, source);
			decoration = skin.decorator(name, "dp");
		}
		const std::map<std::string, std::string> selectors{
		    {"background", ".theme #native-menu"},
		    {"panel", ".theme .station-panel,.theme .engineering-board,.theme #gameplay-readouts,.theme "
		              "#comms-history,.theme #comms-list,.theme #sensor-panel,.theme .local-settings-card,.theme "
		              ".command-card,.theme .menu-column"},
		    {"button", ".theme button"},
		    {"button-hover", ".theme button:hover,.theme button:focus,.theme button.pad-selected"},
		    {"button-pressed", ".theme button:active"},
		    {"button-selected",
		     ".theme button.selected,.theme button.on,.theme button.off,.theme button.on-screen-active"},
		    {"button-disabled", ".theme button:disabled,.theme button.disabled"},
		    {"tab", ".theme #console-tabs button,.theme #layout-tabs button"},
		    {"tab-selected", ".theme #console-tabs button.selected,.theme #layout-tabs button.selected"},
		    {"field", ".theme input[type=text],.theme input[type=number],.theme select,.theme textarea"},
		    {"slider-track", ".theme input slidertrack"},
		    {"slider-thumb", ".theme input sliderbar"},
		    {"progress-track", ".theme progress"},
		    {"progress-fill", ".theme progress fill"},
		    {"scrollbar-track", ".theme scrollbarvertical slidertrack,.theme scrollbarhorizontal slidertrack"},
		    {"scrollbar-thumb", ".theme scrollbarvertical sliderbar,.theme scrollbarhorizontal sliderbar"}};
		css += selectors.at(slot) + " {decorator:" + decoration + ";}\n";
	}
	// Tint state artwork independently of live labels and keep custom instruments intact.
	css += ".theme button:hover,.theme button:focus {image-color:" + token("hover") + ";}\n";
	css += ".theme button:active {image-color:" + token("pressed") + ";}\n";
	css += ".theme button.selected,.theme button.on-screen-active,.theme #console-tabs button.selected,.theme "
	       "#layout-tabs button.selected {image-color:" +
	       token("accent") + ";}\n";
	css += ".theme button.on {image-color:" + token("positive") +
	       ";} .theme button.off {image-color:" + token("negative") + ";}\n";
	css +=
	    ".theme button:disabled,.theme button.disabled,.theme button.disabled:hover {image-color:" + token("disabled") +
	    ";color:" + token("disabled") + ";}\n";
	css += ".theme .camera-choices button,.theme button.settings-cog "
	       "{decorator:none;background-color:transparent;image-color:white;}\n";
	css += ".theme button.settings-cog {border-width:0dp;} .theme .camera-choices button {border-width:2dp;}\n";
	css += ".theme input sliderbar,.theme scrollbarvertical sliderbar,.theme scrollbarhorizontal sliderbar "
	       "{image-color:" +
	       token("accent") + ";}\n";
	css += ".theme progress fill {image-color:" + token("positive") + ";}\n";
	css += ".theme input.throttle slidertrack,.theme input.throttle sliderbar {decorator:none;}\n";
	Rml::StreamMemory stream(reinterpret_cast<const Rml::byte *>(css.data()), css.size());
	Rml::StyleSheetContainer generated;
	if (!generated.LoadStyleSheetContainer(&stream))
		throw std::runtime_error("Unable to load generated UI theme");
	document.SetStyleSheetContainer(original_->CombineStyleSheetContainer(generated));
	document.SetClass("theme", true);
	signature_ = signature;
	retain_menu_images(sources, this);
}
} // namespace phoenix

namespace phoenix
{
void draw_alert_glow(const Json &alert, Rectangle area)
{
	if (alert.empty() || !alert.value("glow", false))
		return;
	const auto colour = alert.value("colour", std::string{});
	if (colour.size() != 7 || colour[0] != '#' ||
	    colour.substr(1).find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
		return;
	const auto rgb = std::stoul(colour.substr(1), nullptr, 16);
	Color tint{static_cast<unsigned char>(rgb >> 16), static_cast<unsigned char>(rgb >> 8),
	           static_cast<unsigned char>(rgb), 0};
	const int width = static_cast<int>(std::min({28.f, area.width / 12, area.height / 12}));
	for (int i = 0; i < width; ++i)
	{
		tint.a = static_cast<unsigned char>(130.f * (1.f - static_cast<float>(i) / width));
		DrawRectangleLinesEx({area.x + i, area.y + i, area.width - 2 * i, area.height - 2 * i}, 1, tint);
	}
}
} // namespace phoenix
