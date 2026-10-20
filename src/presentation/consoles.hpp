#pragma once
#include "navigation_camera.hpp"
#include "region_markers.hpp"
#include "map_plots.hpp"
namespace phoenix
{
class ConsoleSurface final : public Rml::EventListener
{
	ConsoleId route_;
	std::string context_name_;
	bool toolbar_enabled_ = true;
	std::uint64_t display_update_tick_ = ~std::uint64_t{0};
	std::string display_announcement_;
	std::vector<std::int64_t> display_camera(const std::string &widget) const
	{
		if (const auto *layout = model_.layout())
			for (const auto &w : layout->at("widgets"))
				if (w.at("id") == widget && w.at("kind") == "navigation")
					return {static_cast<std::int64_t>(std::clamp(navigation_camera_.x, -1e9, 1e9)),
					        static_cast<std::int64_t>(std::clamp(navigation_camera_.y, -1e9, 1e9)),
					        static_cast<std::int64_t>(
					            std::clamp(navigation_camera_.scale * navigation_area_.width / 100, 1.0, 1e9))};
		return {0, 0, 1};
	}
	bool profile_open_ = false;
	std::string profile_name_;
	Bytes profile_avatar_;
	Json terminal_;
	std::map<std::string, Bytes> crew_portraits_;
	Rml::Context *context_ = nullptr;
	Rml::ElementDocument *document_ = nullptr;
	ConsoleModel model_;
	std::string manual_profile_, manual_console_;
	bool manual_toggle_ = false;
	std::optional<std::string> manual_preset(bool automatic) const
	{
		if (!model_.observation())
			return {};
		std::optional<std::string> fallback;
		for (const auto &d : model_.observation()->gameplay.value("controls", Json::array()))
			if (d.value("action", "") == "preset" && d.value("enabled", true))
			{
				const auto name = d.value("id", "");
				if (automatic && name == "profile.automatic")
					return name;
				if (!automatic && name.starts_with("profile.") && name != "profile.automatic")
				{
					if (name == manual_profile_)
						return name;
					if (!fallback)
						fallback = name;
				}
			}
		return fallback;
	}

#include "navigation_surface.hpp"
	TextureCache radar_textures_;
	std::map<std::string, std::unique_ptr<RadarSweep>> radar_sweeps_;
	Application *app_ = nullptr;
	std::string signature_;
	std::map<std::string, std::string> displayed_values_;
	std::vector<std::pair<std::string, std::string>> readout_values_;
	std::set<std::string> visible_readouts_;
	void measure_readouts()
	{
		visible_readouts_.clear();
		auto *panel = document_->GetElementById("gameplay");
		const auto panel_at = panel->GetAbsoluteOffset(Rml::BoxArea::Border);
		const auto bottom = panel_at.y + panel->GetOffsetHeight() + 100;
		for (const auto &[name, text] : readout_values_)
			if (auto *target = element(id(name)))
			{
				const auto at = target->GetAbsoluteOffset(Rml::BoxArea::Border);
				if (at.y + target->GetOffsetHeight() >= panel_at.y - 100 && at.y <= bottom)
					visible_readouts_.insert(name);
			}
	}
	bool update_readout(const std::string &name, const std::string &text, bool force)
	{
		auto *target = element(id(name));
		if (!target || displayed_values_[name] == text)
			return false;
		// Keep measured offscreen heights; accessible values are published separately.
		if (!force && !displayed_values_[name].empty() && !visible_readouts_.contains(name))
			return false;
		target->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
		displayed_values_[name] = text;
		return true;
	}

	std::map<std::string, Rml::Element *> elements_;
	std::map<std::pair<Rml::Element *, std::string>, std::string> properties_;
	void property(Rml::Element *target, const std::string &name, const std::string &value)
	{
		auto &previous = properties_[{target, name}];
		if (previous != value)
		{
			target->SetProperty(name, value);
			previous = value;
		}
	}
	Rml::Element *element(const std::string &name)
	{
		const auto found = elements_.find(name);
		if (found != elements_.end())
			return found->second;
		return elements_.emplace(name, document_->GetElementById(name)).first->second;
	}
	std::weak_ptr<SessionPort> owner_;
	std::uint64_t generation_ = 0;
	std::vector<std::pair<std::string, std::string>> controls_;
	std::string gameplay_signature_;
	std::string placement_signature_;
	std::string gameplay_readout_signature_;
	std::string engineering_signature_;
	std::set<std::string> held_clicks_;
	std::map<std::string, Texture2D> hud_;
	std::map<std::string, Json> hud_metadata_;
	std::optional<std::array<float, 3>> heading_dial_;
	std::map<std::uint64_t, std::string> heading_contacts_;
	std::map<std::uint64_t, std::string> throttle_contacts_;
	std::map<std::string, Rectangle> throttle_tracks_;
	ManualFlightInput manual_input_;
	std::map<std::string, FlightAxes> pad_axes_;
	FlightAxes manual_sent_;
	std::optional<std::uint64_t> joystick_contact_;
	Rectangle joystick_area_{};
	std::optional<std::uint64_t> manual_pulse_tick_;
	bool manual_was_active_ = false;
	std::string manual_identity_, manual_markup_;
	std::uint64_t manual_sent_tick_ = ~std::uint64_t{0};
	bool syncing_values_ = false;
	bool settings_open_ = false;
	std::string local_settings_tab_ = "display", local_settings_markup_;
	int text_scale_ = 100;
	std::string local_language_ = "en";
	std::vector<std::pair<std::string, std::string>> local_settings_controls_;
	std::string settings_button(const std::string &name, const std::string &label)
	{
		local_settings_controls_.emplace_back(id(name), label);
		return "<button id=\"" + id(name) + "\">" + Rml::StringUtilities::EncodeRml(localise(label)) + "</button>";
	}
	void prepare_local_settings(Application &app, const ConsoleRegion &region)
	{
		local_settings_controls_.clear();
		std::string markup = "<div class=\"local-settings-card\"><h1>" + localise("Console settings") + "</h1><div>";
		for (const auto &[tab, label] : std::vector<std::pair<std::string, std::string>>{
		         {"display", "Display"}, {"language", "Language"}, {"audio", "Audio"}, {"session", "Session"}})
			markup += settings_button("local-settings.tab/" + tab, label);
		markup += "</div>";
		if (local_settings_tab_ == "display")
		{
			markup += "<p>" + localise("Text size") + ": " + std::to_string(text_scale_) + "%</p>";
			markup += settings_button("local-settings.smaller", "Smaller text");
			markup += settings_button("local-settings.larger", "Larger text");
		}
		else if (local_settings_tab_ == "language")
			markup += settings_button("local-settings.language", "Language: " + local_language_);
		else if (local_settings_tab_ == "audio")
		{
			markup += "<p>" + localise("Audio uses the shared native output device.") + "</p>";
			for (const auto &[channel, label, value] :
			     std::vector<std::tuple<std::string, std::string, int>>{{"master", "Master volume", app.master_volume},
			                                                            {"music", "Music volume", app.music_volume},
			                                                            {"sfx", "SFX volume", app.sfx_volume}})
			{
				markup += "<p>" + localise(label) + ": " + std::to_string(value) + "%</p>";
				markup += settings_button("local-settings.audio/" + channel + "/down", label + " -");
				markup += settings_button("local-settings.audio/" + channel + "/up", label + " +");
			}
		}
		else if (model_.observation())
		{
			if (model_.observation()->gameplay.contains("profile"))
				markup +=
				    "<p>" + localise("Complexity") + ": " +
				    Rml::StringUtilities::EncodeRml(model_.observation()->gameplay.at("profile").get<std::string>()) +
				    "</p>";
			for (const auto &control : model_.observation()->gameplay.value("controls", Json::array()))
				if (control.value("action", "") == "away")
					markup +=
					    settings_button(descriptor_action(control.at("id")),
					                    control.value("value", 0) ? "Away - hand control to AI" : "Resume control");
				else if (control.value("action", "") == "preset")
					markup += settings_button(descriptor_action(control.at("id")),
					                          "Complexity: " + control.value("label", std::string{}));
		}
		markup += settings_button("local-settings.close", "Return to station") + "</div>";
		if (markup != local_settings_markup_)
		{
			const auto focus = context_->GetFocusElement() ? context_->GetFocusElement()->GetId() : std::string{};
			elements_.clear();
			document_->GetElementById("local-settings")->SetInnerRML(markup);
			local_settings_markup_ = markup;
			if (auto *previous = document_->GetElementById(focus))
				previous->Focus();
		}
		context_->Update();
		if (std::none_of(local_settings_controls_.begin(), local_settings_controls_.end(),
		                 [&](const auto &control)
		                 {
			                 return context_->GetFocusElement() &&
			                        context_->GetFocusElement()->GetId() == control.first;
		                 }))
			if (auto *first = document_->GetElementById(local_settings_controls_.front().first))
				first->Focus();
		for (const auto &[name, label] : local_settings_controls_)
		{
			app.screen_controls.emplace_back(name, localise(label));
			place(app, region, name);
		}
	}
	std::map<std::string, double> throttle_sent_at_;
	std::map<std::string, double> heading_sent_at_;
	std::map<std::pair<std::string, std::string>, std::int64_t> engineering_probe_requests_;
	std::map<std::string, std::array<float, 3>> heading_dials_;
	std::map<std::string, Rectangle> sharing_pods_;
	std::map<std::uint64_t, std::string> sharing_contacts_;
	bool sharing_hit(const std::string &key, float x, float y) const
	{
		const auto found = sharing_pods_.find(key);
		if (found == sharing_pods_.end())
			return false;
		const auto &pod = found->second;
		const float dx = x - pod.x, dy = y - pod.y;
		return CheckCollisionPointRec({x, y}, pod) &&
		       std::min(dx, pod.width - dx) + std::min(dy, pod.height - dy) >= 8.f;
	}
	std::map<std::string, std::string> dial_systems_;
	std::string asset_path(const std::string &name) const
	{
#ifdef __EMSCRIPTEN__
		return "/assets/helm/" + name;
#else
		return std::string(GetApplicationDirectory()) + "assets/helm/" + name;
#endif
	}
	Texture2D hud(const std::string &name)
	{
		if (!hud_.contains(name))
		{
			const auto texture = LoadTexture(asset_path(name + ".png").c_str());
			if (!texture.id)
				throw std::runtime_error("Missing Console artwork: " + name);
			hud_[name] = texture;
		}
		return hud_.at(name);
	}
	static void image(Texture2D texture, Rectangle area)
	{
		DrawTexturePro(texture, {0, 0, static_cast<float>(texture.width), static_cast<float>(texture.height)}, area,
		               {0, 0}, 0, WHITE);
	}
	Vector2 widget_offset(const std::string &key) const
	{
		const auto at = document_->GetElementById("widget/" + key)->GetAbsoluteOffset(Rml::BoxArea::Border);
		return {at.x, at.y};
	}
	Rectangle throttle_rect(const std::string &key) const
	{
		auto *widget = document_->GetElementById("widget/" + key);
		const auto at = widget_offset(key);
		const auto &size = hud_metadata_.at("helm-throttle-track").at("image_size");
		const float height = std::max(30.f, widget->GetOffsetHeight() - 100),
		            width = height * size[0].get<float>() / size[1].get<float>();
		return {at.x + (widget->GetOffsetWidth() - width) / 2, at.y + 45, width, height};
	}
	void place(Application &app, const ConsoleRegion &region, const std::string &name)
	{
		if (auto *element = this->element(name))
		{
			const auto at = element->GetAbsoluteOffset(Rml::BoxArea::Border);
			app.control_placements[name] = {region.x + at.x,
			                                region.y + at.y,
			                                element->GetOffsetWidth(),
			                                element->GetOffsetHeight(),
			                                context_->GetFocusElement() == element,
			                                true};
		}
	}
	std::string id(const std::string &action) const
	{
		return "console/" + std::to_string(route_) + "/" + action;
	}
<<<<<<< HEAD
	std::string button(const std::string &action, const std::string &label, const std::string &visual = "")
=======
	std::string on_screen_markup(const std::string &action) const
	{
		return "<button class=\"on-screen\" id=\"" + Rml::StringUtilities::EncodeRml(id(action)) +
		       "\"><span class=\"share-screw share-screw-left\"/><span class=\"share-screw share-screw-right\"/>"
		       "<span class=\"share-monitor\"><span/></span><span class=\"share-label\">" +
		       Rml::StringUtilities::EncodeRml(localise("On screen")) +
		       "</span><span class=\"share-caption share-idle\">" +
		       Rml::StringUtilities::EncodeRml(localise("Standby")) +
		       "</span><span class=\"share-caption share-live\">" + Rml::StringUtilities::EncodeRml(localise("Live")) +
		       "</span><span class=\"share-lamp-housing\"><span class=\"share-lamp\"/></span></button>";
	}
	std::string button(const std::string &action, const std::string &label)
>>>>>>> 70549d2 (Style On screen controls with live lamps and radar bezel mounting)
	{
		controls_.emplace_back(id(action), label);
		if (action.starts_with("on-screen/"))
			return on_screen_markup(action);
		return "<button id=\"" + Rml::StringUtilities::EncodeRml(id(action)) + "\">" +
		       (visual.empty() ? Rml::StringUtilities::EncodeRml(translate(label, app_->pseudo_locale))
		                       : "<span class=\"camera-icon\">" + Rml::StringUtilities::EncodeRml(visual) + "</span>") +
		       "</button>";
	}

public:
	explicit ConsoleSurface(ConsoleId route, const std::string &prefix = "console-", bool toolbar = true)
	    : route_(route), context_name_(prefix + std::to_string(route)), toolbar_enabled_(toolbar)
	{
		terminal_ =
		    parse_json(read_file(asset_path("../menu/menu-scene.json"), 64 * 1024), "menu scene").at("terminal");
		for (int i = 1; i <= 12; ++i)
			crew_portraits_.emplace(
			    sha256(read_file(asset_path("../menu/avatar-" + std::to_string(i) + ".png"), 12288)),
			    read_file(asset_path("../menu/portrait-" + std::to_string(i) + ".png"), 512 * 1024));
		context_ = Rml::CreateContext(context_name_, {1280, 720});
		if (!context_)
			throw std::runtime_error("Cannot create independent Console context");
		document_ = context_->LoadDocumentFromMemory(R"(<rml><head><style>
body {width:100%;height:100%;font-family:LatoLatin;font-size:20px;color:#e5e4ca;}
h1 {font-size:1.3em;display:inline-block;margin:0 18px 8px 0;}
h2 {font-size:1em;display:block;margin:4px 0;}
button {display:inline-block;padding:10px;margin:4px;border:1px #9c8b68;background:#222b2b;color:#f0e3c2;}
button:hover,button:focus {background:#455458;border-color:#ec9c52;}
#toolbar {position:absolute;left:20px;top:18px;right:20px;height:85px;overflow:visible;z-index:2;}
#toolbar h1 {margin-left:76px;}
#connection-status {font-size:.75em;}
#widgets {position:absolute;left:4%;right:4%;top:125px;bottom:4%;}
#gameplay {position:absolute;left:60%;right:4%;top:125px;bottom:4%;overflow-x:hidden;overflow-y:auto;clip:always;box-sizing:border-box;background:#0b1719ee;border:1px #988b71;padding:8px;display:none;}
#gameplay-controls button {width:210px;min-height:48px;box-sizing:border-box;}
.engineering #widgets {display:block;pointer-events:none;z-index:4;}
.engineering #widgets .widget {display:none;}
.engineering #widgets .camera_mode-widget,.engineering #widgets .camera_direction-widget {display:block;pointer-events:auto;}
.engineering #connection-status {display:none;}
.engineering #gameplay {background:transparent;border:0px;}
#engineering-cards {display:block;width:100%;}
.engineering-card {display:inline-block;vertical-align:top;box-sizing:border-box;margin:0 12px 14px 0;padding:16px;background:#0b1719ee;border:1px #988b71;}
.resource-meter {display:block;width:100%;height:12px;margin:5px 0;background:#263534;}
.resource-fill {display:block;height:100%;background:#88c6a2;}
.resource-heat {background:#ef9958;}
.engineering-card h2 {color:#ffbd76;}
.engineering-card p {min-height:56px;line-height:1.5;}
.engineering-card button {width:180px;min-height:48px;box-sizing:border-box;font-size:.85em;}
.widget {position:absolute;box-sizing:border-box;padding:8px;overflow:hidden;}
p {display:block;font-size:.85em;margin:4px 0;}
.dial {position:absolute;left:12px;right:12px;top:48px;bottom:72px;}
.widget button {position:absolute;bottom:8px;left:8px;}
.radar-widget button.on-screen {position:absolute;left:auto;right:8px;}
.readout {position:absolute;bottom:12px;left:16px;right:16px;height:44px;text-align:center;color:#cbe9c8;}
.heading-value {display:inline-block;vertical-align:middle;margin:0 8px;width:145px;height:36px;background:#0a1719;color:#cbe9c8;border:1px #988b71;font-size:1.3em;}
.throttle {position:absolute;background:transparent;border:0px;}
.throttle slidertrack {background:transparent;}
.throttle sliderbar {width:100%;background:transparent;}
.heading-value:focus {border-color:#ffb665;background:#122629;}
.throttle sliderarrowdec,.throttle sliderarrowinc {display:none;}
.ready-card {position:absolute;left:10%;top:390px;width:80%;box-sizing:border-box;padding:28px;background:#0b1719ee;border:2px #d48a48;}
.profile-card {top:18%;}
.ready-card h1 {display:block;font-size:32px;}
.ready-card button {min-width:120px;}
.ready-card .avatar-choice {min-width:0;width:64px;height:64px;padding:6px;}
.ready-card input {display:block;width:90%;height:40px;background:#211f1a;color:#fff0d4;margin:10px 0;}
.profile-tv {position:absolute;right:20px;top:0;width:264px;height:330px;background:transparent;border:0px;padding:0px;}
.profile-tv img,.avatar-choice img {pointer-events:none;}
.settings-cog {position:absolute;left:0;top:0;width:64px;height:64px;margin:0;padding:0px;background:transparent;border:2px transparent;}
.settings-cog img {position:absolute;left:4px;top:4px;width:54px;height:54px;pointer-events:none;}
.helm #toolbar {right:auto;height:64px;}
.helm #toolbar h1,.helm #connection-status,.helm .radar-widget h2,.helm .heading-widget h2,.helm .heading-widget label,.helm .heading-widget p {display:none;}
.helm .radar-widget {overflow:visible;}
.helm .dial {left:0;right:0;top:0;bottom:0;}
.helm .radar-widget button,.helm .heading-value {opacity:0;}
.helm .radar-widget button:focus,.helm .heading-value:focus {opacity:1;}
.helm .radar-widget button.on-screen {opacity:1;position:absolute;bottom:auto;right:auto;min-width:0;z-index:4;}
.helm .radar-widget button.on-screen,.helm .radar-widget button.on-screen:hover,.helm .radar-widget button.on-screen:focus {background:transparent;border:0px;margin:0;padding:0;min-height:0;border-radius:0px;pointer-events:none;}
.helm .radar-widget button.on-screen span {display:none;}
.helm .radar-widget button.on-screen .share-label {display:block;left:20px;right:44px;top:9px;height:30px;line-height:30px;font-size:22px;text-align:left;color:#f0dfbf;}
.helm .radar-widget button.on-screen .share-caption {display:block;left:21px;bottom:10px;font-size:14px;}
.helm .radar-widget button.on-screen .share-live {display:none;}
.helm .radar-widget button.on-screen-active .share-live {display:block;}
.helm .radar-widget button.on-screen-active .share-idle {display:none;}

button.on-screen {position:relative;box-sizing:border-box;width:180px;height:66px;min-height:66px;margin:4px;padding:0;border:3px #535b59;border-top-color:#9b8770;border-left-color:#8c735a;border-bottom-color:#141a1c;border-right-color:#202728;background:#242e30;color:#efdfbd;border-radius:7px;vertical-align:middle;}
button.on-screen span {pointer-events:none;}
button.on-screen:hover {background:#303c3e;border-top-color:#d1a26d;}
button.on-screen:focus {border-color:#ffd09a;}
button.on-screen .share-monitor {position:absolute;left:12px;top:18px;width:25px;height:17px;border:2px #a7906d;border-radius:2px;background:#081315;pointer-events:none;}
button.on-screen .share-monitor span {position:absolute;left:8px;top:19px;width:10px;height:3px;background:#a7906d;}
button.on-screen .share-label {position:absolute;left:48px;right:30px;top:9px;font-size:16px;color:#f0dfbf;pointer-events:none;}
button.on-screen .share-caption {position:absolute;left:49px;bottom:9px;font-size:11px;color:#a3aaa1;pointer-events:none;}
button.on-screen .share-live {display:none;color:#a9f5b8;}
button.on-screen .share-lamp-housing {position:absolute;right:9px;top:15px;width:20px;height:20px;border:1px #11191a;border-top-color:#758078;border-radius:11px;background:#17231d;pointer-events:none;}
button.on-screen .share-lamp {position:absolute;left:4px;top:4px;width:10px;height:10px;border-radius:6px;background:#34493b;border:1px #172a1c;}
button.on-screen .share-screw {position:absolute;top:3px;width:5px;height:3px;background:#11191a;border-top:1px #9b9c86;pointer-events:none;}
button.on-screen .share-screw-left {left:4px;}
button.on-screen .share-screw-right {right:4px;}
button.on-screen-active {background:#26342e;border-top-color:#a18f6e;}
button.on-screen-active .share-lamp-housing {background:#225331;}
button.on-screen-active .share-lamp {background:#8aff9a;border-color:#bfffbb;}
button.on-screen-active .share-monitor {border-color:#b2d2b5;}
button.on-screen-active .share-idle {display:none;}
button.on-screen-active .share-live {display:block;}
.helm #gameplay {background:transparent;border:0px;padding:0px;}
.helm .radar-widget button.manual-toggle {display:block;opacity:1;left:5%;bottom:4%;width:220px;height:82px;padding:0;margin:0;background:transparent;border:2px transparent;tab-index:auto;}
.manual-toggle .manual-art {position:absolute;left:0;top:0;width:100%;height:100%;pointer-events:none;}
.manual-toggle .manual-label {position:absolute;left:17%;top:50%;margin-top:-12px;height:24px;font-size:18px;color:#f3e6c9;pointer-events:none;}
.manual-toggle .manual-lamp {position:absolute;left:55%;top:50%;margin-top:-11px;width:22px;height:22px;box-sizing:border-box;border:3px #ff625f;background:#410e10;color:#ffc4ba;font-family:Space Mono;font-size:17px;line-height:16px;text-align:center;pointer-events:none;}
.manual-toggle.manual-on .manual-lamp {border-radius:11px;background:#4fd791;border-color:#a5ffcb;color:#09261b;}
.manual-toggle .manual-state {position:absolute;left:70%;top:50%;margin-top:-10px;height:20px;font-family:Space Mono;font-size:13px;line-height:20px;color:#f3e6c9;pointer-events:none;}
.helm .radar-widget button.manual-toggle:hover,.helm .radar-widget button.manual-toggle:focus {border-color:#ffe4b0;}
.helm .radar-widget button.manual-toggle:disabled {opacity:.5;}

.profile-tv span {position:absolute;left:20%;top:69%;width:55%;text-align:center;pointer-events:none;}
#manual-flight {position:absolute;right:2%;top:76px;width:22%;min-width:180px;max-width:280px;max-height:80%;overflow:auto;z-index:8;background:#08171bee;padding:8px;box-sizing:border-box;}
#manual-flight button {width:46%;min-height:44px;margin:2px;padding:6px;font-size:16px;box-sizing:border-box;}
#manual-flight button.flight-mode {width:96%;}
#manual-stick {display:block;position:relative;width:160px;height:160px;margin:8px auto;border:2px #98cbb8;border-radius:80px;background:#10292a;}
#manual-knob {display:block;position:absolute;width:32px;height:32px;margin-left:-16px;margin-top:-16px;left:50%;top:50%;border-radius:16px;background:#bce8ad;pointer-events:none;}
#manual-flight p {font-size:14px;}
.manual-flight .throttle-widget,.manual-flight .heading-widget {visibility:hidden;}
.local-settings-open #manual-flight {visibility:hidden;}
#local-settings {display:none;position:absolute;left:0;top:0;width:100%;height:100%;background:#080d0fee;z-index:20;}
.local-settings-open #local-settings {display:block;}
.local-settings-open #toolbar,.local-settings-open #widgets,.local-settings-open #gameplay {visibility:hidden;}
.local-settings-card {position:absolute;left:8%;top:8%;width:84%;max-height:84%;overflow:auto;box-sizing:border-box;padding:24px;background:#0b1719;border:2px #988b71;}
.local-settings-card h1 {display:block;}
.local-settings-card button {width:190px;min-height:48px;box-sizing:border-box;}
.navigation-console #gameplay {display:none;}
.navigation-console #widgets {display:block;pointer-events:none;z-index:4;}
.navigation-console #widgets .widget {display:none;}
.navigation-console #widgets .camera_mode-widget,.navigation-console #widgets .camera_direction-widget {display:block;pointer-events:auto;}
.navigation-console #gameplay {left:45%;top:0;width:45%;height:100px;background:transparent;}
.navigation-controls {position:absolute;left:1%;bottom:1%;width:98%;background:#08171bee;}
.navigation-controls button {min-height:42px;margin:3px;}
.navigation-controls input {width:140px;}
.navigation-objectives {position:absolute;left:12px;top:115px;max-width:320px;background:#08171bdd;padding:8px;}
.mission-result {position:absolute;left:0;top:0;width:100%;height:100%;background:#070c09;z-index:15;color:#bce8ad;pointer-events:auto;}
.mission-art {position:absolute;left:0;top:0;width:100%;height:100%;}
.camera-choices {position:absolute;top:28px;left:8px;width:100px;height:100px;}
.camera-art {position:absolute;left:0;top:0;width:100%;height:100%;pointer-events:none;}
.camera-choices button {display:block;position:absolute;bottom:auto;box-sizing:border-box;min-width:0;min-height:0;font-family:Space Mono;font-size:28px;line-height:1;padding:0;margin:0;tab-index:auto;text-align:center;background:transparent;border:2px transparent;color:#e9dec4;}
.camera-icon {position:absolute;display:block;left:0;right:0;top:50%;margin-top:-14px;height:28px;pointer-events:none;}
.camera_mode-widget .camera-choices button {font-size:24px;}
.camera_mode-widget .camera-icon {margin-top:-12px;height:24px;}
.camera-choices button:hover {background:#ffc07a22;border-color:#e9dec488;}
.camera-choices button.selected {background:#fca65133;border-color:#ffc078;color:#ffc078;}
.camera-choices button:focus {border-color:#fff3d9;background:#fff3d933;}
.camera_mode-widget h2,.camera_direction-widget h2 {font-size:14px;margin:0;color:#e9dec4;background:#071012bb;padding:2px 4px;}
.camera-status {position:absolute;left:0;right:0;text-align:center;font-size:14px;color:#f1ddaf;pointer-events:none;}
.mission-copy {position:absolute;left:20%;top:21%;width:60%;height:51%;overflow:auto;font-family:Space Mono;}
.mission-copy h1 {font-size:18px;letter-spacing:3px;color:#81af79;margin:0 0 22px 0;}
.mission-copy h2 {font-size:44px;letter-spacing:2px;color:#d1f9be;margin:0 0 20px 0;}
.mission-copy p {font-size:22px;margin:10px 0;}
.mission-copy .terminal-rule {height:1px;background:#51744c;margin:18px 0;}
.mission-copy .terminal-status {font-size:16px;letter-spacing:2px;color:#81af79;}
.mission-copy button {margin:22px 0 0 0;padding:12px 20px;background:#122116;border:1px #87b77b;color:#d1f9be;}
.mission-copy button:hover,.mission-copy button:focus {background:#bce8ad;color:#102014;border-color:#d1f9be;}
#navigation-layer {position:absolute;left:0;top:0;width:100%;height:100%;pointer-events:none;}
#navigation-layer button,#navigation-layer input,#navigation-canvas {pointer-events:auto;}
</style></head><body><div id="toolbar"></div><div id="widgets"></div><div id="gameplay"><div id="engineering-cards"></div><div id="gameplay-controls"></div><div id="gameplay-readouts"></div></div><div id="lobby"></div><div id="navigation-layer"></div><div id="manual-flight"></div><div id="local-settings"></div></body></rml>)");
		if (!document_)
			throw std::runtime_error("Cannot create Console document");
		document_->AddEventListener("click", this);
		document_->AddEventListener("mousescroll", this);
		document_->AddEventListener("change", this);
		document_->AddEventListener("mousedown", this);
		document_->AddEventListener("mouseup", this);
		document_->AddEventListener("mouseout", this);
		document_->AddEventListener("touchstart", this);
		document_->AddEventListener("touchend", this);
		document_->AddEventListener("touchcancel", this);
		for (const char *name :
		     {"helm-throttle-track", "helm-throttle-handle", "helm-radar-ring", "helm-background-9slice"})
			hud_metadata_[name] =
			    parse_json(read_file(asset_path(std::string(name) + ".sprite.json"), 64 * 1024), name);
		document_->Show();
	}
	~ConsoleSurface()
	{
		for (const auto &[name, texture] : hud_)
			UnloadTexture(texture);
		Rml::RemoveContext(context_name_);
	}
	Rml::Context &context()
	{
		return *context_;
	}
	bool action(const std::string &name)
	{
		const auto prefix = id("");
		if (!app_ || !name.starts_with(prefix))
			return false;
		const auto action = name.substr(prefix.size());
		if (app_ && navigation_action(action))
			return true;
		if ((app_->screen != Screen::console && app_->screen != Screen::viewscreen &&
		     app_->screen != Screen::server_lobby && app_->screen != Screen::settings) ||
		    owner_.lock() != app_->session || (app_->session && generation_ != app_->session->generation()))
			return false;
		if (action == "manual-mode")
		{
			if (settings_open_ || app_->screen == Screen::settings)
				return false;
			cancel_pointer();
			const bool accepted = model_.set_manual(!model_.manual_active());
			app_->accessible_status = model_.announcement();
			return accepted;
		}
		if (action == "manual-left" || action == "manual-right" || action == "manual-forward" ||
		    action == "manual-reverse")
		{
			if (!model_.manual_active() || settings_open_ || app_->screen == Screen::settings)
				return false;
			manual_input_.sample("accessible", {action == "manual-left"    ? 1000
			                                    : action == "manual-right" ? -1000
			                                                               : 0,
			                                    action == "manual-forward"   ? 1000
			                                    : action == "manual-reverse" ? -1000
			                                                                 : 0});
			manual_pulse_tick_ = app_->session->ticks();
			flush_manual();
			return true;
		}
		if (action == "manual-neutral")
		{
			cancel_pointer();
			return true;
		}
		if (action.starts_with("on-screen/"))
			return !settings_open_ && model_.on_screen(action.substr(10), display_camera(action.substr(10)));
#ifndef __EMSCRIPTEN__
		if (action == "settings" || action == "local-settings.close")
		{
			if (!game_active(*app_))
				return false;
			settings_open_ = action == "settings" && !settings_open_;
			cancel_pointer();
			model_.release_controls();
			document_->SetClass("local-settings-open", settings_open_);
			if (!settings_open_)
				if (auto *cog = document_->GetElementById(id("settings")))
					cog->Focus();
			return true;
		}
		if (settings_open_)
		{
			if (std::none_of(local_settings_controls_.begin(), local_settings_controls_.end(),
			                 [&](const auto &control)
			                 {
				                 return control.first == name;
			                 }))
				return false;
			if (action.starts_with("local-settings.tab/"))
				local_settings_tab_ = action.substr(19);
			else if (action == "local-settings.smaller")
				text_scale_ = std::max(80, text_scale_ - 10);
			else if (action == "local-settings.larger")
				text_scale_ = std::min(150, text_scale_ + 10);
			else if (action == "local-settings.language")
			{
				const auto languages = installed_languages();
				auto it = std::find(languages.begin(), languages.end(), local_language_);
				local_language_ = it == languages.end() || ++it == languages.end() ? languages.front() : *it;
				signature_.clear();
			}
			else if (action.starts_with("local-settings.audio/"))
			{
				const auto channel = action.substr(21, action.find('/', 21) - 21);
				auto &value = channel == "master"  ? app_->master_volume
				              : channel == "music" ? app_->music_volume
				                                   : app_->sfx_volume;
				value = std::clamp(value + (action.ends_with("/up") ? 5 : -5), 0, 100);
				remember_preferences(*app_);
			}
			else
				return model_.act(action);
			return true;
		}
#endif
		if (action == "profile" || action == "profile-save" || action.starts_with("avatar/"))
		{
			if (!app_->session->lobby().active || app_->session->lobby().launched)
				return false;
			if (action == "profile")
				profile_open_ = !profile_open_;
			else if (action == "profile-save")
			{
				if (!app_->session->set_local_player_profile(route_, profile_name_, profile_avatar_))
					return false;
				profile_open_ = false;
			}
			else
			{
				int choice = 0;
				const auto value = action.substr(7);
				if (std::from_chars(value.data(), value.data() + value.size(), choice).ec != std::errc{} ||
				    choice < 1 || choice > 12)
					return false;
				profile_avatar_ = read_file(asset_path("../menu/avatar-" + std::to_string(choice) + ".png"), 12288);
			}
			signature_.clear();
			return true;
		}
		if (action.starts_with("complexity/"))
		{
			const auto lobby = app_->session->lobby();
			if (!lobby.active || lobby.launched)
				return false;
			for (const auto &seat : lobby.consoles)
				if (seat.local_screen == route_)
				{
					LobbyCommand command{LobbyAction::select_profile};
					command.slot = seat.slot;
					command.console = seat.id;
					command.profile = action.substr(11);
					return app_->session->lobby_command(command);
				}
			return false;
		}
		if (action == "ready")
		{
			const auto lobby = app_->session->lobby();
			if (!lobby.active || lobby.launched)
				return false;
			for (const auto &seat : lobby.consoles)
				if (seat.local_screen == route_)
				{
					LobbyCommand command{seat.ready ? LobbyAction::unready : LobbyAction::ready};
					command.screen = route_;
					return app_->session->lobby_command(command);
				}
			return false;
		}

		if (action == "manual")
		{
			if (!manual_toggle_ || !game_active(*app_) || app_->screen == Screen::settings || !model_.observation())
				return false;
			const bool manual = model_.observation()->gameplay.value("profile", "") != "automatic";
			const auto preset = manual_preset(manual);
			if (!preset)
				return false;
			const bool accepted = model_.act(descriptor_action(*preset));
			app_->accessible_status = model_.announcement();
			return accepted;
		}

		if ((action.starts_with("camera/") || action == "game.camera" || action == "game.camera-back") &&
		    (!game_active(*app_) || app_->screen == Screen::settings))
			return false;
		if (action == "game.manual")
		{
			if (!manual_toggle_ || !game_active(*app_) || app_->screen == Screen::settings)
				return false;
			if (auto *toggle = element(id("manual")))
				return toggle->Focus();
			return false;
		}
		if (action == "game.camera")
		{
			for (const auto &[name, label] : controls_)
				if (name.find("/camera/") != std::string::npos)
					if (auto *choice = element(name))
					{
						choice->Focus();
						return true;
					}
			return false;
		}
		if (auto *focused = context_->GetFocusElement();
		    focused && (focused->GetId().find("/camera/") != std::string::npos || focused->GetId() == id("manual")))
		{
			if (action == "game.camera-back")
			{
				focused->Blur();
				return true;
			}
			if (action == "game.contacts")
			{
				context_->ProcessKeyDown(Rml::Input::KI_RETURN, 0);
				context_->ProcessKeyUp(Rml::Input::KI_RETURN, 0);
				return true;
			}
			if (action == "game.left" || action == "game.right" || action == "game.faster" || action == "game.slower")
			{
				std::vector<std::string> choices;
				for (const auto &[name, label] : controls_)
					if (name.find("/camera/") != std::string::npos || name == id("manual"))
						choices.push_back(name);
				const auto current = std::find(choices.begin(), choices.end(), focused->GetId());
				if (current == choices.end() || choices.empty())
					return false;
				const int step = action == "game.left" || action == "game.faster" ? -1 : 1;
				const auto index =
				    (static_cast<int>(current - choices.begin()) + static_cast<int>(choices.size()) + step) %
				    choices.size();
				if (auto *choice = element(choices[index]))
					choice->Focus();
				return true;
			}
		}
		if (action.starts_with("camera/"))
		{
			if (owner_.lock() != app_->session || generation_ != app_->session->generation())
				return false;
			const auto split = action.rfind('/');
			if (split <= 7 || split + 2 != action.size() || action.back() < '0' || action.back() > '7')
				return false;
			const auto key = action.substr(7, split - 7);
			const auto *layout = model_.layout();
			if (!layout)
				return false;
			for (const auto &widget : layout->at("widgets"))
				if (widget.at("id") == key &&
				    (widget.at("kind") == "camera_mode" || widget.at("kind") == "camera_direction"))
					return model_.command(widget.at("kind"), action.back() - '0');
			return false;
		}

		if (action == "settings")
		{
			app_->settings_console = route_;
			return invoke_navigation(*app_, "settings");
		}
		if (action == "release")
		{
#ifdef __EMSCRIPTEN__
			return app_->session && app_->session->release_console(route_);
#else
			return false;
#endif
		}
		if (action.starts_with("claim/"))
		{
#ifndef __EMSCRIPTEN__
			if (game_active(*app_))
				return false;
#endif
			for (const auto &seat : app_->session->console_seats())
				if (action == "claim/" + std::to_string(seat.address.ship) + "/" + seat.address.console)
					return app_->session->claim_console(route_, seat.address);
			return false;
		}
		if (action.starts_with("pad/"))
		{
			app_->gamepad_assignments[action.substr(4)] = route_;
			return true;
		}
		const bool accepted = model_.act(action);
		app_->accessible_status = model_.announcement();
		return accepted;
	}
	void ProcessEvent(Rml::Event &event) override
	{
		if (syncing_values_)
			return;
		const auto name = event.GetTargetElement()->GetId();
		if (event.GetType() == "mousescroll" && navigation_visible_ && !settings_open_)
		{
			const auto x = event.GetParameter<float>("mouse_x", 0), y = event.GetParameter<float>("mouse_y", 0);
			if (CheckCollisionPointRec({x, y}, navigation_area_))
				navigation_camera_.zoom(std::pow(1.2, event.GetParameter<float>("wheel_delta", 0)),
				                        x - navigation_area_.width / 2,
				                        y - navigation_area_.y - navigation_area_.height / 2);
			return;
		}
		if (event.GetType() == "mousedown" || event.GetType() == "touchstart")
		{
			if (name.starts_with(id("game.control/")) && model_.hold_control(name.substr(id("").size())))
				held_clicks_.insert(name);
			return;
		}
		if (event.GetType() == "mouseup" || event.GetType() == "mouseout" || event.GetType() == "touchend" ||
		    event.GetType() == "touchcancel")
		{
			if (name.starts_with(id("game.control/")))
				model_.release_control(name.substr(id("").size()));
			if (event.GetType() == "mouseout" || event.GetType() == "touchcancel")
				held_clicks_.erase(name);
			return;
		}
		if (event.GetType() == "click" && held_clicks_.erase(name))
			return;
		if (event.GetType() == "change")
		{
			if (auto *field = dynamic_cast<Rml::ElementFormControl *>(event.GetTargetElement()))
			{
				if (name.starts_with(id("speed/")))
				{
					double value = 0;
					const auto text = field->GetValue();
					if (std::from_chars(text.data(), text.data() + text.size(), value).ec == std::errc{})
						set_value(name, std::to_string(-value));
				}
				else
					set_value(name, field->GetValue());
			}
			return;
		}
		if (app_ && app_->screen == Screen::server_lobby && name == id("ready"))
			invoke_navigation(*app_, name);
		else
			action(name);
	}
	void focus(const std::string &name)
	{
		if (auto *element = this->element(name))
			element->Focus(true);
	}
	bool set_value(const std::string &name, const std::string &text)
	{
		if (name == id("nav.x") || name == id("nav.y"))
		{
			std::int64_t value = 0;
			const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
			if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value < -1000000000 ||
			    value > 1000000000)
				return false;
			(name == id("nav.x") ? navigation_x_ : navigation_y_) = static_cast<double>(value);
			return true;
		}
		if (settings_open_)
			return false;
		if (app_ && profile_open_ && name == id("profile-name"))
		{
			profile_name_ = text;
			return true;
		}
		if (!app_ || !name.starts_with(id("")) || !game_active(*app_) || owner_.lock() != app_->session ||
		    generation_ != app_->session->generation())
			return false;
		double value = 0;
		const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
		if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(value))
			return false;
		const auto action = name.substr(id("").size());
		if (model_.manual_active() && (action.starts_with("speed/") || action.starts_with("heading/")))
			return false;
		bool accepted = false;
		if (action.starts_with("speed/"))
			accepted = model_.set_speed_fraction(value / 100.0, model_.widget_system(action.substr(6)));
		else if (action.starts_with("heading/"))
			accepted = model_.set_heading_degrees(value, model_.widget_system(action.substr(8)));
		if (accepted)
		{
			app_->accessible_status = model_.announcement();
			if (action.starts_with("heading/"))
				if (auto *field = dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(name));
				    field && field->GetValue() != text)
					field->SetValue(text);
		}
		return accepted;
	}
	void flush_manual()
	{
		if (!app_ || !app_->session || !model_.manual_active() || settings_open_)
			return;
		const auto axes = manual_input_.axes();
		if (axes == manual_sent_)
			return;
		const auto tick = app_->session->ticks();
		const bool releasing =
		    (axes.yaw == 0 && manual_sent_.yaw != 0) || (axes.thrust == 0 && manual_sent_.thrust != 0);
		if (!releasing && axes != FlightAxes{} && manual_sent_tick_ == tick)
			return;
		if (model_.set_manual_axes(axes.yaw, axes.thrust))
		{
			manual_sent_ = axes;
			manual_sent_tick_ = tick;
		}
	}
	void cancel_gamepad(const std::string &device)
	{
		manual_input_.cancel_source("pad/" + device);
		flush_manual();
	}
	void gamepad(const std::string &device, const std::string &control, float value)
	{
		auto &axes = pad_axes_[device];
		(control == "flight.yaw" ? axes.yaw : axes.thrust) = static_cast<int>(std::lround(value * 1000));
		manual_input_.sample("pad/" + device, axes);
		if (!model_.manual_active() || settings_open_)
			manual_input_.cancel();
		// Both stick axes are sampled before the frame submits their combined value.
	}
	void manual_frame(bool keyboard_selected, FlightAxes keyboard_axes, bool focused)
	{
		manual_input_.sample("keyboard", keyboard_axes);
		if (!keyboard_selected)
			manual_input_.cancel_source("keyboard");
		const auto *focus = context_->GetFocusElement();
		if (focus && focus->GetTagName() == "input")
			manual_input_.cancel_source("keyboard");
		std::string identity;
		if (model_.observation())
		{
			identity = std::to_string(model_.observation()->own.id) + "/" + model_.observation()->console;
			for (const auto &controller : model_.observation()->gameplay.value("controllers", Json::array()))
				identity += controller.at("id").get<std::string>() + "/" + controller.at("generation").dump() +
				            controller.at("human").dump();
		}
		const bool active = model_.manual_active();
		if (identity != manual_identity_ || active != manual_was_active_ || !active || settings_open_ || !focused)
		{
			manual_input_.cancel();
			if (manual_was_active_ || manual_sent_ != FlightAxes{})
				model_.release_controls();
			manual_sent_ = {};
			joystick_contact_.reset();
			manual_pulse_tick_.reset();
		}
		manual_identity_ = identity;
		manual_was_active_ = active;
		if (manual_pulse_tick_ && app_->session->ticks() > *manual_pulse_tick_)
		{
			manual_input_.sample("accessible", {});
			manual_pulse_tick_.reset();
			flush_manual();
		}
		flush_manual();
	}
	void prepare_manual(Application &app, const ConsoleRegion &region, bool pregame)
	{
		auto *panel = document_->GetElementById("manual-flight");
		const bool available = !pregame && model_.manual_available();
		property(panel, "display", available ? "block" : "none");
		const bool active = available && model_.manual_active();
		document_->SetClass("manual-flight", active);
		if (!available)
			return;
		std::vector<std::pair<std::string, std::string>> actions{{"manual-mode", "Manual flight"}};
		if (active)
			for (const auto &[name, label] :
			     std::vector<std::pair<std::string, std::string>>{{"manual-left", "Yaw left (pulse)"},
			                                                      {"manual-right", "Yaw right (pulse)"},
			                                                      {"manual-forward", "Forward thrust (pulse)"},
			                                                      {"manual-reverse", "Reverse thrust (pulse)"},
			                                                      {"manual-neutral", "Release flight controls"}})
				actions.emplace_back(name, label);
		std::string markup = "<button class=\"flight-mode\" id=\"" + id("manual-mode") + "\">" +
		                     Rml::StringUtilities::EncodeRml(localise(active ? "Manual flight" : "Assisted flight")) +
		                     "</button>";
		if (active)
		{
			markup += "<div id=\"manual-stick\"><div id=\"manual-knob\"/></div><p id=\"manual-values\"/><p>" +
			          Rml::StringUtilities::EncodeRml(
			              localise("W/S thrust; A/D yaw. Left stick: yaw and thrust. Release to coast.")) +
			          "</p>";
			for (std::size_t i = 1; i < actions.size(); ++i)
				markup += "<button id=\"" + id(actions[i].first) + "\">" +
				          Rml::StringUtilities::EncodeRml(localise(actions[i].second)) + "</button>";
		}

		if (manual_markup_ != markup)
		{
			const auto focused = context_->GetFocusElement() ? context_->GetFocusElement()->GetId() : std::string{};
			properties_.clear();
			panel->SetInnerRML(markup);
			manual_markup_ = markup;
			if (auto *previous = document_->GetElementById(focused))
				previous->Focus();
		}
		context_->Update();
		for (const auto &[name, label] : actions)
		{
			app.screen_controls.emplace_back(id(name), localise(label));
			place(app, region, id(name));
		}
		app.screen_toggles[id("manual-mode")] = active;
		document_->GetElementById(id("manual-mode"))->SetAttribute("aria-pressed", active ? "true" : "false");
		joystick_area_ = {};
		if (active)
		{
			auto *stick = document_->GetElementById("manual-stick");
			const auto at = stick->GetAbsoluteOffset(Rml::BoxArea::Border);
			joystick_area_ = {at.x, at.y, stick->GetOffsetWidth(), stick->GetOffsetHeight()};
			const auto axes = manual_input_.axes();
			auto *knob = document_->GetElementById("manual-knob");
			property(knob, "left", std::to_string(50 - axes.yaw * .04f) + "%");
			property(knob, "top", std::to_string(50 - axes.thrust * .04f) + "%");
			const auto text = localise("Yaw") + " " + std::to_string(axes.yaw / 10) + "%; " + localise("Thrust") + " " +
			                  std::to_string(axes.thrust / 10) + "%";
			document_->GetElementById("manual-values")->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
			app.screen_readouts.emplace_back(id("manual-values"), text);
		}
	}
	bool pointer(std::uint64_t contact, float x, float y, DeviceEventKind kind)
	{
		if (settings_open_)
			return false;
		if (!app_ || !game_active(*app_) || owner_.lock() != app_->session ||
		    generation_ != app_->session->generation())
			return false;
		if (const auto held = sharing_contacts_.find(contact); held != sharing_contacts_.end())
		{
			const auto key = held->second;
			if (kind == DeviceEventKind::Up || kind == DeviceEventKind::Cancel)
			{
				sharing_contacts_.erase(held);
				if (kind == DeviceEventKind::Up && sharing_hit(key, x, y))
					action(id("on-screen/" + key));
			}
			return true;
		}
		if (kind == DeviceEventKind::Down)
			for (const auto &[key, dial] : sharing_pods_)
				if (sharing_hit(key, x, y))
				{
					sharing_contacts_[contact] = key;
					if (auto *b = document_->GetElementById(id("on-screen/" + key)))
						b->Focus();
					return true;
				}
		if (kind == DeviceEventKind::Down && model_.layout())
			for (const auto &w : model_.layout()->at("widgets"))
				if (w.at("kind") == "radar" && !sharing_pods_.contains(w.at("id").get<std::string>()))
					if (auto *b = document_->GetElementById(id("on-screen/" + w.at("id").get<std::string>())))
					{
						const auto at = b->GetAbsoluteOffset(Rml::BoxArea::Border);
						if (CheckCollisionPointRec({x, y}, {at.x, at.y, b->GetOffsetWidth(), b->GetOffsetHeight()}))
							return false;
					}
		if (navigation_pointer(contact, x, y, kind))
			return true;
		if (model_.manual_active())
		{
			if (kind == DeviceEventKind::Down && !joystick_contact_ && CheckCollisionPointRec({x, y}, joystick_area_))
				joystick_contact_ = contact;
			if (joystick_contact_ == contact)
			{
				if (kind == DeviceEventKind::Up || kind == DeviceEventKind::Cancel)
				{
					manual_input_.sample("joystick", {});
					joystick_contact_.reset();
					flush_manual();
				}
				else
				{
					const auto axis = [](float value)
					{
						return static_cast<int>(std::lround(std::clamp(value, -1.f, 1.f) * 1000));
					};
					manual_input_.sample(
					    "joystick",
					    {axis((joystick_area_.x + joystick_area_.width / 2 - x) / (joystick_area_.width / 2)),
					     axis((joystick_area_.y + joystick_area_.height / 2 - y) / (joystick_area_.height / 2))});
					flush_manual();
				}
				return true;
			}
			return false;
		}
		if (kind == DeviceEventKind::Down)
			for (const auto &[key, track] : throttle_tracks_)
				if (CheckCollisionPointRec({x, y}, {track.x - track.width * .2f, track.y - track.width * .2f,
				                                    track.width * 1.4f, track.height + track.width * .4f}))
				{
					throttle_contacts_[contact] = key;
					break;
				}
		if (const auto found = throttle_contacts_.find(contact); found != throttle_contacts_.end())
		{
			const auto key = found->second;
			if (kind != DeviceEventKind::Cancel && throttle_tracks_.contains(key))
			{
				const auto track = throttle_tracks_.at(key);
				const double fraction = std::clamp((track.y + track.height - y) / track.height, 0.f, 1.f);
				const auto system = model_.widget_system(key);
				const auto requested =
				    std::llround(model_.system_minimum(system) +
				                 (model_.system_maximum(system) - model_.system_minimum(system)) * fraction);
				if (requested != model_.system_request(system) &&
				    (kind == DeviceEventKind::Down || kind == DeviceEventKind::Up ||
				     GetTime() - throttle_sent_at_[key] >= .05))
				{
					if (model_.set_speed_fraction(fraction, system))
						throttle_sent_at_[key] = GetTime();
				}
			}
			if (kind == DeviceEventKind::Up || kind == DeviceEventKind::Cancel)
				throttle_contacts_.erase(contact);
			return true;
		}
		if (kind == DeviceEventKind::Down)
		{
			for (const auto &[key, dial] : heading_dials_)
			{
				const auto [cx, cy, radius] = dial;
				const auto length = std::hypot(x - cx, cy - y);
				if (length >= radius * .69f && length <= radius)
				{
					heading_contacts_[contact] = key;
					break;
				}
			}
			if (!heading_contacts_.contains(contact))
				return false;
		}
		if (!heading_contacts_.contains(contact))
			return false;
		const auto key = heading_contacts_.at(contact);
		const auto [cx, cy, radius] = heading_dials_.at(key);
		const float dx = x - cx, dy = cy - y, length = std::hypot(dx, dy);
		if (kind != DeviceEventKind::Cancel && length > 1)
		{
			const double degrees = std::fmod(std::atan2(dy, dx) * 180.0 / 3.141592653589793 + 360.0, 360.0);
			const auto &system = dial_systems_.at(key);
			const auto requested = static_cast<std::int64_t>(std::llround(degrees * 1000)) % 360000;
			if (requested != model_.system_request(system, "heading_mdeg") &&
			    (kind == DeviceEventKind::Down || kind == DeviceEventKind::Up ||
			     GetTime() - heading_sent_at_[key] >= .05))
			{
				if (model_.set_heading_degrees(degrees, system))
					heading_sent_at_[key] = GetTime();
				app_->accessible_status = model_.announcement();
			}
		}
		if (kind == DeviceEventKind::Up || kind == DeviceEventKind::Cancel)
			heading_contacts_.erase(contact);
		return true;
	}
	void cancel_pointer()
	{
		manual_input_.cancel();
		manual_sent_ = {};
		joystick_contact_.reset();
		manual_pulse_tick_.reset();
		navigation_contacts_.clear();
		navigation_starts_.clear();
		heading_contacts_.clear();
		sharing_contacts_.clear();
		throttle_contacts_.clear();
		model_.release_controls();
		held_clicks_.clear();
	}
	void update_view()
	{
		context_->Update();
		if (settings_open_)
			return;
		// Read geometry as one batch before changing any text. Interleaving these
		// operations forced a complete RmlUi layout for each telemetry readout.
		measure_readouts();
		bool changed = false;
		for (const auto &[name, text] : readout_values_)
			changed |= update_readout(name, text, false);
		if (changed)
			context_->Update();
	}
	bool probe_readout_scroll()
	{
		// Explicit graphical smoke: deferred text becomes current on scrolling,
		// while its semantic value was already current before it became visible.
		for (const auto &[name, text] : readout_values_)
			if (displayed_values_[name] != text)
			{
				const auto semantic =
				    std::find(app_->screen_readouts.begin(), app_->screen_readouts.end(), std::pair{id(name), text});
				if (semantic == app_->screen_readouts.end())
				{
					std::cerr << "Deferred readout semantic missing: " << name << std::endl;
					return false;
				}
				auto *panel = document_->GetElementById("gameplay");
				const float previous_scroll = panel->GetScrollTop();
				element(id(name))->ScrollIntoView({Rml::ScrollAlignment::Start, Rml::ScrollAlignment::Nearest,
				                                   Rml::ScrollBehavior::Instant, Rml::ScrollParentage::Closest});
				update_view();
				if (displayed_values_[name] != text)
				{
					std::cerr << "Deferred readout did not refresh: " << name << std::endl;
					return false;
				}
				panel->SetScrollTop(previous_scroll);
				update_view();
				std::cout << "Deferred readout scroll and current semantics passed" << std::endl;
				return true;
			}
		return false;
	}
	bool probe_report()
	{
		for (const auto &[name, text] : readout_values_)
			if (text.find("Engine Console report") != std::string::npos)
			{
				auto *target = element(id(name));
				if (!target)
					return false;
				target->ScrollIntoView();
				context_->Update();
				return true;
			}
		return false;
	}
	bool probe_on_screen(const std::string &widget)
	{
		auto *control = document_->GetElementById(id("on-screen/" + widget));
		if (!control)
			return false;
		const auto at = control->GetAbsoluteOffset(Rml::BoxArea::Border);
		const float x = at.x + control->GetOffsetWidth() / 2, y = at.y + control->GetOffsetHeight() / 2;
		// Exercise the same custom-dial interception and RmlUi hit testing as a pointer click.
		if (sharing_pods_.contains(widget))
		{
			if (heading_dials_.contains(widget))
			{
				const auto [cx, cy, radius] = heading_dials_.at(widget);
				for (int degrees = 0; degrees < 360; ++degrees)
				{
					const float angle = degrees * 3.141592653589793f / 180;
					if (sharing_hit(widget, cx + std::cos(angle) * radius * .97f, cy + std::sin(angle) * radius * .97f))
						return false;
				}
			}
			if (pointer(999998, at.x + 1, at.y + 1, DeviceEventKind::Down))
				return false;
			if (!pointer(999999, x, y, DeviceEventKind::Down) ||
			    !pointer(999999, at.x + 1, at.y + 1, DeviceEventKind::Up))
				return false;
			return pointer(999999, x, y, DeviceEventKind::Down) && pointer(999999, x, y, DeviceEventKind::Up);
		}
		context_->ProcessMouseMove(static_cast<int>(x), static_cast<int>(y), 0);
		context_->ProcessMouseButtonDown(0, 0);
		if (pointer(999999, x, y, DeviceEventKind::Up))
			return false;
		context_->ProcessMouseButtonUp(0, 0);
		return true;
	}
	bool probe_helm(bool verify = false)
	{
		if (!heading_dial_ || !model_.layout())
			return false;
		unsigned throttle = 0;
		for (const auto &widget : model_.layout()->at("widgets"))
			if (widget.at("kind") == "throttle")
			{
				const auto binding = model_.widget_system(widget.at("id"));
				const auto track = throttle_tracks_.at(widget.at("id").get<std::string>());
				const float fraction = (++throttle % 2) ? .75f : .25f;
				const auto expected =
				    std::llround(model_.system_minimum(binding) +
				                 (model_.system_maximum(binding) - model_.system_minimum(binding)) * fraction);
				if (!verify)
				{
					pointer(998, track.x + track.width / 2, track.y + track.height * (1 - fraction),
					        DeviceEventKind::Down);
					pointer(998, track.x + track.width / 2, track.y + track.height * (1 - fraction),
					        DeviceEventKind::Up);
				}
				if (std::abs(model_.system_request(binding) - expected) > 1)
					return false;
				if (verify && widget.contains("controller"))
				{
					bool found = false;
					for (const auto &controller : model_.observation()->gameplay.at("controllers"))
						if (controller.at("id") == widget.at("controller"))
						{
							const auto &command = controller.at("commands").at(widget.at("channel").get<std::string>());
							if (std::abs(command.at("value").get<std::int64_t>() - expected) > 1 ||
							    command.at("revision") != command.at("consumed"))
								return false;
							for (const auto &consumer : command.at("consumers"))
							{
								bool physical = false;
								for (const auto &system : model_.observation()->systems)
									if (system.id == consumer.get<std::string>())
										physical = std::abs(system.requested - expected) <= 1;
								if (!physical)
									return false;
							}
							found = true;
						}
					if (!found)
						return false;
				}
			}
		if (verify)
			return throttle > 0;
		const auto [x, y, r] = *heading_dial_;
		return throttle && pointer(999, x, y - r * .85f, DeviceEventKind::Down) &&
		       pointer(999, x, y - r * .85f, DeviceEventKind::Up) && model_.requested_heading() == 90000;
	}
	bool probe_manual(unsigned stage)
	{
		if (!model_.manual_available())
			return false;
		const auto requested = [&](const std::string &field, int expected)
		{
			for (const auto &system : model_.observation()->systems)
				if (system.telemetry.contains(field) && system.telemetry.at(field) == expected)
					return true;
			std::cout << "Manual probe missing " << field << "=" << expected << "; input " << manual_input_.axes().yaw
			          << "," << manual_input_.axes().thrust << std::endl;
			for (const auto &system : model_.observation()->systems)
				if (system.telemetry.contains(field))
					std::cout << system.id << " actual request " << system.telemetry.at(field) << std::endl;
			return false;
		};
		const auto cx = joystick_area_.x + joystick_area_.width / 2;
		const auto cy = joystick_area_.y + joystick_area_.height / 2;
		if (stage == 0)
			return action(id("manual-mode"));
		if (!model_.manual_active() || joystick_area_.width <= 0)
			return false;
		if (stage == 1)
		{
			if (!pointer(9001, cx - joystick_area_.width / 4, cy - joystick_area_.height / 4, DeviceEventKind::Down) ||
			    !pointer(9001, cx - joystick_area_.width * .375f, cy - joystick_area_.height * .375f,
			             DeviceEventKind::Move))
				return false;
			manual_frame(false, {}, true);
			if (manual_sent_ != FlightAxes{500, 500})
				return false;
			return pointer(9001, cx - joystick_area_.width / 4, cy - joystick_area_.height / 4, DeviceEventKind::Move);
		}
		if (stage == 2)
			return requested("longitudinal_thrust", 500) && requested("manual_steering", 500) &&
			       pointer(9001, -100, -100, DeviceEventKind::Up);
		if (stage == 3)
		{
			if (!requested("longitudinal_thrust", 0) || !requested("manual_steering", 0) ||
			    !model_.observation()->own.vx)
				return false;
			manual_frame(true, {-1000, -1000}, true);
			return true;
		}
		if (stage == 4)
		{
			if (!requested("longitudinal_thrust", -1000) || !requested("manual_steering", -1000))
				return false;
			manual_frame(false, {}, true);
			return true;
		}
		if (stage == 5)
		{
			gamepad("smoke-pad", "flight.yaw", .2f);
			gamepad("smoke-pad", "flight.thrust", .6f);
			manual_frame(false, {}, true);
			return true;
		}
		if (stage == 6)
		{
			if (!requested("longitudinal_thrust", 600) || !requested("manual_steering", 200))
				return false;
			gamepad("smoke-pad", "flight.yaw", 0);
			gamepad("smoke-pad", "flight.thrust", 0);
			manual_frame(false, {}, true);
			return true;
		}
		return requested("longitudinal_thrust", 0) && requested("manual_steering", 0);
	}
	bool probe_engineering(unsigned stage)
	{
		std::cout << "Engineering probe stage " << stage << std::endl;
		if (!model_.observation())
			return false;
		const auto cards = engineering_cards(*model_.observation());
		if (cards.empty())
			return false;
		// Actions refresh the model; retain a value snapshot while exercising descriptors.
		const auto view = *model_.observation();
		if (stage == 0)
		{
			auto *panel = document_->GetElementById("engineering-cards");
			std::cout << "Engineering geometry: panel " << panel->GetOffsetWidth() << ", card "
			          << panel->GetChild(0)->GetOffsetWidth() << ", cards " << panel->GetNumChildren() << std::endl;
			if (panel->GetOffsetWidth() <= 0 || panel->GetChild(0)->GetOffsetWidth() > panel->GetOffsetWidth())
				return false;
			bool heat = false, battery = false;
			for (const auto &card : cards)
			{
				heat |= card.heat.has_value();
				battery |= card.battery.has_value();
			}
			const auto descriptors = view.gameplay.at("controls");
			bool profile_selected = false;
			for (const auto &d : descriptors)
				if (d.value("id", "") == "profile.detailed")
				{
					profile_selected = action(id(descriptor_action(d.at("id"))));
					std::cout << "Engineering detailed profile admission: " << profile_selected << "; "
					          << model_.announcement() << std::endl;
				}
			std::cout << "Engineering telemetry: heat=" << heat << " battery=" << battery
			          << "; profile=" << view.gameplay.value("profile", "missing") << std::endl;
			return heat && battery && profile_selected;
		}
		if (stage == 1)
		{
			std::cout << "Engineering profile: " << view.gameplay.value("profile", "missing") << std::endl;
			std::set<std::string> exercised;
			const auto descriptors = view.gameplay.at("controls");
			for (const auto &d : descriptors)
			{
				const auto operation = d.value("action", "");
				if ((operation == "generator_output" || operation == "group_allocation" ||
				     operation == "cooling_allocation") &&
				    d.value("value", std::int64_t{0}) > 0 && !exercised.contains(operation))
				{
					std::cout << "Engineering command: " << d.at("id") << std::endl;
					if (!action(id(descriptor_action(d.at("id")))))
						return false;
					exercised.insert(operation);
					const auto consumers = command_consumers(view, d);
					if (consumers.empty())
						return false;
					for (const auto &consumer : consumers)
						engineering_probe_requests_[{consumer, operation}] = d.at("value");
				}
			}
			std::cout << "Engineering operation families exercised: " << exercised.size() << std::endl;
			return exercised.size() == 3;
		}
		for (const auto &[key, value] : engineering_probe_requests_)
		{
			const auto system = std::find_if(view.systems.begin(), view.systems.end(),
			                                 [&](const auto &s)
			                                 {
				                                 return s.id == key.first;
			                                 });
			if (system == view.systems.end() || !system->telemetry.contains(key.second) ||
			    system->telemetry.at(key.second) != value)
				return false;
		}
		return !action(id("release")) && !action(id("local-settings.quit"));
	}
	void prepare(Application &app, const ConsoleRegion &region, const std::set<std::string> &pads)
	{
		app_ = &app;
		const auto preparation_started = GetTime();
		const auto previous_announcement = model_.announcement();
		model_.refresh(app.session, route_);
		app.frame_metrics["console/" + std::to_string(route_) + "/projection"] = GetTime() - preparation_started;
		const auto layout_started = GetTime();
		if (model_.announcement() != previous_announcement && !model_.announcement().empty())
			app.accessible_status = model_.announcement();
		if (owner_.lock() != app.session || (app.session && generation_ != app.session->generation()))
		{
			owner_ = app.session;
			manual_profile_.clear();
			manual_console_.clear();
			local_language_ = app.language;
			generation_ = app.session ? app.session->generation() : 0;
			settings_open_ = false;
			document_->SetClass("local-settings-open", false);
			radar_textures_.clear();
			radar_sweeps_.clear();
			signature_.clear();
			cancel_pointer();
			document_->Focus();
		}
		if (model_.observation())
		{
			const auto scope = std::to_string(model_.observation()->own.id) + "/" + model_.observation()->console;
			if (scope != manual_console_)
			{
				manual_console_ = scope;
				manual_profile_.clear();
			}
			const auto profile = model_.observation()->gameplay.value("profile", "");
			if (!profile.empty() && profile != "automatic")
				manual_profile_ = "profile." + profile;
		}
		context_->SetDimensions({static_cast<int>(region.width), static_cast<int>(region.height)});
#ifndef __EMSCRIPTEN__
		select_language(local_language_, app.pseudo_locale);
		struct RestoreLanguage
		{
			Application &app;
			~RestoreLanguage()
			{
				select_language(app.language, app.pseudo_locale);
			}
		} restore_language{app};
		property(document_, "font-size", std::to_string(text_scale_ * .2f) + "px");
		if (settings_open_ && game_active(app))
		{
			prepare_local_settings(app, region);
			return;
		}
#endif
		const bool pregame = app.session && app.session->lobby().active && !app.session->lobby().launched;
		const Json *layout = model_.layout();
		const bool helm = layout && std::any_of(layout->at("widgets").begin(), layout->at("widgets").end(),
		                                        [](const auto &widget)
		                                        {
			                                        return widget.at("kind") == "throttle";
		                                        });

		manual_toggle_ = !pregame && helm && layout && manual_preset(true) && manual_preset(false) &&
		                 std::any_of(layout->at("widgets").begin(), layout->at("widgets").end(),
		                             [](const auto &w)
		                             {
			                             return w.at("kind") == "radar";
		                             });
		const bool has_camera = layout && std::any_of(layout->at("widgets").begin(), layout->at("widgets").end(),
		                                              [](const auto &widget)
		                                              {
			                                              return widget.at("kind") == "camera_mode" ||
			                                                     widget.at("kind") == "camera_direction";
		                                              });
		document_->SetClass("helm", helm && !pregame);
		const auto cards = !pregame && !helm && model_.observation() ? engineering_cards(*model_.observation())
		                                                             : std::vector<EngineeringCard>{};
		const bool engineering = !cards.empty();
		document_->SetClass("engineering", engineering);
		std::string title = "Console", description;
		std::string complexity;
		std::vector<std::string> complexity_choices;
		bool ready = false;
		if (pregame)
		{
			const auto lobby = app.session->lobby();
			for (const auto &seat : lobby.consoles)
				if (seat.local_screen == route_)
				{
					title = seat.name;
					ready = seat.ready;
					complexity = seat.profile;
					complexity_choices = seat.profiles;
					for (const auto &member : lobby.members)
						if (member.id == seat.member && !profile_open_)
						{
							profile_name_ = member.name;
							profile_avatar_ = member.avatar;
							if (profile_avatar_.empty())
							{
								profile_avatar_ = read_file(
								    asset_path("../menu/avatar-" + std::to_string(GetRandomValue(1, 12)) + ".png"),
								    12288);
								app.session->set_local_player_profile(route_, profile_name_, profile_avatar_);
							}
						}
					for (const auto &slot : lobby.slots)
						if (slot.id == seat.slot)
							for (const auto &spec : resolve_consoles(app.session->catalog(), slot.ship))
								if (spec.id == seat.id)
									layout = &app.session->catalog().consoles.at(spec.definition);
				}
		}
		if (layout)
		{
			title = layout->value("name", layout->value("id", title));
			description =
			    layout->value("description",
			                  std::string("Operate the fitted systems and monitor this Console's permitted contacts."));
		}
		// Authored widgets still determine controls, placement and authority.
		std::string signature = (layout ? layout->dump() : "vacant") + (app.pseudo_locale ? "pseudo" : "normal") +
		                        (pregame ? ready ? "ready" : "waiting" : "running") + (profile_open_ ? "profile" : "") +
		                        (profile_open_ ? "" : profile_name_) + "/complexity/" + complexity +
		                        (manual_toggle_ ? "/manual" : "");
		for (const auto &choice : complexity_choices)
			signature += "/" + choice;
		if (model_.observation())
			signature += "/" + std::to_string(model_.observation()->own.id) + "/" + model_.observation()->console;
		for (const auto &pad : pads)
			signature += "/pad/" + pad;
		if (signature != signature_)
		{
			signature_ = std::move(signature);
			placement_signature_.clear();
			properties_.clear();
			elements_.clear();
			gameplay_signature_.clear();
			gameplay_readout_signature_.clear();
			engineering_signature_.clear();
			displayed_values_.clear();
			controls_.clear();
			heading_contacts_.clear();
			std::string toolbar = "<h1>" + Rml::StringUtilities::EncodeRml(localise(title)) + "</h1>";
			if (toolbar_enabled_ && !pregame)
			{
				button("settings", "Settings");
				toolbar += "<button class=\"settings-cog\" id=\"" + id("settings") + "\"><img src=\"" +
				           asset_path("../menu/settings.png") + "\" width=\"56\" height=\"56\"/></button>";
			}
			for (const auto &pad : toolbar_enabled_ && !pregame ? pads : std::set<std::string>{})
				toolbar += button("pad/" + pad, "Assign controller " + pad);
			toolbar += "<p id=\"connection-status\"></p>";
			document_->GetElementById("toolbar")->SetInnerRML(toolbar);
			std::string widgets;
			bool has_radar = false;
			bool has_manual_toggle = false;
			int heading_count = 0;
			if (layout && !pregame)
				for (const auto &widget : layout->at("widgets"))
				{
					if (widget.at("kind") == "radar")
						has_radar = true;
					if (widget.at("kind") == "heading")
						++heading_count;
				}
			if (layout && !pregame)
				for (const auto &widget : layout->at("widgets"))
				{
					const auto key = widget.at("id").get<std::string>(), kind = widget.at("kind").get<std::string>();
					auto percent = [&](const char *field)
					{
						return std::to_string(widget.at(field).get<double>() / 10) + "%";
					};
					widgets += "<div class=\"widget " + kind + "-widget\" id=\"widget/" +
					           Rml::StringUtilities::EncodeRml(key) + "\" style=\"left:" + percent("x") +
					           ";top:" + percent("y") + ";width:" + percent("width") + ";height:" + percent("height") +
					           "\"><h2>" + Rml::StringUtilities::EncodeRml(localise(widget.at("label"))) + "</h2>";
					if (kind == "camera_mode" || kind == "camera_direction")
					{
						widgets += "<div class=\"camera-choices\" id=\"camera-panel/" +
						           Rml::StringUtilities::EncodeRml(key) + "\"><img class=\"camera-art\" src=\"" +
						           asset_path(kind == "camera_mode" ? "camera-mode.png" : "camera-direction.png") +
						           "\"/>";
						static constexpr const char *arrows[] = {"↑", "↗", "→", "↘", "↓", "↙", "←", "↖"};
						for (int value = 0; value < (kind == "camera_mode" ? 2 : 8); ++value)
						{
							widgets += button("camera/" + key + "/" + std::to_string(value),
							                  kind == "camera_mode" ? (value ? "Third person" : "First person")
							                                        : camera_directions[value],
							                  kind == "camera_mode" ? (value ? "3P" : "1P") : arrows[value]);
						}
						widgets += "</div><p class=\"camera-status\" id=\"" +
						           Rml::StringUtilities::EncodeRml(id("camera-status/" + key)) + "\"/>";
					}
					if (kind == "throttle")
						widgets += "<input class=\"throttle\" id=\"" + id("speed/" + key) +
						           "\" type=\"range\" orientation=\"vertical\" min=\"-100\" max=\"0\" step=\"0.1\" "
						           "value=\"0\"" +
						           (pregame ? " disabled" : "") + "/>";
					if (kind == "heading")
					{
						widgets += "<label for=\"" + id("heading/" + key) + "\">" +
						           Rml::StringUtilities::EncodeRml(localise("Desired heading (degrees)")) +
						           "</label><input class=\"heading-value\" id=\"" + id("heading/" + key) +
						           "\" type=\"text\" maxlength=\"7\" value=\"0\"" + (pregame ? " disabled" : "") +
						           "/><p>" +
						           Rml::StringUtilities::EncodeRml(
						               localise("Click or drag the radar ring. East 0; North 90.")) +
						           "</p>";
						if (!has_radar || heading_count > 1)
							widgets += "<div class=\"dial\" id=\"dial/" + Rml::StringUtilities::EncodeRml(key) + "\"/>";
					}
					if (kind == "radar")
						widgets += "<div class=\"dial\" id=\"dial/" + Rml::StringUtilities::EncodeRml(key) + "\"/>" +
						           button("game.contacts/" + key, "Read radar contacts") +
						           button("on-screen/" + key, "On screen");
					if (kind == "radar" && manual_toggle_ && !has_manual_toggle)
					{
						has_manual_toggle = true;
						controls_.emplace_back(id("manual"), "Manual");
						widgets += "<button class=\"manual-toggle\" id=\"" + id("manual") +
						           "\"><img class=\"manual-art\" src=\"" + asset_path("manual-toggle.png") +
						           "\"/><span class=\"manual-label\">" +
						           Rml::StringUtilities::EncodeRml(localise("Manual")) +
						           "</span><span class=\"manual-lamp\" id=\"" + id("manual.lamp") +
						           "\"/><span class=\"manual-state\" id=\"" + id("manual.state") + "\"/></button>";
					}
					widgets += "<p class=\"" + std::string(kind == "throttle" ? "readout" : "heading-actual") +
					           "\" id=\"value/" + Rml::StringUtilities::EncodeRml(key) + "\"/></div>";
				}
			document_->GetElementById("widgets")->SetInnerRML(pregame ? "" : widgets);
			document_->GetElementById("toolbar")->SetProperty("display", pregame ? "none" : "block");
			std::string lobby;
			if (pregame)
				lobby = "<div class=\"ready-card\"><h1 id=\"" + id("lobby-title") + "\">" +
				        Rml::StringUtilities::EncodeRml(localise(title)) + "</h1><p>" +
				        Rml::StringUtilities::EncodeRml(localise(description)) + "</p><p>" +
				        Rml::StringUtilities::EncodeRml(
				            localise(ready ? "Ready. Waiting for the crew." : "Check your Console, then ready up.")) +
				        "</p>" + button("ready", ready ? "Not Ready" : "Ready") + "<p id=\"countdown\"/></div>";
			if (pregame)
			{
				if (!complexity_choices.empty())
				{
					lobby += "<div class=\"ready-card\"><p>" + localise("Complexity") + ": " +
					         Rml::StringUtilities::EncodeRml(complexity) + "</p>";
					for (const auto &choice : complexity_choices)
						lobby += button("complexity/" + choice, choice);
					lobby += "</div>";
				}
				button("profile", "Edit profile");
				const float tw = terminal_.value("display_width", 264.f), sw = terminal_.at("size").at(0),
				            sh = terminal_.at("size").at(1);
				const auto &aperture = terminal_.at("screen");
				const float ax = aperture.at("x"), ay = aperture.at("y"), aw = aperture.at("width"),
				            ah = aperture.at("height");
				lobby += "<button id=\"" + id("profile") +
				         "\" class=\"profile-tv\" style=\"width:" + std::to_string(tw) +
				         "px;height:" + std::to_string(tw * sh / sw) + "px\"><img src=\"" +
				         asset_path("../menu/crt-terminal.png") + "\" width=\"" + std::to_string(tw) +
				         "\"/><span style=\"left:" + std::to_string(ax / sw * 100) +
				         "%;top:" + std::to_string((ay + ah * .85f) / sh * 100) +
				         "%;width:" + std::to_string(aw / sw * 100) +
				         "%;height:" + std::to_string(ah * .15f / sh * 100) + "%;background:#101918;font-size:16px\">" +
				         Rml::StringUtilities::EncodeRml(profile_name_) + "</span></button>";
				if (profile_open_)
				{
					controls_.clear();
					lobby = "<div class=\"ready-card profile-card\"><h1>" +
					        Rml::StringUtilities::EncodeRml(localise("Profile")) + "</h1><label>" +
					        Rml::StringUtilities::EncodeRml(localise("Player name")) + "</label><input id=\"" +
					        id("profile-name") + "\" type=\"text\" maxlength=\"64\" value=\"" +
					        Rml::StringUtilities::EncodeRml(profile_name_) + "\"/><div>";
					for (int i = 1; i <= 12; ++i)
					{
						button("avatar/" + std::to_string(i), "Avatar " + std::to_string(i));
						lobby += "<button class=\"avatar-choice\" id=\"" + id("avatar/" + std::to_string(i)) +
						         "\"><img src=\"" + asset_path("../menu/avatar-" + std::to_string(i) + ".png") +
						         "\" width=\"48\" height=\"48\"/></button>";
					}
					lobby += "</div>" + button("profile-save", "Use this profile") + "</div>";
				}
			}
			document_->GetElementById("lobby")->SetInnerRML(lobby);
		}
		if (pregame && profile_open_)
			app.screen_inputs.push_back({id("profile-name"), localise("Player name"), profile_name_});
		if (pregame)
			app.screen_readouts.emplace_back(id("lobby-title"), localise(title) + ". " + localise(description));
		if (auto *status = document_->GetElementById("connection-status"))
		{
			const auto text = localise(pregame ? description : model_.announcement());
			if (displayed_values_["connection-status"] != text)
			{
				status->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
				displayed_values_["connection-status"] = text;
			}
		}
		if (pregame)
			if (auto *countdown = document_->GetElementById("countdown"))
			{
				const auto left = app.session->lobby().countdown_ms;
				countdown->SetInnerRML(left < 0 ? ""
				                                : Rml::StringUtilities::EncodeRml(localise("Starting in")) + " " +
				                                      std::to_string((left + 999) / 1000));
			}
		const auto readouts_started = GetTime();
		auto gameplay_controls =
		    pregame ? std::vector<std::pair<std::string, std::string>>{} : model_.gameplay_controls();
		if (model_.manual_available())
		{
			std::set<std::string> hidden;
			for (const auto &d : model_.observation()->gameplay.value("controls", Json::array()))
			{
				const auto a = d.value("action", "");
				if (a == "assisted" || a == "manual_steering" || a == "longitudinal_thrust" ||
				    (model_.manual_active() && (a == "speed_mm_s" || a == "heading_mdeg" || a == "destination" ||
				                                a == "follow_contact" || a == "match_speed")))
					hidden.insert(descriptor_action(d.at("id")));
			}
			std::erase_if(gameplay_controls,
			              [&](const auto &c)
			              {
				              return hidden.contains(c.first);
			              });
		}
		std::erase_if(gameplay_controls,
		              [](const auto &control)
		              {
			              // ConsoleModel encodes descriptor IDs as hexadecimal.
			              return control.first == "game.control/61776179";
		              });
		if (model_.observation())
		{
			std::set<std::string> camera_controls;
			for (const auto &d : model_.observation()->gameplay.value("controls", Json::array()))
				if (d.value("action", "") == "camera_mode" || d.value("action", "") == "camera_direction")
					camera_controls.insert(descriptor_action(d.at("id")));
			std::erase_if(gameplay_controls,
			              [&](const auto &control)
			              {
				              return camera_controls.contains(control.first);
			              });
		}
		if (helm && model_.observation())
		{
			std::set<std::string> dial_controls;
			for (const auto &d : model_.observation()->gameplay.value("controls", Json::array()))
				if (d.value("action", "") == "heading_mdeg" || d.value("action", "") == "speed_mm_s")
					for (const auto &widget : layout->at("widgets"))
						if ((widget.at("kind") == "throttle" && d.at("action") == "speed_mm_s") ||
						    (widget.at("kind") == "heading" && d.at("action") == "heading_mdeg"))
						{
							const auto target = d.value("controller", std::string{}).empty()
							                        ? d.value("system", std::string{})
							                        : "controller/" + d.at("controller").get<std::string>() + "/" +
							                              d.at("channel").get<std::string>();
							if (model_.widget_system(widget.at("id")) == target)
								dial_controls.insert(descriptor_action(d.at("id")));
						}
			// The dial and throttle already expose these actions, including numeric
			// accessibility inputs. Their changing +/- values must not rebuild the panel.
			std::erase_if(gameplay_controls,
			              [&](const auto &control)
			              {
				              return dial_controls.contains(control.first);
			              });
		}
		if (manual_toggle_)
		{
			std::set<std::string> presets;
			for (const auto &d : model_.observation()->gameplay.value("controls", Json::array()))
				if (d.value("action", "") == "preset")
					presets.insert(descriptor_action(d.at("id")));
			std::erase_if(gameplay_controls,
			              [&](const auto &control)
			              {
				              return presets.contains(control.first);
			              });
		}
		if (engineering && model_.observation())
		{
			std::set<std::string> messages;
			for (const auto &d : model_.observation()->gameplay.value("controls", Json::array()))
				if (d.value("action", "").starts_with("message."))
					messages.insert(descriptor_action(d.at("id")));
			std::erase_if(gameplay_controls,
			              [&](const auto &control)
			              {
				              return messages.contains(control.first);
			              });
		}
		readout_values_ = pregame ? std::vector<std::pair<std::string, std::string>>{} : model_.gameplay_readouts();
		if (engineering)
			std::erase_if(readout_values_,
			              [](const auto &readout)
			              {
				              return readout.first.starts_with("game.readout/system-");
			              });
		const auto &gameplay_readouts = readout_values_;
		std::set<std::string> engineering_controls;
		property(document_->GetElementById("engineering-cards"), "width",
		         std::to_string(region.width * .92f - 24) + "px");
		std::string engineering_markup;
		for (const auto &card : cards)
		{
			const int columns = region.width >= 900 ? 2 : 1;
			const float card_width = (region.width * .92f - 40) / columns - 12;
			engineering_markup += "<div class=\"engineering-card\" style=\"width:" + std::to_string(card_width) +
			                      "px\"><h2>" + Rml::StringUtilities::EncodeRml(localise(card.title)) +
			                      "</h2><p id=\"" + id("resource/" + card.id) + "\"></p>";
			for (const auto &[name, value] : std::vector<std::pair<std::string, std::optional<int>>>{
			         {"heat", card.heat}, {"battery", card.battery}})
				if (value)
					engineering_markup += "<div class=\"resource-meter\"><div class=\"resource-fill resource-" + name +
					                      "\" id=\"" + id("gauge/" + card.id + "/" + name) + "\"/></div>";
			for (const auto &[name, label] : card.controls)
			{
				engineering_controls.insert(name);
				for (auto &control : gameplay_controls)
					if (control.first == name)
						control.second = localise(card.title) + ": " + label;
				engineering_markup +=
				    "<button id=\"" + id(name) + "\">" + Rml::StringUtilities::EncodeRml(label) + "</button>";
			}
			engineering_markup += "</div>";
		}
		if (engineering_markup != engineering_signature_)
		{
			elements_.clear();
			document_->GetElementById("engineering-cards")->SetInnerRML(engineering_markup);
			engineering_signature_ = engineering_markup;
			properties_.clear();
			for (const auto &card : cards)
				displayed_values_.erase("resource/" + card.id);
		}
		for (const auto &card : cards)
		{
			const auto name = "resource/" + card.id;
			if (displayed_values_[name] != card.summary)
			{
				element(id(name))->SetInnerRML(Rml::StringUtilities::EncodeRml(card.summary));
				displayed_values_[name] = card.summary;
			}
			app.screen_readouts.emplace_back(id(name), localise(card.title) + ". " + card.summary);
			for (const auto &[metric, value] : std::vector<std::pair<std::string, std::optional<int>>>{
			         {"heat", card.heat}, {"battery", card.battery}})
				if (value)
					property(element(id("gauge/" + card.id + "/" + metric)), "width", std::to_string(*value) + "%");
		}
		app.frame_metrics["console/" + std::to_string(route_) + "/readouts"] = GetTime() - readouts_started;
		auto *gameplay = document_->GetElementById("gameplay");
		auto *widgets = document_->GetElementById("widgets");
		const bool has_gameplay = !gameplay_controls.empty() || !gameplay_readouts.empty();
		const auto placement = signature_ + "/" + std::to_string(region.width) + "/" + std::to_string(region.height) +
		                       (has_gameplay ? "/gameplay" : "/empty");
		const bool placement_changed = placement_signature_ != placement;
		if (placement_changed)
		{
			placement_signature_ = placement;
			gameplay->SetProperty("display", has_gameplay ? "block" : "none");
			widgets->SetProperty("right", "4%");
			widgets->SetProperty("bottom", "4%");
			gameplay->SetProperty("left", "4%");
			gameplay->SetProperty("right", "4%");
			gameplay->SetProperty("top", "125px");
			gameplay->SetProperty("bottom", "4%");
			gameplay->RemoveProperty("width");
			gameplay->SetProperty("height", std::to_string(std::max(80.f, region.height * .96f - 125)) + "px");
			bool placed_gameplay = false;
			if (has_gameplay && layout && layout->contains("gameplay_panel") &&
			    layout->at("gameplay_panel").is_object())
			{
				const auto &panel = layout->at("gameplay_panel");
				if (panel.contains("x") && panel.contains("y") && panel.contains("width") && panel.contains("height") &&
				    panel.at("x").is_number() && panel.at("y").is_number() && panel.at("width").is_number() &&
				    panel.at("height").is_number())
				{
					const double x = panel.at("x"), y = panel.at("y"), width = panel.at("width"),
					             height = panel.at("height");
					if (std::isfinite(x) && std::isfinite(y) && std::isfinite(width) && std::isfinite(height) &&
					    x >= 0 && y >= 0 && width > 0 && height > 0 && x + width <= 1 && y + height <= 1)
					{
						const auto available_height = std::max(80.f, region.height - 125.f);
						gameplay->SetProperty("left", std::to_string(region.width * x) + "px");
						gameplay->SetProperty("top", std::to_string(125 + available_height * y) + "px");
						gameplay->SetProperty("width", std::to_string(region.width * width) + "px");
						gameplay->SetProperty("height", std::to_string(available_height * height) + "px");
						gameplay->RemoveProperty("right");
						gameplay->RemoveProperty("bottom");
						placed_gameplay = true;
					}
				}
			}
			if (has_gameplay && !placed_gameplay && layout && !layout->at("widgets").empty())
			{
				if (region.width >= 900)
				{
					widgets->SetProperty("right", "42%");
					gameplay->SetProperty("left", "60%");
				}
				else
				{
					widgets->SetProperty("bottom", "45%");
					gameplay->SetProperty("top", "58%");
				}
			}
			// The compact helm preset has one throttle. Multiple independent controls
			// must retain their authored geometry instead of occupying the same slot.
			if (helm && !pregame && !placed_gameplay &&
			    std::count_if(layout->at("widgets").begin(), layout->at("widgets").end(),
			                  [](const auto &widget)
			                  {
				                  return widget.at("kind") == "throttle";
			                  }) == 1)
			{
				widgets->SetProperty("top", "4%");
				widgets->SetProperty("right", "4%");
				widgets->SetProperty("bottom", "4%");
				const float width = region.width * .92f, height = region.height * .92f;
				const float side = std::min(height, width * (has_camera ? .60f : .70f));
				for (const auto &widget : layout->at("widgets"))
				{
					auto *element = document_->GetElementById("widget/" + widget.at("id").get<std::string>());
					const std::string kind = widget.at("kind");
					if (kind == "radar")
					{
						element->SetProperty(
						    "left",
						    std::to_string(has_camera ? width * .24f + (width * .61f - side) / 2 : (width - side) / 2) +
						        "px");
						element->SetProperty("top", std::to_string((height - side) / 2) + "px");
						element->SetProperty("width", std::to_string(side) + "px");
						element->SetProperty("height", std::to_string(side) + "px");
					}
					else if (kind == "throttle")
					{
						element->SetProperty("left", "85%");
						element->SetProperty("top", "10%");
						element->SetProperty("width", "15%");
						element->SetProperty("height", "80%");
					}
					else if (kind == "camera_mode" || kind == "camera_direction")
					{
						element->SetProperty("left", "0%");
						element->SetProperty("top", kind == "camera_mode" ? "8%" : "30%");
						element->SetProperty("width", "22%");
						element->SetProperty("height", kind == "camera_mode" ? "22%" : "45%");
					}
					else if (kind == "heading")
					{
						element->SetProperty("left", "0%");
						element->SetProperty("top", "85%");
						element->SetProperty("width", "15%");
						element->SetProperty("height", "15%");
					}
				}
				gameplay->SetProperty("left", "4%");
				gameplay->SetProperty("top", has_camera ? "74%" : "18%");
				gameplay->SetProperty("width", has_camera ? "22%" : "12%");
				gameplay->SetProperty("height", has_camera ? "16%" : "60%");
				gameplay->RemoveProperty("right");
				gameplay->RemoveProperty("bottom");
			}
			if (engineering)
			{
				gameplay->SetProperty("left", "4%");
				gameplay->SetProperty("top", "100px");
				gameplay->SetProperty("width", std::to_string(region.width * .92f) + "px");
				gameplay->SetProperty("height", std::to_string(std::max(80.f, region.height * .96f - 100)) + "px");
				gameplay->RemoveProperty("right");
				gameplay->RemoveProperty("bottom");
			}
		}
		Json gameplay_identity = Json::array();
		for (const auto &[name, label] : gameplay_controls)
			if (!engineering_controls.contains(name))
				gameplay_identity.push_back(name);
		const auto gameplay_signature = gameplay_identity.dump();
		if (gameplay_signature_ != gameplay_signature)
		{
			elements_.clear();
			for (const auto &held : held_clicks_)
				model_.release_control(held.substr(id("").size()));
			held_clicks_.clear();
			std::string markup;
			for (const auto &[name, label] : gameplay_controls)
				if (!engineering_controls.contains(name))
					markup += "<button id=\"" + Rml::StringUtilities::EncodeRml(id(name)) + "\">" +
					          Rml::StringUtilities::EncodeRml(label) + "</button>";
			document_->GetElementById("gameplay-controls")->SetInnerRML(markup);
			gameplay_signature_ = gameplay_signature;
			for (const auto &[name, label] : gameplay_controls)
				displayed_values_.erase("control/" + name);
		}
		for (const auto &[name, label] : gameplay_controls)
			if (!engineering_controls.contains(name) && displayed_values_["control/" + name] != label)
			{
				if (auto *element = this->element(id(name)))
					element->SetInnerRML(Rml::StringUtilities::EncodeRml(label));
				displayed_values_["control/" + name] = label;
			}
		gameplay_identity = Json::array();
		for (const auto &[name, text] : gameplay_readouts)
			gameplay_identity.push_back(name);
		const auto readout_signature = gameplay_identity.dump();
		if (gameplay_readout_signature_ != readout_signature)
		{
			elements_.clear();
			std::string markup;
			for (const auto &[name, text] : gameplay_readouts)
			{
				markup += "<p id=\"" + Rml::StringUtilities::EncodeRml(id(name)) + "\"></p>";
				displayed_values_.erase(name);
			}
			document_->GetElementById("gameplay-readouts")->SetInnerRML(markup);
			gameplay_readout_signature_ = readout_signature;
		}
		for (const auto &[name, text] : gameplay_readouts)
		{
			update_readout(name, text, placement_changed);
			app.screen_readouts.emplace_back(id(name), text);
		}
		{
			const auto update_started = GetTime();
			context_->Update();
			app.frame_metrics["console/" + std::to_string(route_) + "/before-widgets"] = GetTime() - update_started;
		}
		heading_dial_.reset();
		heading_dials_.clear();
		dial_systems_.clear();
		throttle_tracks_.clear();
		std::string shared_heading;
		int heading_count = 0;
		if (layout && !pregame)
			for (const auto &widget : layout->at("widgets"))
				if (widget.at("kind") == "heading")
				{
					shared_heading = model_.widget_system(widget.at("id").get<std::string>());
					++heading_count;
				}
		if (layout && !pregame)
			for (const auto &widget : layout->at("widgets"))
			{
				const auto key = widget.at("id").get<std::string>(), kind = widget.at("kind").get<std::string>();
				if (kind == "radar" || kind == "heading")
					if (auto *dial = document_->GetElementById("dial/" + key))
					{
						const auto at = dial->GetAbsoluteOffset(Rml::BoxArea::Border);
						const float radius =
						    std::max(1.f, std::min(dial->GetOffsetWidth(), dial->GetOffsetHeight()) * .5f);
						const auto system = kind == "heading"    ? model_.widget_system(key)
						                    : heading_count == 1 ? shared_heading
						                                         : std::string{};
						if (!system.empty())
						{
							heading_dials_[key] = {at.x + dial->GetOffsetWidth() / 2,
							                       at.y + dial->GetOffsetHeight() / 2, radius};
							dial_systems_[key] = system;
							if (!heading_dial_)
								heading_dial_ = heading_dials_.at(key);
						}
					}
				if (kind == "throttle")
				{
					const auto track = throttle_rect(key);
					const auto &markers = hud_metadata_.at("helm-throttle-track").at("markers");
					const float top = markers.at("travel_max")[1].get<float>() /
					                  hud_metadata_.at("helm-throttle-track").at("image_size")[1].get<float>(),
					            bottom = markers.at("travel_min")[1].get<float>() /
					                     hud_metadata_.at("helm-throttle-track").at("image_size")[1].get<float>();
					auto *slider =
					    dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(id("speed/" + key)));
					property(slider, "left", std::to_string(track.x - widget_offset(key).x - track.width * .1f) + "px");
					property(slider, "top",
					         std::to_string(track.y - widget_offset(key).y + track.height * top - track.width * .2f) +
					             "px");
					property(slider, "width", std::to_string(track.width * 1.2f) + "px");
					property(slider, "height",
					         std::to_string(track.height * (bottom - top) + track.width * .4f) + "px");
					const auto system = model_.widget_system(key);
					const auto minimum = model_.system_minimum(system), maximum = model_.system_maximum(system);
					const double percent =
					    maximum > minimum ? 100.0 * (model_.system_request(system) - minimum) / (maximum - minimum) : 0;
					throttle_tracks_[key] = {track.x, track.y + track.height * top, track.width,
					                         track.height * (bottom - top)};
					// Programmatic projection updates must never enqueue a fresh player command.
					syncing_values_ = true;
					if (slider->GetValue() != std::to_string(-percent))
						slider->SetValue(std::to_string(-percent));
					syncing_values_ = false;
					Rml::ElementList bars;
					slider->GetElementsByTagName(bars, "sliderbar");
					for (auto *bar : bars)
						property(bar, "height", std::to_string(track.width * .4f) + "px");
					if (!pregame && !model_.manual_active())
						app.screen_inputs.push_back(
						    {id("speed/" + key), "Desired speed (%)", std::to_string(percent), true});
				}
				if (kind == "heading" && !pregame && !model_.manual_active())
				{
					auto *field =
					    dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(id("heading/" + key)));
					const auto requested = std::string(
					    TextFormat("%.1f", model_.system_request(model_.widget_system(key), "heading_mdeg") / 1000.0));
					if (context_->GetFocusElement() != field && field->GetValue() != requested)
					{
						syncing_values_ = true;
						field->SetValue(requested);
						syncing_values_ = false;
					}
					app.screen_inputs.push_back(
					    {id("heading/" + key), "Desired heading (degrees)",
					     std::string(TextFormat(
					         "%.1f", model_.system_request(model_.widget_system(key), "heading_mdeg") / 1000.0)),
					     false});
				}
				if (kind == "camera_mode" || kind == "camera_direction")
				{
					const auto camera = model_.camera();
					const auto selected = kind == "camera_mode" ? camera.mode : camera.direction;
					auto *housing = document_->GetElementById("widget/" + key);
					const float aspect = kind == "camera_mode" ? 2.5f : 1.f;
					const float width = std::max(
					    1.f, std::min(housing->GetOffsetWidth() - 16, (housing->GetOffsetHeight() - 50) * aspect));
					const float height = width / aspect;
					if (auto *panel = document_->GetElementById("camera-panel/" + key))
					{
						property(panel, "left", std::to_string((housing->GetOffsetWidth() - width) / 2) + "px");
						property(panel, "width", std::to_string(width) + "px");
						property(panel, "height", std::to_string(height) + "px");
					}
					if (auto *status = document_->GetElementById(id("camera-status/" + key)))
					{
						property(status, "top", std::to_string(height + 30) + "px");
						update_readout("camera-status/" + key,
						               localise(kind == "camera_mode" ? (selected ? "Third person" : "First person")
						                                              : camera_directions[selected]),
						               true);
					}
					static constexpr float centers[8][2] = {{.5f, .19f}, {.73f, .27f}, {.81f, .5f}, {.73f, .73f},
					                                        {.5f, .81f}, {.27f, .73f}, {.19f, .5f}, {.27f, .27f}};
					for (int i = 0; i < (kind == "camera_mode" ? 2 : 8); ++i)
						if (auto *choice = document_->GetElementById(id("camera/" + key + "/" + std::to_string(i))))
						{
							choice->SetClass("selected", i == selected);
							property(choice, "left",
							         std::to_string(kind == "camera_mode" ? (i ? 51.f : 16.f)
							                                              : (centers[i][0] - .11f) * 100) +
							             "%");
							property(choice, "top",
							         kind == "camera_mode" ? "25%"
							                               : std::to_string((centers[i][1] - .11f) * 100) + "%");
							property(choice, "width", kind == "camera_mode" ? "32%" : "22%");
							property(choice, "height", kind == "camera_mode" ? "50%" : "22%");
						}
				}
				std::string value =
				    kind == "throttle" ? "Requested " +
				                             std::string(TextFormat(
				                                 "%.1f", model_.system_request(model_.widget_system(key)) / 1000.0)) +
				                             " m/s"
				    : kind == "heading"
				        ? "Actual " +
				              std::to_string(model_.observation() ? model_.observation()->own.heading / 1000 : 0) +
				              " degrees"
				        : "";
				value = localise(value);
				if (auto *readout = document_->GetElementById("value/" + key);
				    readout && displayed_values_["value/" + key] != value)
				{
					readout->SetInnerRML(Rml::StringUtilities::EncodeRml(value));
					displayed_values_["value/" + key] = value;
				}
			}
		sharing_pods_.clear();
		if (helm && !pregame && layout)
			for (const auto &w : layout->at("widgets"))
				if (w.at("kind") == "radar")
				{
					const auto key = w.at("id").get<std::string>();
					auto *dial = document_->GetElementById("dial/" + key);
					auto *control = document_->GetElementById(id("on-screen/" + key));
					if (!dial || !control)
						continue;
					const auto at = dial->GetAbsoluteOffset(Rml::BoxArea::Border);
					const auto parent = widget_offset(key);
					const float r = std::min(dial->GetOffsetWidth(), dial->GetOffsetHeight()) * .5f;
					const float cx = at.x + dial->GetOffsetWidth() / 2, cy = at.y + dial->GetOffsetHeight() / 2;
					const Rectangle pod{cx + r * .34f, cy + r * 1.02f, std::clamp(r * .70f, 196.f, 240.f), 76.f};
					sharing_pods_[key] = pod;
					property(control, "left", std::to_string(pod.x - parent.x) + "px");
					property(control, "top", std::to_string(pod.y - parent.y) + "px");
					property(control, "width", std::to_string(pod.width) + "px");
					property(control, "height", std::to_string(pod.height) + "px");
				}
		{
			const auto update_started = GetTime();
			context_->Update();
			app.frame_metrics["console/" + std::to_string(route_) + "/after-widgets"] = GetTime() - update_started;
		}
		for (const auto &[name, label] : controls_)
		{
			if (name == id("manual"))
			{
				const bool manual = model_.observation()->gameplay.value("profile", "") != "automatic";
				app.screen_toggles[name] = manual;
				if (auto *toggle = element(name))
				{
					toggle->SetClass("manual-on", manual);
					toggle->SetAttribute("aria-pressed", manual ? "true" : "false");
					// Keep the single switch inside the radar bezel at smaller viewport sizes.
					const float width = std::clamp(toggle->GetParentNode()->GetOffsetWidth() * .34f, 160.f, 220.f);
					property(toggle, "width", std::to_string(width) + "px");
					property(toggle, "height", std::to_string(width / 2.68f) + "px");
				}
				update_readout("manual.lamp", manual ? "+" : "-", true);
				update_readout("manual.state", localise(manual ? "On" : "Off"), true);
				app.screen_controls.emplace_back(name, localise("Manual") + ": " + localise(manual ? "On" : "Off"));
			}
			else if (name.starts_with(id("on-screen/")))
			{
				const auto widget = name.substr(id("on-screen/").size());
				const bool active = model_.is_on_screen(widget);
				app.screen_toggles[name] = active;
				if (auto *b = document_->GetElementById(name))
				{
					b->SetClass("on-screen", true);
					b->SetClass("on-screen-active", active);
					b->SetAttribute("aria-pressed", active ? "true" : "false");
				}
				app.screen_controls.emplace_back(name, localise("On screen") + ": " + localise(active ? "On" : "Off"));
			}
			else
			{
				const auto *choice = element(name);
				const bool selected =
				    name.find("/camera/") != std::string::npos && choice && choice->IsClassSet("selected");
				app.screen_controls.emplace_back(name, selected ? localise(label) + ". " + localise("Selected")
				                                                : localise(label));
			}
			if (name == id("manual"))
				context_->Update();
			place(app, region, name);
		}
		for (const auto &[name, label] : gameplay_controls)
		{
			app.screen_controls.emplace_back(id(name), label);
			place(app, region, id(name));
		}
		for (const auto &[name, text] : gameplay_readouts)
			place(app, region, id(name));
		for (const auto &card : cards)
			place(app, region, id("resource/" + card.id));
		for (const auto &field : app.screen_inputs)
			if (field.id.starts_with(id("")))
				place(app, region, field.id);
		if (pregame)
			place(app, region, id("lobby-title"));
		prepare_navigation(app, region, pregame);
		prepare_manual(app, region, pregame);
		if (!pregame && model_.observation())
		{
			const auto publication = app.session->public_board().value("display", Json(nullptr));
			const auto source = publication.is_object()
			                        ? publication.value("console", "") + "/" + publication.value("widget", "")
			                        : std::string{};
			if (source != display_announcement_)
			{
				display_announcement_ = source;
				app.accessible_status =
				    source.empty() ? localise("Normal viewscreen") : localise("On screen") + ": " + source;
			}
			if (publication.is_object() && publication.at("kind") == "navigation" &&
			    model_.is_on_screen(publication.at("widget")) && display_update_tick_ != app.session->ticks())
			{
				const auto camera = display_camera(publication.at("widget"));
				if (Json(camera) != publication.at("camera"))
				{
					model_.on_screen(publication.at("widget"), camera, true);
					display_update_tick_ = app.session->ticks();
				}
			}
		}
		app.frame_metrics["console/" + std::to_string(route_) + "/layout"] = GetTime() - layout_started;
	}
	void draw_profile(const ConsoleRegion &region)
	{
		if (app_->session->lobby().active && !app_->session->lobby().launched && !profile_open_ &&
		    !profile_avatar_.empty())
		{
			const auto key = sha256(profile_avatar_);
			const auto &portrait = crew_portraits_.contains(key) ? crew_portraits_.at(key) : profile_avatar_;
			const auto texture = radar_textures_.load(key + "/portrait", portrait);
			const float tw = terminal_.value("display_width", 264.f), sw = terminal_.at("size").at(0), scale = tw / sw;
			const auto &screen = terminal_.at("screen");
			const float x = screen.at("x"), y = screen.at("y"), w = screen.at("width"), h = screen.at("height");
			const Rectangle area{region.x + region.width - 20 - tw + x * scale, region.y + y * scale, w * scale,
			                     h * .85f * scale};
			const float fill = std::max(area.width / texture.width, area.height / texture.height);
			DrawTexturePro(texture,
			               {(texture.width - area.width / fill) / 2, (texture.height - area.height / fill) / 2,
			                area.width / fill, area.height / fill},
			               area, {0, 0}, 0, WHITE);
		}
	}

	void draw(const ConsoleRegion &region)
	{
		const Rectangle entire{region.x, region.y, region.width, region.height};
		BeginScissorMode(static_cast<int>(region.x), static_cast<int>(region.y), static_cast<int>(region.width),
		                 static_cast<int>(region.height));
		DrawRectangleRec(entire, {8, 13, 15, 255});
		const auto background = hud("helm-background-9slice");
		const auto &slices = hud_metadata_.at("helm-background-9slice").at("nine_slice");
		DrawTextureNPatch(background,
		                  {{0, 0, static_cast<float>(background.width), static_cast<float>(background.height)},
		                   slices.at("left").get<int>(),
		                   slices.at("top").get<int>(),
		                   slices.at("right").get<int>(),
		                   slices.at("bottom").get<int>(),
		                   NPATCH_NINE_PATCH},
		                  entire, {0, 0}, 0, WHITE);

		const auto &view = model_.observation();
		const float ox = region.x, oy = region.y;
		if (auto *widgets = document_->GetElementById("widgets"))
			for (int i = 0; i < widgets->GetNumChildren(); ++i)
			{
				auto *widget = widgets->GetChild(i);
				const auto key = widget->GetId().substr(7);
				if (auto *dial = document_->GetElementById("dial/" + key))
				{
					const auto at = dial->GetAbsoluteOffset(Rml::BoxArea::Border);
					const float radius = std::min(dial->GetOffsetWidth(), dial->GetOffsetHeight()) * .5f;
					const Vector2 centre{ox + at.x + dial->GetOffsetWidth() / 2,
					                     oy + at.y + dial->GetOffsetHeight() / 2};
					if (view && view->feeds & system_bit(OperatingSystem::Radar))
					{
						const auto radar =
						    select_system(view->systems, OperatingSystem::Radar, model_.widget_system(key), false);
						for (const auto &sensor : view->systems)
							if (radar && sensor.id == *radar)
							{
								Json settings = Json::object();
								if (const auto *layout = model_.layout())
									for (const auto &w : layout->at("widgets"))
										if (w.at("id") == key)
											settings = w.value("sweep", Json::object());
								auto &sweep = radar_sweeps_[key];
								if (!sweep)
									sweep = std::make_unique<RadarSweep>();
								draw_radar_plot(radar_textures_, app_->session->catalog(), *view, sensor,
								                model_.waypoint(), *sweep, settings, centre, radius * .66f, GetTime(),
								                app_->reduced_motion);
							}
					}
					if (sharing_pods_.contains(key))
					{
						auto pod = sharing_pods_.at(key);
						pod.x += ox;
						pod.y += oy;
						// Two mounting arms join the external pod to the casing, with their roots tucked behind its outer rim.
						for (const auto &[angle, end_x] : std::array<std::pair<float, float>, 2>{
						         {{65.f, pod.x + 25}, {48.f, pod.x + pod.width - 25}}})
						{
							const float radians = angle * 3.141592653589793f / 180;
							const Vector2 start{centre.x + std::cos(radians) * radius * .90f,
							                    centre.y + std::sin(radians) * radius * .90f};
							const Vector2 end{end_x, pod.y + 9};
							DrawLineEx(start, end, 18, {7, 9, 9, 255});
							DrawLineEx(start, end, 12, {116, 111, 96, 255});
							DrawLineEx(start, end, 5, {207, 192, 158, 255});
						}
					}
					image(hud("helm-radar-ring"), {centre.x - radius, centre.y - radius, radius * 2, radius * 2});
					if (sharing_pods_.contains(key))
					{
						const bool active = model_.is_on_screen(key);
						const bool focused =
						    context_->GetFocusElement() == document_->GetElementById(id("on-screen/" + key));
						auto pod = sharing_pods_.at(key);
						pod.x += ox;
						pod.y += oy;
						const auto plate = [](Rectangle box, float cut, Color colour)
						{
							const std::array<Vector2, 8> corners{{{box.x + cut, box.y},
							                                      {box.x + box.width - cut, box.y},
							                                      {box.x + box.width, box.y + cut},
							                                      {box.x + box.width, box.y + box.height - cut},
							                                      {box.x + box.width - cut, box.y + box.height},
							                                      {box.x + cut, box.y + box.height},
							                                      {box.x, box.y + box.height - cut},
							                                      {box.x, box.y + cut}}};
							const Vector2 middle{box.x + box.width / 2, box.y + box.height / 2};
							for (std::size_t i = 0; i < corners.size(); ++i)
								DrawTriangle(middle, corners[(i + 1) % corners.size()], corners[i], colour);
						};
						plate({pod.x + 3, pod.y + 5, pod.width, pod.height}, 10, {4, 7, 8, 255});
						plate(pod, 8, focused ? Color{239, 153, 65, 255} : Color{191, 178, 148, 255});
						plate({pod.x + 3, pod.y + 3, pod.width - 6, pod.height - 6}, 7, {62, 64, 59, 255});
						plate({pod.x + 7, pod.y + 7, pod.width - 14, pod.height - 14}, 5, {17, 25, 26, 255});
						DrawRectangleRec({pod.x + 3, pod.y + 15, 4, pod.height - 30}, {213, 93, 24, 255});
						DrawRectangleRec({pod.x + pod.width - 7, pod.y + 15, 4, pod.height - 30}, {213, 93, 24, 255});
						for (const auto screw : std::array<Vector2, 2>{
						         {{pod.x + 13, pod.y + 6}, {pod.x + pod.width - 13, pod.y + pod.height - 6}}})
						{
							DrawCircleV(screw, 3, {10, 13, 13, 255});
							DrawLineEx({screw.x - 2, screw.y}, {screw.x + 2, screw.y}, 1, {160, 153, 131, 255});
						}
						const Vector2 lamp{pod.x + pod.width - 26, pod.y + pod.height / 2};
						DrawCircleV(lamp, 12, {7, 12, 10, 255});
						DrawCircleLinesV(lamp, 12, {133, 131, 109, 255});
						DrawCircleV(lamp, 8, active ? Color{107, 252, 130, 255} : Color{42, 65, 43, 255});
						if (active)
							DrawCircleV({lamp.x - 2, lamp.y - 2}, 2, {220, 255, 221, 255});
					}
					if (!model_.manual_active() && dial_systems_.contains(key))
					{
						const auto heading = model_.system_request(dial_systems_.at(key), "heading_mdeg");
						const float angle = heading * 3.141592653589793f / 180000;
						const Vector2 marker{centre.x + std::cos(angle) * radius * .87f,
						                     centre.y - std::sin(angle) * radius * .87f};
						const auto pointer = hud("heading-pointer");
						const float size = std::max(28.f, radius * .2f);
						DrawTexturePro(
						    pointer, {0, 0, static_cast<float>(pointer.width), static_cast<float>(pointer.height)},
						    {marker.x, marker.y, size, size}, {size / 2, size / 2}, 90 - heading / 1000.f, WHITE);
					}
				}
				if (!model_.manual_active() && document_->GetElementById(id("speed/" + key)))
				{
					auto track = throttle_rect(key);
					track.x += ox;
					track.y += oy;
					image(hud("helm-throttle-track"), track);
					const auto system = model_.widget_system(key);
					const auto minimum = model_.system_minimum(system), maximum = model_.system_maximum(system);
					const float fraction =
					    maximum > minimum ? std::clamp(static_cast<float>(model_.system_request(system) - minimum) /
					                                       (maximum - minimum),
					                                   0.f, 1.f)
					                      : 0;
					const auto &marks = hud_metadata_.at("helm-throttle-track").at("markers");
					const float top = marks.at("travel_max")[1].get<float>() /
					                  hud_metadata_.at("helm-throttle-track").at("image_size")[1].get<float>(),
					            bottom = marks.at("travel_min")[1].get<float>() /
					                     hud_metadata_.at("helm-throttle-track").at("image_size")[1].get<float>();
					const float y = track.y + track.height * (bottom - fraction * (bottom - top));
					image(hud("helm-throttle-handle"),
					      {track.x - track.width * .1f, y - track.width * .2f, track.width * 1.2f, track.width * .4f});
					if (context_->GetFocusElement() == document_->GetElementById(id("speed/" + key)))
						DrawRectangleLinesEx(
						    {track.x - track.width * .1f, y - track.width * .2f, track.width * 1.2f, track.width * .4f},
						    2, {255, 190, 94, 255});
				}
				if (auto *readout = document_->GetElementById("value/" + key);
				    readout && readout->GetClassNames() == "readout" && !readout->GetInnerRML().empty())
				{
					const auto at = readout->GetAbsoluteOffset(Rml::BoxArea::Border);
					image(hud("readout-panel"), {ox + at.x - 8, oy + at.y - 8, readout->GetOffsetWidth() + 16,
					                             readout->GetOffsetHeight() + 16});
				}
			}
		draw_navigation(ox, oy);
		EndScissorMode();
	}
};
} // namespace phoenix
