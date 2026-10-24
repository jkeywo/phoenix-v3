#pragma once
namespace phoenix
{
// Generated styles only. No authored stylesheet or script crosses the content boundary.
class UiThemeRenderer
{
	Rml::SharedPtr<Rml::StyleSheetContainer> original_;
	std::string signature_;

public:
	inline void reset(Rml::ElementDocument &document)
	{
		if (signature_.empty())
			return;
		if (original_)
			document.SetStyleSheetContainer(original_);
		document.SetClass("theme", false);
		signature_.clear();
	}
	inline void apply(Rml::ElementDocument &document, const Catalog &catalog, const std::string &id = {},
	                  const Json &overrides = Json::object())
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
		css += ".theme button,.theme input,.theme select {color:" + token("text") +
		       ";background-color:" + token("panel") + ";border-color:" + token("border") +
		       ";border-radius:" + std::to_string(t.at("radius").get<float>()) + "dp;}\n";
		css += ".theme button,.theme input,.theme select {border-width:" +
		       std::to_string(t.at("border_width").get<float>()) + "dp;}\n";
		css += ".theme #gameplay button,.theme .menu-column button {padding:" +
		       std::to_string(t.at("gap").get<float>() / 2) + "dp;}\n";
		css += ".theme .throttle {background-color:transparent;border-width:0dp;}\n";
		css += ".theme button:hover,.theme button:focus,.theme input:focus,.theme .pad-selected {border-color:" +
		       token("focus") + ";}\n";
		css += ".theme .selected,.theme button:checked {border-color:" + token("accent") + ";}\n";
		css += ".theme button:disabled,.theme input:disabled {color:" + token("disabled") + ";}\n";
		css += ".theme .error {color:" + token("error") + ";}\n";
		css += ".theme #gameplay,.theme #sensor-panel,.theme #comms-layer,.theme "
		       "#settings-panel,.theme .command-card,.theme .menu-column {background-color:" +
		       token("panel") + ";color:" + token("text") + ";}\n";
		for (const auto &[slot, reference] : theme.at("assets").items())
		{
			const auto ref = reference.get<std::string>();
			if (slot == "font" || slot.starts_with("fallback-"))
			{
				// The backend requires font bytes to outlive RmlUi; the registry owns them.
				register_theme_font(catalog.assets.at(ref), token("font_family"), slot != "font");
				continue;
			}
			if (slot != "background" && slot != "panel" && slot != "button")
				continue;
			const auto source = register_theme_image(catalog.assets.at(ref));
			std::string decoration = "image(" + source + " cover)";
			if (theme.contains("slices") && theme.at("slices").contains(slot))
			{
				const auto image = decode_asset_image(catalog.assets.at(ref));
				const auto &s = theme.at("slices").at(slot);
				const int top = s.at(0), right = s.at(1), bottom = s.at(2), left = s.at(3);
				const auto name = "theme-" + slot;
				css += "@spritesheet " + name + " {src:" + source + ";resolution:1x;" + name + "-outer:0px 0px " +
				       std::to_string(image.width) + "px " + std::to_string(image.height) + "px;" + name +
				       "-inner:" + std::to_string(left) + "px " + std::to_string(top) + "px " +
				       std::to_string(image.width - left - right) + "px " +
				       std::to_string(image.height - top - bottom) + "px;}\n";
				decoration = "ninepatch(" + name + "-outer," + name + "-inner," + std::to_string(top) + "dp " +
				             std::to_string(right) + "dp " + std::to_string(bottom) + "dp " + std::to_string(left) +
				             "dp) border-box";
			}
			const std::string selector =
			    slot == "button" ? ".theme button"
			    : slot == "background"
			        ? ".theme #native-menu"
			        : ".theme #gameplay,.theme .status-widget,.theme .camera_mode-widget,.theme "
			          ".camera_direction-widget,.theme .menu-column,.theme .command-card,.theme #settings-panel";
			css += selector + " {decorator:" + decoration + ";}\n";
		}
		Rml::StreamMemory stream(reinterpret_cast<const Rml::byte *>(css.data()), css.size());
		Rml::StyleSheetContainer generated;
		if (!generated.LoadStyleSheetContainer(&stream))
			throw std::runtime_error("Unable to load generated UI theme");
		document.SetStyleSheetContainer(original_->CombineStyleSheetContainer(generated));
		document.SetClass("theme", true);
		signature_ = signature;
	}
};
} // namespace phoenix
