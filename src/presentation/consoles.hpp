#pragma once
#include "navigation_camera.hpp"
#include "region_markers.hpp"
#include "map_plots.hpp"
namespace phoenix
{
class ConsoleSurface final : public Rml::EventListener
{
	ConsoleId route_;
	bool prepare_dirty_ = true, render_dirty_ = true, render_urgent_ = true, prepared_cache_ = false;
	double render_dirty_since_ = 0, last_paint_cost_ = .003;
	std::shared_ptr<const Observation> prepared_source_;
	std::weak_ptr<SessionPort> prepared_owner_;
	std::uint64_t prepared_generation_ = 0, painted_tick_ = 0;
	std::string prepared_environment_;
	std::set<std::string> prepared_pads_;
	std::vector<std::pair<std::string, std::string>> cached_controls_, cached_readouts_;
	std::vector<Application::TextControl> cached_inputs_;
	std::map<std::string, bool> cached_toggles_;
	std::map<std::string, Application::ControlPlacement> cached_placements_;
	std::map<std::string, Application::ControlPlacement> painted_controls_;
	RenderTexture2D ui_texture_{};
	static bool same_observation_scope(const Observation &left, const Observation &right)
	{
		if (left.own.id != right.own.id || left.own.definition != right.own.definition ||
		    left.console != right.console || left.controls != right.controls || left.feeds != right.feeds ||
		    left.additional_consoles.size() != right.additional_consoles.size())
			return false;
		for (std::size_t i = 0; i < left.additional_consoles.size(); ++i)
			if (!same_observation_scope(left.additional_consoles[i], right.additional_consoles[i]))
				return false;
		return true;
	}
	static bool same_observation_scope(const std::shared_ptr<const Observation> &left,
	                                   const std::shared_ptr<const Observation> &right)
	{
		return left && right ? same_observation_scope(*left, *right) : !left && !right;
	}
	void remember_metadata(const Application &app)
	{
		const auto prefix = id("");
		cached_controls_.clear();
		cached_readouts_.clear();
		cached_inputs_.clear();
		cached_toggles_.clear();
		cached_placements_.clear();
		for (const auto &entry : app.screen_controls)
			if (entry.first.starts_with(prefix))
				cached_controls_.push_back(entry);
		for (const auto &entry : app.screen_readouts)
			if (entry.first.starts_with(prefix))
				cached_readouts_.push_back(entry);
		for (const auto &entry : app.screen_inputs)
			if (entry.id.starts_with(prefix))
				cached_inputs_.push_back(entry);
		for (const auto &entry : app.screen_toggles)
			if (entry.first.starts_with(prefix))
				cached_toggles_.insert(entry);
		for (const auto &entry : app.control_placements)
			if (entry.first.starts_with(prefix))
				cached_placements_.insert(entry);
	}
	std::map<std::string, Application::ControlPlacement> interactive_placements() const
	{
		std::map<std::string, Application::ControlPlacement> result;
		for (const auto &[name, label] : cached_controls_)
			if (auto found = cached_placements_.find(name); found != cached_placements_.end())
				result.insert(*found);
		for (const auto &input : cached_inputs_)
			if (auto found = cached_placements_.find(input.id); found != cached_placements_.end())
				result.insert(*found);
		return result;
	}
	void check_painted_controls()
	{
		const auto current = interactive_placements();
		if (current.size() != painted_controls_.size())
		{
			render_urgent_ = true;
			return;
		}
		for (const auto &[name, placement] : current)
		{
			const auto found = painted_controls_.find(name);
			if (found == painted_controls_.end())
			{
				render_urgent_ = true;
				return;
			}
			const auto &old = found->second;
			if (placement.x != old.x || placement.y != old.y || placement.width != old.width ||
			    placement.height != old.height || placement.enabled != old.enabled)
			{
				render_urgent_ = true;
				return;
			}
		}
	}
	void restore_metadata(Application &app)
	{
		app.screen_controls.insert(app.screen_controls.end(), cached_controls_.begin(), cached_controls_.end());
		app.screen_readouts.insert(app.screen_readouts.end(), cached_readouts_.begin(), cached_readouts_.end());
		app.screen_inputs.insert(app.screen_inputs.end(), cached_inputs_.begin(), cached_inputs_.end());
		app.screen_toggles.insert(cached_toggles_.begin(), cached_toggles_.end());
		const auto focused = context_->GetFocusElement() ? context_->GetFocusElement()->GetId() : std::string{};
		for (auto entry : cached_placements_)
		{
			entry.second.focused = entry.first == focused;
			app.control_placements.insert(std::move(entry));
		}
	}

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
#include "conversations.hpp"
	TextureCache radar_textures_;
	std::map<std::string, std::unique_ptr<RadarSweep>> radar_sweeps_;
	Application *app_ = nullptr;
	std::string signature_;
	std::vector<std::pair<std::string, std::string>> content_packs_;
	std::map<std::string, std::vector<ConsoleSpec>> resolved_consoles_;
	const std::vector<ConsoleSpec> &console_specs(const Application &app, const std::string &ship)
	{
		const auto found = resolved_consoles_.find(ship);
		if (found != resolved_consoles_.end())
			return found->second;
		return resolved_consoles_.emplace(ship, resolve_consoles(app.session->catalog(), ship)).first->second;
	}
	const Json *prepared_layout_ = nullptr;
	std::uint64_t prepared_layout_revision_ = ~std::uint64_t{0}, layout_identity_ = 0;
	struct StanceChoice
	{
		std::string action, target, label;
		bool selected;
		bool operator==(const StanceChoice &) const = default;
	};
	std::vector<StanceChoice> command_choices_;
	std::string command_language_, command_markup_;

	std::map<std::string, std::string> displayed_values_;
	std::vector<std::pair<std::string, std::string>> readout_values_, resource_readouts_;
	std::set<std::string> visible_readouts_;
	void measure_readouts()
	{
		visible_readouts_.clear();
		auto *panel = document_->GetElementById("gameplay");
		const auto panel_at = panel->GetAbsoluteOffset(Rml::BoxArea::Border);
		const auto bottom = panel_at.y + panel->GetOffsetHeight() + 100;
		const auto viewport_bottom = static_cast<float>(context_->GetDimensions().y) + 100;
		auto measure = [&](const auto &values)
		{
			for (const auto &[name, text] : values)
				if (auto *target = element(id(name)); target && target->IsVisible(true))
				{
					const auto at = target->GetAbsoluteOffset(Rml::BoxArea::Border);
					const auto top = name.starts_with("resource/") ? -100.f : std::max(-100.f, panel_at.y - 100);
					const auto limit =
					    name.starts_with("resource/") ? viewport_bottom : std::min(viewport_bottom, bottom);
					if (at.y + target->GetOffsetHeight() >= top && at.y <= limit)
						visible_readouts_.insert(name);
				}
		};
		measure(readout_values_);
		measure(resource_readouts_);
	}
	bool update_readout(const std::string &name, const std::string &text, bool force)
	{
		auto *target = element(id(name));
		if (!target || displayed_values_[name] == text)
			return false;
		// Keep measured offscreen heights; accessible values are published separately.
		if (!force && !displayed_values_[name].empty() && !visible_readouts_.contains(name))
			return false;
		const auto encoded = Rml::StringUtilities::EncodeRml(text);
		if (auto *leaf =
		        target->GetNumChildren() == 1 ? dynamic_cast<Rml::ElementText *>(target->GetChild(0)) : nullptr)
			leaf->SetText(encoded);
		else
			target->SetInnerRML(encoded);
		displayed_values_[name] = text;
		return true;
	}

	struct MarkupValue
	{
		Rml::ObserverPtr<Rml::Element> element;
		std::string text;
	};
	std::map<Rml::Element *, MarkupValue> leaf_markup_;
	void leaf_markup(Rml::Element *target, const std::string &text)
	{
		auto &previous = leaf_markup_[target];
		if (previous.element.get() == target && previous.text == text)
			return;
		target->SetInnerRML(text);
		previous = {target->GetObserverPtr(), text};
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
	std::string gameplay_signature_, sensor_controls_signature_;
	std::string placement_signature_;
#include "authored_layout.hpp"
	std::string gameplay_readout_signature_, fault_dialog_signature_, fault_dialog_system_;
	std::string engineering_signature_;
	std::set<std::string> held_clicks_;
	std::map<std::string, Texture2D> hud_;
	TextureCache hud_cache_;
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
	std::array<bool, 3> keyboard_flight_{};
	std::uint64_t manual_sent_tick_ = ~std::uint64_t{0};
	bool syncing_values_ = false;
	bool settings_open_ = false;
	bool station_loading_ = false;
	std::string station_loading_markup_;
	std::string local_settings_tab_ = "display", local_settings_markup_;
	int text_scale_ = 100;
	std::string local_language_ = "en";
	std::vector<std::pair<std::string, std::string>> local_settings_controls_;
	std::string settings_button(const std::string &name, const std::string &label)
	{
		local_settings_controls_.emplace_back(id(name), label);
		return "<button id=\"" + id(name) + "\">" + Rml::StringUtilities::EncodeRml(localise(label)) + "</button>";
	}
	std::string star_choice(const std::string &action, const std::string &profile,
	                        const std::map<std::string, int> &levels, const std::string &selected, bool settings)
	{
		const int stars = complexity_stars(levels, profile);
		if (!stars && !levels.empty())
			return {};
		const auto label = (stars ? "Complexity: " + std::to_string(stars) + " of 3 stars" : "Complexity: " + profile) +
		                   (profile == selected ? " (selected)" : "");
		auto markup = settings ? settings_button(action, label) : button(action, label);
		return stars ? complexity_star_button(markup, stars, complexity_stars(levels, selected), asset_path("../menu/"))
		             : markup;
	}
	bool select_lobby_complexity(const std::string &profile)
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
				command.profile = profile;
				return app_->session->lobby_command(command);
			}
		return false;
	}
#include "personnel.hpp"
#include "away.hpp"
#include "craft.hpp"
#include "logistics.hpp"
#include "station_combat.hpp"
	void prepare_local_settings(Application &app, const ConsoleRegion &region)
	{
		local_settings_controls_.clear();
		const bool operations = local_settings_tab_ == "logistics" || local_settings_tab_ == "docking" ||
		                        local_settings_tab_ == "away" || local_settings_tab_ == "craft" ||
		                        local_settings_tab_ == "personnel";
		const auto content_width = std::max(180.f, region.width * .84f - 52.f);
		std::string markup =
		    "<div class=\"local-settings-card\"><div style=\"display:block;width:" + std::to_string(content_width) +
		    "dp;\"><h1>" + localise(operations ? "Ship operations" : "Console settings") + "</h1><div>";
		if (!operations)
			for (const auto &[tab, label] :
			     std::vector<std::pair<std::string, std::string>>{{"logistics", "Stores and deliveries"},
			                                                      {"docking", "Docking"},
			                                                      {"away", "Away missions"},
			                                                      {"craft", "Flight operations"},
			                                                      {"personnel", "Personnel"},
			                                                      {"display", "Display"},
			                                                      {"language", "Language"},
			                                                      {"audio", "Audio"},
			                                                      {"session", "Session"}})
				if (operations ==
				    (tab == "logistics" || tab == "docking" || tab == "away" || tab == "craft" || tab == "personnel"))
					markup += settings_button("local-settings.tab/" + tab, label);
		markup += "</div>";
		if (local_settings_tab_ == "logistics")
			markup += logistics_markup();
		else if (local_settings_tab_ == "docking")
			markup += docking_markup();
		else if (local_settings_tab_ == "away")
			markup += away_markup();
		else if (local_settings_tab_ == "craft")
			markup += craft_markup();
		else if (local_settings_tab_ == "personnel")
			markup += personnel_markup(app);
		else if (local_settings_tab_ == "display")
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
			                                                            {"siren", "Siren volume", app.siren_volume},
			                                                            {"sfx", "SFX volume", app.sfx_volume}})
			{
				markup += "<p>" + localise(label) + ": " + std::to_string(value) + "%</p>";
				markup += settings_button("local-settings.audio/" + channel + "/down", label + " -");
				markup += settings_button("local-settings.audio/" + channel + "/up", label + " +");
			}
		}
		else if (app.session->lobby().active && !app.session->lobby().launched)
		{
			if (!model_.layout() || !model_.layout()->value("command", false))
				for (const auto &seat : app.session->lobby().consoles)
					if (seat.local_screen == route_)
						for (const auto &choice : seat.profiles)
							markup +=
							    star_choice("complexity/" + choice, choice, seat.profile_stars, seat.profile, true);
		}
		else if (model_.observation())
		{
			const auto levels = model_.observation()->gameplay.value("profile_stars", std::map<std::string, int>{});
			const auto selected = model_.observation()->gameplay.value("profile", std::string{});
			if (levels.empty() && model_.observation()->gameplay.contains("profile"))
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
					markup += star_choice(descriptor_action(control.at("id")),
					                      control.at("id").get<std::string>().substr(8), levels, selected, true);
		}
		markup += settings_button("local-settings.close", "Return to station") + "</div></div>";
		if (markup != local_settings_markup_)
		{
			const auto focus = context_->GetFocusElement() ? context_->GetFocusElement()->GetId() : std::string{};
			auto *settings = document_->GetElementById("local-settings");
			const auto scroll = settings->GetFirstChild() ? settings->GetFirstChild()->GetScrollTop() : 0.f;
			elements_.clear();
			settings->SetInnerRML(markup);
			local_settings_markup_ = markup;
			if (auto *previous = document_->GetElementById(focus))
				previous->Focus();
			context_->Update();
			if (auto *card = settings->GetFirstChild())
				card->SetScrollTop(scroll);
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
		if (local_settings_tab_ == "personnel")
			for (const auto &[key, text] : personnel_readouts_)
			{
				app.screen_readouts.emplace_back(key, text);
				place(app, region, key);
			}
		if (local_settings_tab_ == "away")
			for (const auto &[key, text] : away_readouts_)
			{
				app.screen_readouts.emplace_back(key, text);
				place(app, region, key);
			}
		if (local_settings_tab_ == "logistics" || local_settings_tab_ == "docking")
			for (const auto &[key, text] : supply_readouts_)
			{
				app.screen_readouts.emplace_back(key, text);
				place(app, region, key);
			}
		if (local_settings_tab_ == "craft")
			for (const auto &[key, text] : craft_readouts_)
			{
				app.screen_readouts.emplace_back(key, text);
				place(app, region, key);
			}
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
		if (app_ && app_->session && active_theme_.contains("assets") && active_theme_.at("assets").contains(name))
		{
			const auto ref = active_theme_.at("assets").at(name).get<std::string>();
			return hud_cache_.load(ref, app_->session->catalog().assets.at(ref));
		}
		if (!hud_.contains(name))
		{
			const auto texture = hud_cache_.load_file(asset_path(name + ".png"));
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
		const float density = context_->GetDensityIndependentPixelRatio();
		Rml::ElementList headings;
		widget->GetElementsByTagName(headings, "h2");
		const float header =
		    std::max(45.f * density, (headings.empty() ? 0.f : headings.front()->GetOffsetHeight()) + 16.f * density);
		const float height = std::max(30.f * density, widget->GetOffsetHeight() - header - 55.f * density),
		            width = height * size[0].get<float>() / size[1].get<float>();
		return {at.x + (widget->GetOffsetWidth() - width) / 2, at.y + header, width, height};
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
	std::string button(const std::string &action, const std::string &label, const std::string &visual = "")
	{
		controls_.emplace_back(id(action), label);
		if (action.starts_with("on-screen/"))
			return on_screen_markup(action);
		return "<button id=\"" + Rml::StringUtilities::EncodeRml(id(action)) + "\">" +
		       (visual.empty() ? Rml::StringUtilities::EncodeRml(translate(label, app_->pseudo_locale))
		                       : "<span class=\"camera-icon\">" + Rml::StringUtilities::EncodeRml(visual) + "</span>") +
		       "</button>";
	}

	std::string console_markup(std::string markup) const
	{
		std::string sheets = complexity_star_styles();
		for (const auto &[token, file] : std::vector<std::pair<std::string, std::string>>{
		         {"CARD_SPRITE", "helm-background-9slice"}, {"BUTTON_SPRITE", "button-generic-9slice"}})
		{
			const auto data = parse_json(read_file(asset_path("../menu/" + file + ".sprite.json"), 65536), file);
			const auto &slice = data.at("nine_slice");
			const int w = data.at("image_size")[0], h = data.at("image_size")[1];
			const int top = slice.at("top"), right = slice.at("right"), bottom = slice.at("bottom"),
			          left = slice.at("left");
			auto px = [](int n)
			{
				return std::to_string(n) + "px ";
			};
			sheets += "@spritesheet " + token + " { src: \"" + asset_path("../menu/" + file + ".png") +
			          "\"; resolution: 1x; " + token + "-outer: 0px 0px " + px(w) + px(h) + "; " + token +
			          "-inner: " + px(left) + px(top) + px(w - left - right) + px(h - top - bottom) + "; }\n";
			const auto needle = "image(" + token + ")";
			markup.replace(markup.find(needle), needle.size(),
			               "ninepatch(" + token + "-outer, " + token + "-inner, 14px 18px 14px 18px) border-box");
		}
		for (auto at = markup.find("px"); at != std::string::npos; at = markup.find("px", at + 2))
			markup.replace(at, 2, "dp");
		markup.insert(markup.find("<style>") + 7, sheets);
		return markup;
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
		document_ = context_->LoadDocumentFromMemory(console_markup(R"(<rml><head><style>
body {width:100%;height:100%;font-family:LatoLatin;font-size:20px;color:#e5e4ca;}

.command-console #widgets,.command-console #connection-status {display:none;}
.command-console #gameplay {background:transparent;border:0px;padding:0;}
.embedded-console #toolbar h1 {margin-left:0;}
.command-card {display:block;max-width:480px;box-sizing:border-box;width:100%;padding:18px 20px;margin:0 0 12px;background:#101b1c;decorator:image(CARD_SPRITE);}
.stance-row {display:block;}
.command-console #toolbar h1 {background:#101b1cee;padding:8px 14px;}
.narrow-command #gameplay-controls .stance-button {width:96%;max-width:none;}
.stance-label {pointer-events:none;display:block;max-width:90px;margin:0 auto;font-size:1em;text-align:center;}
.stance-state {pointer-events:none;display:block;font-size:0.6em;margin-top:8px;color:#ffd082;}
.stance-button:focus .stance-state {background:#fff3d9;color:#101b1c;}
.command-card h2 {margin:0 8px 10px;color:#ffbd76;}
.objective-card {max-width:none;}
#gameplay-controls .stance-button {vertical-align:top;width:46%;max-width:144px;min-width:72px;min-height:100px;margin:2px;padding:14px 6px;font-size:.75em;background:#20292b;decorator:image(BUTTON_SPRITE);border:2px transparent;}
#gameplay-controls .stance-button.selected {image-color:#ffb668;border-color:#efb66b;color:#ffe2a5;}
#gameplay-controls .stance-button:hover,#gameplay-controls .stance-button:focus {image-color:#ffe1b8;border-color:#fff3d9;color:#ffffff;}
h1 {font-size:1.3em;display:inline-block;margin:0 18px 8px 0;}
h2 {font-size:1em;display:block;margin:4px 0;}
button {display:inline-block;padding:10px;margin:4px;border:1px #9c8b68;background:#222b2b;color:#f0e3c2;}
button:hover,button:focus {background:#455458;border-color:#ec9c52;}
#toolbar {position:absolute;left:20px;top:18px;right:20px;height:85px;overflow:visible;z-index:2;}
#toolbar h1 {margin-left:76px;}
#connection-status {font-size:.75em;}
#widgets {position:absolute;left:4%;right:4%;top:125px;bottom:4%;}
#gameplay {position:absolute;left:60%;right:4%;top:125px;bottom:4%;overflow-x:hidden;overflow-y:auto;clip:always;box-sizing:border-box;background:#0b1719ee;border:1px #988b71;padding:8px;display:none;}
#sensor-panel {position:absolute;left:4%;top:105dp;width:30%;height:42%;overflow-y:auto;background:#0b1719ee;border:1px #988b71;padding:8dp;display:none;}
#sensor-controls,#sensor-readouts,#sensor-title {display:block;width:100%;box-sizing:border-box;}
#sensor-panel p {width:100%;font-size:0.8em;margin:6dp 0;box-sizing:border-box;}
#sensor-panel button {width:46%;min-width:140dp;min-height:44dp;box-sizing:border-box;font-size:0.8em;}
.local-settings-open #sensor-panel {visibility:hidden;}
#gameplay scrollbarvertical {width:14px;background:#263638;}
#gameplay scrollbarvertical sliderbar {min-height:32px;background:#ccc2af;}
#gameplay scrollbarvertical slidertrack {width:14px;}
#gameplay-controls button {width:210px;min-height:48px;box-sizing:border-box;}
.engineering #widgets {display:block;pointer-events:none;z-index:4;}
.engineering #widgets .widget {display:none;}
.engineering #widgets .camera_mode-widget,.engineering #widgets .camera_direction-widget {display:block;pointer-events:auto;}
.engineering #connection-status {display:none;}
.engineering #gameplay {background:transparent;border:0px;}
#engineering-cards {display:block;width:100%;}
.team-dispatch #engineering-cards,.team-dispatch #widgets {display:none;}
.team-dispatch #gameplay {background:#0b1719ee;border:1px #988b71;}
.engineering-card {display:inline-block;vertical-align:top;box-sizing:border-box;margin:0 12px 14px 0;padding:16px;background:#0b1719ee;border:1px #988b71;}
.resource-meter {display:block;width:100%;height:12px;margin:5px 0;background:#263534;}
.resource-fill {display:block;height:100%;background:#88c6a2;}
.resource-heat {background:#ef9958;}
.engineering-card h2 {color:#ffbd76;}
.engineering-card p {min-height:56px;line-height:1.5;}
.engineering-card button {width:180px;min-height:48px;box-sizing:border-box;font-size:.85em;}
.widget {position:absolute;box-sizing:border-box;padding:8px;overflow:hidden;}
p {display:block;word-break:break-word;font-size:.85em;margin:4px 0;}
.dial {position:absolute;left:12px;right:12px;top:48px;bottom:72px;}
.widget button {position:absolute;bottom:8px;left:8px;}
.radar-widget button.on-screen {position:absolute;left:auto;right:8px;}
.readout {position:absolute;bottom:12px;left:16px;right:16px;height:44px;text-align:center;color:#cbe9c8;}
.heading-value {display:inline-block;vertical-align:middle;margin:0 8px;width:145px;min-height:1.8em;height:auto;background:#0a1719;color:#cbe9c8;border:1px #988b71;font-size:1.3em;}
.throttle {position:absolute;background:transparent;border:0px;}
.throttle slidertrack {background:transparent;}
.throttle sliderbar {width:100%;background:transparent;}
.heading-value:focus {border-color:#ffb665;background:#122629;}
.throttle sliderarrowdec,.throttle sliderarrowinc {display:none;}
.ready-card {position:absolute;left:10%;top:390px;width:80%;box-sizing:border-box;padding:28px;background:#0b1719ee;border:2px #d48a48;}
.profile-card {top:18%;}
.ready-card h1 {display:block;font-size:1.6em;}
.ready-card button {min-width:120px;}
.ready-card .avatar-choice {min-width:0;width:64px;height:64px;padding:6px;}
.ready-card input {display:block;width:90%;height:40px;background:#211f1a;color:#fff0d4;margin:10px 0;}
.profile-tv {position:absolute;right:20px;top:0;width:264px;height:330px;background:transparent;border:0px;padding:0px;}
.profile-tv img,.avatar-choice img {pointer-events:none;}
.settings-cog {position:absolute;left:0;top:0;width:64px;height:64px;margin:0;padding:0px;background:transparent;border:2px transparent;}
.settings-cog img {position:absolute;left:4px;top:4px;width:54px;height:54px;pointer-events:none;}
.helm #toolbar {right:4%;height:64px;}
.helm #toolbar h1,.helm #connection-status,.helm .radar-widget h2,.helm .heading-widget h2,.helm .heading-widget label,.helm .heading-widget p {display:none;}
.helm .radar-widget {overflow:visible;}
.helm .dial {left:0;right:0;top:0;bottom:0;}
.helm .radar-widget button,.helm .heading-value {opacity:0;}
.helm .radar-widget button:focus,.helm .heading-value:focus,.helm .radar-widget button.pad-selected,.helm .heading-value.pad-selected {opacity:1;}
.helm .radar-widget button.on-screen {opacity:1;position:absolute;bottom:auto;right:auto;min-width:0;z-index:4;}
.helm .radar-widget button.on-screen,.helm .radar-widget button.on-screen:hover,.helm .radar-widget button.on-screen:focus {background:transparent;border:0px;margin:0;padding:0;min-height:0;border-radius:0px;pointer-events:none;}
.helm .radar-widget button.on-screen span {display:none;}
.helm .radar-widget button.on-screen .share-label {display:block;left:24px;right:50px;top:12px;height:1.28em;line-height:1.28em;font-size:1.1em;text-align:left;color:#f0dfbf;}
.helm .radar-widget button.on-screen .share-caption {display:block;left:25px;bottom:14px;height:1.24em;line-height:1.24em;font-size:0.65em;}
.helm .radar-widget button.on-screen:focus .share-label {text-decoration:underline;color:#ffd49b;}
.helm .radar-widget button.on-screen .share-live {display:none;}
.helm .radar-widget button.on-screen-active .share-live {display:block;}
.helm .radar-widget button.on-screen-active .share-idle {display:none;}

button.on-screen {position:relative;box-sizing:border-box;width:180px;height:66px;min-height:66px;margin:4px;padding:0;border:3px #535b59;border-top-color:#9b8770;border-left-color:#8c735a;border-bottom-color:#141a1c;border-right-color:#202728;background:#242e30;color:#efdfbd;border-radius:7px;vertical-align:middle;}
button.on-screen span {pointer-events:none;}
button.on-screen:hover {background:#303c3e;border-top-color:#d1a26d;}
button.on-screen:focus {border-color:#ffd09a;}
button.on-screen .share-monitor {position:absolute;left:12px;top:18px;width:25px;height:17px;border:2px #a7906d;border-radius:2px;background:#081315;pointer-events:none;}
button.on-screen .share-monitor span {position:absolute;left:8px;top:19px;width:10px;height:3px;background:#a7906d;}
button.on-screen .share-label {position:absolute;left:48px;right:30px;top:9px;font-size:0.8em;color:#f0dfbf;pointer-events:none;}
button.on-screen .share-caption {position:absolute;left:49px;bottom:9px;font-size:0.55em;color:#a3aaa1;pointer-events:none;}
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
.manual-toggle .manual-label {position:absolute;left:17%;top:50%;margin-top:-0.67em;height:1.34em;font-size:0.9em;color:#f3e6c9;pointer-events:none;}
.manual-toggle .manual-lamp {position:absolute;left:55%;top:50%;margin-top:-.65em;width:1.3em;height:1.3em;box-sizing:border-box;border:3px #ff625f;background:#410e10;color:#ffc4ba;font-family:Space Mono;font-size:0.85em;line-height:.94em;text-align:center;pointer-events:none;}
.manual-toggle.manual-on .manual-lamp {border-radius:50%;background:#4fd791;border-color:#a5ffcb;color:#09261b;}
.manual-toggle .manual-state {position:absolute;left:70%;top:50%;margin-top:-0.77em;height:1.54em;font-family:Space Mono;font-size:0.65em;line-height:1.54em;color:#f3e6c9;pointer-events:none;}
.helm .radar-widget button.manual-toggle:hover,.helm .radar-widget button.manual-toggle:focus {border-color:#ffe4b0;}
.helm .radar-widget button.manual-toggle:disabled {opacity:.5;}

.profile-tv span {position:absolute;left:20%;top:69%;width:55%;text-align:center;pointer-events:none;}
#manual-flight {position:absolute;right:2%;top:76px;width:22%;min-width:180px;max-width:280px;max-height:80%;overflow:auto;z-index:8;background:#08171bee;padding:8px;box-sizing:border-box;}
#manual-flight button {width:46%;min-height:44px;margin:2px;padding:6px;font-size:0.8em;box-sizing:border-box;}
#manual-flight button.flight-mode {width:96%;}
#manual-stick {display:block;position:relative;width:160px;height:160px;margin:8px auto;border:2px #98cbb8;border-radius:80px;background:#10292a;}
#manual-knob {display:block;position:absolute;width:32px;height:32px;margin-left:-16px;margin-top:-16px;left:50%;top:50%;border-radius:16px;background:#bce8ad;pointer-events:none;}
#manual-flight p {font-size:0.7em;}
.manual-flight .throttle-widget,.manual-flight .heading-widget {visibility:hidden;}
.local-settings-open #manual-flight {visibility:hidden;}
#local-settings {display:none;position:absolute;left:0;top:0;width:100%;height:100%;background:#080d0fee;z-index:20;}
.local-settings-open #local-settings {display:block;}
.local-settings-open #comms-layer,.local-settings-open #toolbar,.local-settings-open #widgets,.local-settings-open #gameplay,.local-settings-open #lobby {visibility:hidden;}
.local-settings-card {position:absolute;left:8%;top:8%;width:84%;max-height:84%;overflow-x:hidden;overflow-y:auto;box-sizing:border-box;padding:24px;background:#0b1719;border:2px #988b71;}
.local-settings-card h1 {display:block;}
)"
		                                                            R"(
.local-settings-card {display:block;}
.local-settings-card > div {display:block;width:100%;}
.local-settings-card h1,.local-settings-card h2,.local-settings-card p {display:block;width:100%;box-sizing:border-box;}
.personnel-row {display:block;width:100%;box-sizing:border-box;border-bottom:1px #526265;padding:8px;}
.away-attention button {background:#664e27;border:2px #f2d68a;}
.personnel-row img {width:64px;height:64px;vertical-align:middle;}
.personnel-row button {width:75%;}
.local-settings-card button {width:190px;min-height:48px;box-sizing:border-box;}
.local-settings-card .personnel-row button {width:80%;text-align:left;}
.navigation-console #gameplay {display:none;}
.navigation-console #widgets {display:block;pointer-events:none;z-index:4;}
.navigation-console #widgets .widget {display:none;}
.navigation-console #widgets .camera_mode-widget,.navigation-console #widgets .camera_direction-widget {display:block;pointer-events:auto;}
.navigation-console #gameplay {left:45%;top:0;width:45%;height:100px;background:transparent;}
.navigation-controls {position:absolute;left:1%;bottom:1%;width:98%;background:#08171bee;}
.navigation-controls button {min-height:42px;margin:3px;}
.navigation-controls input {width:140px;}
.navigation-objectives {position:absolute;left:12px;top:115px;max-width:320px;background:#08171bdd;padding:8px;}
.mission-result {display:block;position:absolute;left:0;top:0;width:100%;height:100%;background:#070c09;z-index:15;color:#bce8ad;pointer-events:auto;}
.mission-art {position:absolute;left:0;top:0;width:100%;height:100%;}
.camera-choices {position:absolute;top:28px;left:8px;width:100px;height:100px;}
.camera-art {position:absolute;left:0;top:0;width:100%;height:100%;pointer-events:none;}
.camera-choices button {display:block;position:absolute;bottom:auto;box-sizing:border-box;min-width:0;min-height:0;font-family:Space Mono;font-size:1.4em;line-height:1;padding:0;margin:0;tab-index:auto;text-align:center;background:transparent;border:2px transparent;color:#e9dec4;}
.camera-icon {position:absolute;display:block;left:0;right:0;top:50%;margin-top:-.5em;height:1em;pointer-events:none;}
.camera_mode-widget .camera-choices button {font-size:1.2em;}
.camera_mode-widget .camera-icon {margin-top:-.5em;height:1em;}
.camera-choices button:hover {background:#ffc07a22;border-color:#e9dec488;}
.camera-choices button.selected {background:#fca65133;border-color:#ffc078;color:#ffc078;}
.camera-choices button:focus {border-color:#fff3d9;background:#fff3d933;}
.camera_mode-widget h2,.camera_direction-widget h2 {font-size:0.7em;margin:0;color:#e9dec4;background:#071012bb;padding:2px 4px;}
.camera-status {position:absolute;left:0;right:0;text-align:center;font-size:0.7em;color:#f1ddaf;pointer-events:none;}
.mission-copy {display:block;position:absolute;left:20%;top:21%;width:60%;height:51%;overflow:auto;font-family:Space Mono;background:#081b14ed;padding:24px;box-sizing:border-box;border:1px #51744c;pointer-events:auto;}
.mission-copy h1 {font-size:0.9em;letter-spacing:3px;color:#81af79;margin:0 0 22px 0;}
.mission-copy h2 {font-size:2.2em;letter-spacing:2px;color:#d1f9be;margin:0 0 20px 0;}
.mission-copy p {font-size:1.1em;margin:10px 0;}
.mission-copy .mission-report-main p,.mission-copy .mission-report-side p {font-size:0.9em;margin:8px 0;}
.mission-copy h1,.mission-copy h2,.mission-copy p {display:block;width:100%;box-sizing:border-box;}
.mission-copy .terminal-rule {height:1px;background:#51744c;margin:18px 0;}
.mission-copy .terminal-status {font-size:0.8em;letter-spacing:2px;color:#81af79;}
.mission-copy button {display:block;width:260px;height:54px;box-sizing:border-box;margin:22px 0 0 0;padding:12px 20px;background:#122116;border:1px #87b77b;color:#d1f9be;}
.mission-copy button:hover,.mission-copy button:focus {background:#bce8ad;color:#102014;border-color:#d1f9be;}
#navigation-layer {position:absolute;left:0;top:0;width:100%;height:100%;pointer-events:none;}
#navigation-layer button,#navigation-layer input,#navigation-canvas {pointer-events:auto;}
#gameplay-controls .status-indicator {width:132px;min-height:30px;height:32px;margin:2px;padding:3px;font-size:0.7em;overflow:hidden;}
.integrated-helm #gameplay-controls button {width:210dp;min-height:48dp;font-size:.85em;padding:6dp;margin:2dp;}
.radar-contact {position:absolute;width:24px;height:24px;min-width:0;min-height:0;margin:0;padding:0;background:transparent;border:1px transparent;z-index:4;}
.radar-contact:focus {border-color:#ffe2a5;background:#285052;}
#fault-dialog {position:absolute;left:10%;top:15%;width:80%;height:65%;padding:24px;box-sizing:border-box;background:#111e22;border:3px #dfa35d;z-index:80;overflow:auto;display:none;}
#fault-dialog p {font-size:1.1em;line-height:1.4;}
#comms-layer {position:absolute;left:1%;top:80px;width:98%;bottom:1%;background:#102027;}
#comms-list {position:absolute;left:0;top:0;height:100%;overflow:auto;}
#comms-list button {display:block;width:96%;min-height:48px;margin:4px;}
#comms-conversation {position:absolute;top:0;height:100%;}
#comms-header {display:block;width:100%;height:20%;overflow:auto;}
#comms-choices,#comms-incoming,#comms-compose {display:block;}
#comms-header button {width:44%;min-height:44dp;box-sizing:border-box;}
#comms-header h3,#comms-list h3 {display:block;margin:6px 4px;font-size:1.1em;}
#comms-history {height:52%;overflow:auto;background:#172b32;padding:8px;}
#comms-incoming {height:30px;}
#comms-choices {max-height:18%;overflow:auto;}
#comms-choices button {display:block;width:100%;box-sizing:border-box;min-height:44dp;margin:4dp 0;}
#comms-compose input {width:75%;height:40px;}
.layout-node {box-sizing:border-box;min-width:0;min-height:0;}
#authored-body {display:none;position:absolute;left:0;right:0;top:160dp;bottom:0;overflow:auto;}
#layout-tabs,#console-tabs {position:absolute;left:1%;right:1%;top:64dp;height:44dp;overflow-x:auto;white-space:nowrap;z-index:8;}
#layout-tabs {display:none;top:110dp;}
#layout-tabs button,#console-tabs button {min-height:44dp;display:inline-block;margin:0 4dp;}
#layout-tabs .selected,#console-tabs .selected {border:2dp #ffe4b0;}
.authored #authored-body,.authored #layout-tabs {display:block;}
.authored #authored-body .widget {display:block;box-sizing:border-box;}
.authored #toolbar h1 {display:block;font-size:1em;white-space:normal;word-break:break-all;max-width:100%;}
.authored .radar-widget button.on-screen {left:8dp;top:auto;right:auto;bottom:8dp;width:auto;height:auto;min-width:140dp;min-height:44dp;padding:6dp;background:#102027;border:1dp #988b71;pointer-events:auto;}
.authored .radar-widget button.on-screen .share-label {position:static;display:inline;left:auto;right:auto;top:auto;bottom:auto;font-size:1em;height:auto;line-height:1.2em;}
.authored .radar-widget button.on-screen .share-caption,.authored .radar-widget button.on-screen .share-monitor,.authored .radar-widget button.on-screen .share-screw,.authored .radar-widget button.on-screen .share-lamp-housing {display:none;}
.authored .radar-widget button.on-screen-active .share-live {position:static;display:inline;font-size:0.8em;}

.authored #authored-body #navigation-layer {pointer-events:auto;}
.local-settings-open #authored-body,.local-settings-open #layout-tabs,.local-settings-open #console-tabs,.local-settings-open #navigation-layer {visibility:hidden;}
.authored #layout-tabs {display:none;}
.station-page-open #console-tabs {visibility:visible;z-index:25;}
.station-page-open .local-settings-card {top:120dp;max-height:75%;}
#station-loading {display:none;position:absolute;left:0;top:0;width:100%;height:100%;background:#08131b;z-index:40;}
#station-loading div {position:absolute;left:10%;top:35%;width:80%;padding:20dp;background:#102027;}
</style></head><body><div id="console-tabs"></div><div id="layout-tabs"></div><div id="authored-body"></div><div id="comms-layer"><div id="comms-list"></div><div id="comms-conversation"><div id="comms-header"></div><div id="comms-history"></div><div id="comms-incoming"></div><div id="comms-choices"></div><div id="comms-compose"></div></div></div><div id="sensor-panel"><h2 id="sensor-title"></h2><div id="sensor-controls"></div><div id="sensor-readouts"></div></div><div id="fault-dialog"></div><div id="toolbar"></div><div id="widgets"></div><div id="gameplay"><div id="engineering-cards"></div><div id="gameplay-controls"></div><div id="gameplay-messages"></div><div id="gameplay-readouts"></div></div><div id="lobby"></div><div id="navigation-layer"></div><div id="manual-flight"></div><div id="local-settings"></div></body></rml>)"));
		if (!document_)
			throw std::runtime_error("Cannot create Console document");
		for (const char *event : {"mouseover", "mousemove", "focus", "blur", "keydown", "keyup"})
			document_->AddEventListener(event, this);
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
		if (ui_texture_.id)
			UnloadRenderTexture(ui_texture_);
		Rml::RemoveContext(context_name_);
	}
	void preview_text_scale(int scale)
	{
		text_scale_ = std::clamp(scale, 50, 200);
	}
	const UiResolved &authored_geometry() const
	{
		return authored_resolved_;
	}
	void preview_pages(const Json &layout)
	{
		if (!layout.contains("_preview_pages"))
			return;
		const auto hint = layout.at("_preview_pages").dump();
		const bool interact = layout.value("_preview_interact", false);
		if (hint != preview_pages_signature_ || (preview_interact_ && !interact))
			authored_pages_ = layout.at("_preview_pages").get<std::map<std::string, std::string>>();
		preview_pages_signature_ = hint;
		preview_interact_ = interact;
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
		prepare_dirty_ = render_dirty_ = render_urgent_ = true;
		if (station_loading_)
			return false;
		const auto action = name.substr(prefix.size());
		if (action == "systems" || action == "systems-close")
		{
			model_.act("game.status-close");
			inline_model_.act("game.status-close");
			systems_open_ = action == "systems";
			return true;
		}
		if (action.starts_with("inline/"))
		{
			const bool accepted = !inline_console_.empty() && inline_model_.act(action.substr(7));
			return accepted;
		}
		if (action.starts_with("radar-contact/"))
			return select_radar_contact(std::stoull(action.substr(14)));
		if (action.starts_with("layout-page/") || action.starts_with("game.console/") ||
		    action.starts_with("station-page/"))
			return authored_action(action);
		if (app_ && app_->session && away_action(action))
			return true;
		if (app_ && app_->session && supply_action(action))
			return true;
		if (app_ && app_->session && craft_action(action))
			return true;
		if (app_ && app_->session && personnel_action(action))
			return true;
		if (app_ && !settings_open_ && app_->screen != Screen::settings && conversation_action(action))
			return true;
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
		if (action == "settings" && route_ == 31)
		{
			app_->settings_console = route_;
			return invoke_navigation(*app_, "settings");
		}
		if (action == "settings" || action == "local-settings.close")
		{
			if (!app_->session)
				return false;
			settings_open_ = action == "settings" && !settings_open_;
			if (settings_open_)
				local_settings_tab_ = "display";
			cancel_pointer();
			model_.release_controls();
			document_->SetClass("local-settings-open", settings_open_);
			if (!settings_open_)
				if (auto *cog = document_->GetElementById(id("settings")))
					cog->Focus();
			return true;
		}
		if (action == "operations")
		{
			settings_open_ = true;
			local_settings_tab_ = "logistics";
			cancel_pointer();
			model_.release_controls();
			document_->SetClass("local-settings-open", true);
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
				              : channel == "siren" ? app_->siren_volume
				                                   : app_->sfx_volume;
				value = std::clamp(value + (action.ends_with("/up") ? 5 : -5), 0, 100);
				remember_preferences(*app_);
			}
			else if (!action.starts_with("complexity/"))
				return model_.act(action);
			else
				return select_lobby_complexity(action.substr(11));
			return true;
		}
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
			return select_lobby_complexity(action.substr(11));
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
		if (model_.observation())
			for (const auto &control : model_.observation()->gameplay.value("controls", Json::array()))
				if (control.value("action", "") == "preset" && descriptor_action(control.at("id")) == action)
					return false;
		const bool accepted = model_.act(action);
		app_->accessible_status = model_.announcement();
		return accepted;
	}
	bool controller_hold(const std::string &name, bool pressed)
	{
		if (!name.starts_with(id("game.control/")))
			return false;
		const auto key = name.substr(id("").size());
		if (!pressed)
			return model_.release_control(key);
		return !settings_open_ && model_.hold_control(key);
	}
	std::string controller_identity() const
	{
		if (!model_.observation())
			return {};
		std::string identity = std::to_string(model_.observation()->own.id) + "/" + model_.observation()->console;
		for (const auto &controller : model_.observation()->gameplay.value("controllers", Json::array()))
			identity += "/" + controller.at("id").get<std::string>() + "/" + controller.at("generation").dump() + "/" +
			            controller.at("human").dump();
		return identity;
	}

	bool controller_flight() const
	{
		return !settings_open_ && model_.layout() &&
		       std::any_of(model_.layout()->at("widgets").begin(), model_.layout()->at("widgets").end(),
		                   [](const auto &w)
		                   {
			                   return w.at("kind") == "throttle";
		                   });
	}
	Rml::Element *controller_element(const std::string &name)
	{
		return document_->GetElementById(name);
	}
	void controller_cancel()
	{
		cancel_pointer();
		model_.release_controls();
	}
	bool controller_back()
	{
		if (settings_open_)
			return action(id("local-settings.close"));
		if (profile_open_)
			return action(id("profile"));
		return false;
	}

	void ProcessEvent(Rml::Event &event) override
	{
		if (syncing_values_)
			return;
		render_dirty_ = true;
		if (event.GetType() != "mousemove" && event.GetType() != "mouseover" && event.GetType() != "mouseout")
			prepare_dirty_ = render_urgent_ = true;
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
		if (event.GetType() != "click")
			return;
		if (app_ && app_->screen == Screen::server_lobby && name == id("ready"))
			invoke_navigation(*app_, name);
		else
			action(name);
	}
	void focus(const std::string &name)
	{
		prepare_dirty_ = render_dirty_ = render_urgent_ = true;
		if (auto *element = this->element(name))
		{
			element->ScrollIntoView({Rml::ScrollAlignment::Nearest, Rml::ScrollAlignment::Nearest,
			                         Rml::ScrollBehavior::Instant, Rml::ScrollParentage::Closest});
			element->Focus(true);
		}
	}
	bool set_value(const std::string &name, const std::string &text)
	{
		if (name.starts_with(id("")))
			prepare_dirty_ = render_dirty_ = render_urgent_ = true;
		if (name == id("comms/draft") && text.size() <= 4096)
		{
			if (auto *field = dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(name)))
			{
				if (field->GetValue() != text)
					field->SetValue(text);
				return true;
			}
			return false;
		}
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
		if (settings_open_ || systems_open_ || !model_.status_system().empty() ||
		    (!inline_console_.empty() && !inline_model_.status_system().empty()))
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
		model_.release_controls();
		flush_manual();
	}
	void gamepad(const std::string &device, const std::string &control, float value)
	{
		if (control == "flight.impulse")
		{
			if (value > 0 && !settings_open_)
				model_.flight_button("impulse", -1, true);
			return;
		}
		if (control == "flight.boost" || control == "flight.port" || control == "flight.starboard")
		{
			model_.flight_button(control == "flight.boost" ? "boost" : "lateral_thrust",
			                     control == "flight.port"        ? -1000
			                     : control == "flight.starboard" ? 1000
			                                                     : 1,
			                     value > 0 && !settings_open_);
			return;
		}
		auto &axes = pad_axes_[device];
		(control == "flight.yaw" ? axes.yaw : axes.thrust) = static_cast<int>(std::lround(value * 1000));
		manual_input_.sample("pad/" + device, axes);
		if (!model_.manual_active() || settings_open_)
			manual_input_.cancel();
		// Both stick axes are sampled before the frame submits their combined value.
	}
	void manual_frame(bool keyboard_selected, FlightAxes keyboard_axes, bool focused)
	{
		if (keyboard_axes.yaw || keyboard_axes.thrust)
			prepare_dirty_ = render_dirty_ = render_urgent_ = true;
		const auto *keyboard_focus = context_->GetFocusElement();
		const bool keyboard_enabled = keyboard_selected && focused && !settings_open_ &&
		                              (!keyboard_focus || keyboard_focus->GetTagName() != "input");
		const std::array<int, 3> keys{KEY_Q, KEY_E, KEY_LEFT_SHIFT};
		if (keyboard_enabled && IsKeyPressed(KEY_I))
			model_.flight_button("impulse", -1, true);
		for (std::size_t i = 0; i < keys.size(); ++i)
		{
			const bool pressed = keyboard_enabled && (keyboard_flight_[i] ? IsKeyDown(keys[i]) : IsKeyPressed(keys[i]));
			if (pressed != keyboard_flight_[i])
				model_.flight_button(i == 2 ? "boost" : "lateral_thrust", i == 0 ? -1000 : i == 1 ? 1000 : 1, pressed);
			keyboard_flight_[i] = pressed;
		}
		if (!focused || settings_open_)
			model_.release_controls();
		manual_input_.sample("keyboard", keyboard_axes);
		if (!keyboard_selected)
			manual_input_.cancel_source("keyboard");
		const auto *focus = context_->GetFocusElement();
		if (focus && focus->GetTagName() == "input")
			manual_input_.cancel_source("keyboard");
		const auto identity = controller_identity();
		const bool active = model_.manual_active();
		if (identity != manual_identity_ || active != manual_was_active_ || !active || settings_open_ || !focused)
		{
			if (identity != manual_identity_ || !focused || settings_open_)
				model_.release_controls();
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
		if (!(model_.layout() && model_.layout()->contains("layout")))
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
		markup += "<p>" +
		          Rml::StringUtilities::EncodeRml(
		              localise("Q/E lateral thrust; Shift boost. Gamepad shoulders: lateral; west button: boost.")) +
		          "</p>";
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
			leaf_markup(document_->GetElementById("manual-values"), Rml::StringUtilities::EncodeRml(text));
			app.screen_readouts.emplace_back(id("manual-values"), text);
		}
	}
	bool pointer(std::uint64_t contact, float x, float y, DeviceEventKind kind)
	{
		render_dirty_ = true;
		if (kind != DeviceEventKind::Move || heading_contacts_.contains(contact) ||
		    throttle_contacts_.contains(contact) || navigation_contacts_.contains(contact) ||
		    joystick_contact_ == contact)
			prepare_dirty_ = true;
		if (settings_open_ || systems_open_ || !model_.status_system().empty() ||
		    (!inline_console_.empty() && !inline_model_.status_system().empty()))
			return false;
		if (kind == DeviceEventKind::Down)
			for (auto *hit = context_->GetElementAtPoint({x, y}); hit; hit = hit->GetParentNode())
				if (hit->GetTagName() == "button")
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
		if (kind == DeviceEventKind::Down && model_.layout() && model_.observation())
			for (const auto &w : model_.layout()->at("widgets"))
				if (w.at("kind") == "radar")
				{
					const auto key = w.at("id").get<std::string>();
					if (auto *dial = document_->GetElementById("dial/" + key))
					{
						const auto at = dial->GetAbsoluteOffset(Rml::BoxArea::Border);
						const Vector2 centre{at.x + dial->GetOffsetWidth() / 2, at.y + dial->GetOffsetHeight() / 2};
						const auto radius = std::min(dial->GetOffsetWidth(), dial->GetOffsetHeight()) * .33f;
						const auto &view = *model_.observation();
						const auto radar =
						    select_system(view.systems, OperatingSystem::Radar, model_.widget_system(key), false);
						for (const auto &sensor : view.systems)
							if (radar && sensor.id == *radar && sensor.enabled)
							{
								EntityId nearest = 0;
								float distance = 18 * context_->GetDensityIndependentPixelRatio();
								for (const auto &body : view.contacts)
									if (sensor.maximum > 0 &&
									    std::find(sensor.contact_ids.begin(), sensor.contact_ids.end(), body.id) !=
									        sensor.contact_ids.end() &&
									    std::hypot(double(body.x - view.own.x), double(body.y - view.own.y)) <=
									        sensor.maximum)
									{
										const Vector2 point{
										    centre.x + float(double(body.x - view.own.x) / sensor.maximum) * radius,
										    centre.y - float(double(body.y - view.own.y) / sensor.maximum) * radius};
										const float delta = std::hypot(x - point.x, y - point.y);
										if (delta < distance)
										{
											distance = delta;
											nearest = body.id;
										}
									}
								if (nearest)
									return select_radar_contact(nearest);
								for (const auto &b : view.bearing_contacts)
									if (std::find(sensor.contact_ids.begin(), sensor.contact_ids.end(), b.id) !=
									        sensor.contact_ids.end() &&
									    CheckCollisionPointCircle(
									        {x, y}, bearing_ring_point(centre, radius, b.bearing_mdeg), 14))
										return select_radar_contact(b.id);
							}
					}
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
		if (context_->GetNextUpdateDelay() <= 0)
			render_dirty_ = true;
		context_->Update();
		measure_authored_geometry();
		if (settings_open_)
			return;
		// Read geometry as one batch before changing any text. Interleaving these
		// operations forced a complete RmlUi layout for each telemetry readout.
		measure_readouts();
		bool changed = false;
		for (const auto &[name, text] : readout_values_)
			changed |= update_readout(name, text, false);
		for (const auto &[name, text] : resource_readouts_)
			changed |= update_readout(name, text, false);
		if (changed)
		{
			render_dirty_ = true;
			context_->Update();
		}
	}
	bool probe_conversation() const
	{
		auto *history = document_->GetElementById("comms-history");
		if (!history || !history->IsVisible(true) || !history->GetNumChildren())
			return false;
		for (const auto &placement : authored_resolved_.placements)
			if (placement.visible && !placement.component.starts_with("conversation-") &&
			    placement.component != "conversations")
				return false;
		const float density = context_->GetDensityIndependentPixelRatio();
		auto *choices = document_->GetElementById("comms-choices");
		Rml::ElementList replies;
		choices->GetElementsByTagName(replies, "button");
		for (auto *reply : replies)
			if (reply->GetOffsetWidth() < history->GetClientWidth() - 24 * density)
			{
				std::cout << "Comms reply width " << reply->GetOffsetWidth() << "; history "
				          << history->GetClientWidth() << std::endl;
				return false;
			}
		for (int i = 0; i < history->GetNumChildren(); ++i)
		{
			auto *row = history->GetChild(i);
			Rml::ElementList text;
			row->GetElementsByClassName(text, "message-text");
			if (text.empty() || row->GetOffsetWidth() < history->GetClientWidth() - 24 * density)
			{
				std::cout << "Comms row width " << row->GetOffsetWidth() << "; history " << history->GetClientWidth()
				          << std::endl;
				return false;
			}
			if (text.front()->GetOffsetWidth() < row->GetOffsetWidth() - 120 * density)
			{
				std::cout << "Comms text width " << text.front()->GetOffsetWidth() << "; row " << row->GetOffsetWidth()
				          << std::endl;
				return false;
			}
		}
		return true;
	}
	bool probe_tab(const std::string &action)
	{
		auto *control = document_->GetElementById(id(action));
		if (!control || !control->IsVisible(true))
			return false;
		control->ScrollIntoView();
		context_->Update();
		const auto at = control->GetAbsoluteOffset(Rml::BoxArea::Border);
		const float x = at.x + control->GetOffsetWidth() / 2, y = at.y + control->GetOffsetHeight() / 2;
		if (pointer(9999, x, y, DeviceEventKind::Down))
		{
			pointer(9999, x, y, DeviceEventKind::Cancel);
			return false;
		}
		context_->ProcessMouseMove(static_cast<int>(x), static_cast<int>(y), 0);
		context_->ProcessMouseButtonDown(0, 0);
		context_->ProcessMouseButtonUp(0, 0);
		model_.refresh(app_->session, route_);
		if (action.starts_with("game.console/"))
			return model_.observation() && model_.observation()->console == action.substr(13);
		if (action.starts_with("station-page/"))
			return settings_open_ && local_settings_tab_ == action.substr(13);
		return true;
	}
	bool probe_fit() const
	{
		const auto dimensions = context_->GetDimensions();
		for (const auto &placement : authored_resolved_.placements)
			if (placement.visible && model_.layout())
				for (const auto &widget : model_.layout()->at("widgets"))
					if (widget.at("id") == placement.component && widget.at("kind") == "radar")
					{
						auto *element = document_->GetElementById("widget/" + placement.component);
						const auto at = element->GetAbsoluteOffset(Rml::BoxArea::Border);
						if (at.x < -1 || at.y < -1 || at.x + element->GetOffsetWidth() > dimensions.x + 1 ||
						    at.y + element->GetOffsetHeight() > dimensions.y + 1)
							return false;
					}
		return true;
	}
	bool probe_readout_scroll()
	{
		// Explicit graphical smoke: deferred text becomes current on scrolling,
		// while its semantic value was already current before it became visible.
		auto values = readout_values_;
		values.insert(values.end(), resource_readouts_.begin(), resource_readouts_.end());
		for (const auto &[name, text] : values)
			if (auto *target = element(id(name)); target && target->IsVisible(true) && displayed_values_[name] != text)
			{
				const auto semantic = std::find_if(app_->screen_readouts.begin(), app_->screen_readouts.end(),
				                                   [&](const auto &entry)
				                                   {
					                                   return entry.first == id(name) && entry.second.ends_with(text);
				                                   });
				if (semantic == app_->screen_readouts.end())
				{
					std::cerr << "Deferred readout semantic missing: " << name << std::endl;
					return false;
				}
				std::vector<std::pair<Rml::Element *, float>> scroll;
				for (auto *parent = target->GetParentNode(); parent; parent = parent->GetParentNode())
					scroll.emplace_back(parent, parent->GetScrollTop());
				target->ScrollIntoView({Rml::ScrollAlignment::Start, Rml::ScrollAlignment::Nearest,
				                        Rml::ScrollBehavior::Instant, Rml::ScrollParentage::Closest});
				update_view();
				if (displayed_values_[name] != text)
				{
					std::cerr << "Deferred readout did not refresh: " << name << std::endl;
					return false;
				}
				for (const auto &[parent, previous] : scroll)
					parent->SetScrollTop(previous);
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
	bool probe_ready()
	{
		auto *control = document_->GetElementById(id("ready"));
		if (!control || !app_)
			return false;
		auto ready = [&]()
		{
			for (const auto &member : app_->session->lobby().members)
				if (member.local_screen == route_)
					return member.ready;
			return false;
		};
		const bool original = ready();
		const auto at = control->GetAbsoluteOffset(Rml::BoxArea::Border);
		context_->ProcessMouseMove(static_cast<int>(at.x + control->GetOffsetWidth() / 2),
		                           static_cast<int>(at.y + control->GetOffsetHeight() / 2), 0);
		for (int frame = 0; frame < 5; ++frame)
		{
			context_->ProcessMouseWheel(0, 0);
			context_->ProcessMouseWheel(1, 0);
			context_->Update();
			if (ready() != original)
				return false;
		}
		context_->ProcessMouseButtonDown(0, 0);
		context_->ProcessMouseButtonUp(0, 0);
		if (ready() == original)
			return false;
		context_->ProcessMouseWheel(0, 0);
		context_->Update();
		if (ready() == original)
			return false;
		context_->ProcessMouseButtonDown(0, 0);
		context_->ProcessMouseButtonUp(0, 0);
		return ready() == original;
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
					action(id("settings"));
					local_settings_tab_ = "session";
					const auto size = context_->GetDimensions();
					prepare_local_settings(*app_,
					                       {route_, 0, 0, static_cast<float>(size.x), static_cast<float>(size.y)});
					profile_selected = action(id(descriptor_action(d.at("id"))));
					action(id("local-settings.close"));
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
	bool preparation_urgent() const
	{
		return prepare_dirty_ || !prepared_cache_;
	}
	void prepare(Application &app, const ConsoleRegion &physical_region, const std::set<std::string> &pads,
	             bool allow_fresh = true)
	{
		const auto source = app.session ? app.session->console_snapshot(route_) : nullptr;
		const auto environment = std::to_string(physical_region.x) + "/" + std::to_string(physical_region.y) + "/" +
		                         std::to_string(physical_region.width) + "/" + std::to_string(physical_region.height) +
		                         "/" + app.language + "/" + std::to_string(app.pseudo_locale) + "/" +
		                         std::to_string(text_scale_) + "/" + std::to_string(static_cast<int>(app.screen));
		if (prepared_owner_.lock() != app.session || !app.session ||
		    prepared_generation_ != app.session->generation() || !same_observation_scope(prepared_source_, source))
			render_urgent_ = true;
		const auto record_freshness = [&]
		{
			const auto tick = app.session ? app.session->ticks() : 0;
			app.frame_metrics["console/" + std::to_string(route_) + "/data-age"] =
			    prepared_source_ && tick >= prepared_source_->observed_tick
			        ? (tick - prepared_source_->observed_tick) * .05
			        : 0;
		};
		const bool stable = prepared_cache_ && !prepare_dirty_ && source && app.session && app.session->running() &&
		                    app.screen == Screen::console && prepared_owner_.lock() == app.session &&
		                    prepared_generation_ == app.session->generation() && prepared_environment_ == environment &&
		                    prepared_pads_ == pads && same_observation_scope(prepared_source_, source);
		if (stable &&
		    (source == prepared_source_ || (app.budget_console_preparation && (!allow_fresh || render_dirty_))))
		{
			restore_metadata(app);
			record_freshness();
			return;
		}
		prepare_current(app, physical_region, pads);
		remember_metadata(app);
		check_painted_controls();
		prepared_source_ = app.session ? app.session->console_snapshot(route_) : nullptr;
		prepared_owner_ = app.session;
		prepared_generation_ = app.session ? app.session->generation() : 0;
		prepared_environment_ = environment;
		prepared_pads_ = pads;
		record_freshness();
		prepared_cache_ = true;
		prepare_dirty_ = false;
		render_dirty_ = true;
	}
	bool paint_pending()
	{
		if (context_->GetNextUpdateDelay() <= 0)
			render_dirty_ = true;
		if (render_dirty_ && !render_dirty_since_)
			render_dirty_since_ = GetTime();
		return render_dirty_;
	}
	double paint_wait() const
	{
		return render_dirty_since_ ? GetTime() - render_dirty_since_ : 0;
	}
	double paint_cost() const
	{
		return last_paint_cost_;
	}
	void render_ui(UiRenderer &renderer, const ConsoleRegion &region, bool allow_paint = true)
	{
		if (app_)
			app_->frame_metrics["console/" + std::to_string(route_) + "/paint-age"] = paint_wait();
		const int width = static_cast<int>(region.width), height = static_cast<int>(region.height);
		if (!ui_texture_.id || ui_texture_.texture.width != width || ui_texture_.texture.height != height)
		{
			if (ui_texture_.id)
				UnloadRenderTexture(ui_texture_);
			ui_texture_ = LoadRenderTexture(width, height);
			render_dirty_ = render_urgent_ = true;
		}
		if (render_dirty_ && (allow_paint || render_urgent_))
		{
			const auto paint_started = GetTime();
			BeginTextureMode(ui_texture_);
			ClearBackground(BLANK);
			renderer.viewport({0, 0, region.width, region.height});
			context_->Render();
			renderer.finish();
			EndTextureMode();
			painted_controls_ = interactive_placements();
			painted_tick_ = prepared_source_ ? prepared_source_->observed_tick : 0;
			render_dirty_ = render_urgent_ = false;
			render_dirty_since_ = 0;
			last_paint_cost_ = GetTime() - paint_started;
		}
		if (app_ && app_->session)
		{
			const auto tick = app_->session->ticks();
			app_->frame_metrics["console/" + std::to_string(route_) + "/visible-data-age"] =
			    tick >= painted_tick_ ? (tick - painted_tick_) * .05 : 0;
		}
		BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
		DrawTexturePro(ui_texture_.texture, {0, 0, float(width), -float(height)},
		               {region.x, region.y, region.width, region.height}, {0, 0}, 0, WHITE);
		EndBlendMode();
	}
	void prepare_loading(const GameScreen::Loading *loading, const ConsoleRegion &region)
	{
		auto *panel = document_->GetElementById("station-loading");
		if (!panel)
		{
			auto node = document_->CreateElement("div");
			node->SetId("station-loading");
			panel = document_->AppendChild(std::move(node));
		}
		station_loading_ = loading != nullptr;
		const auto text =
		    loading ? localise(loading->phase) +
		                  (loading->total ? " " + std::to_string(loading->completed * 100 / loading->total) + "%" : "")
		            : std::string{};
		if (text != station_loading_markup_)
		{
			station_loading_markup_ = text;
			panel->SetInnerRML("<div><h1>" + Rml::StringUtilities::EncodeRml(localise("Loading Console")) +
			                   "</h1><p id=\"" + id("loading") + "\">" + Rml::StringUtilities::EncodeRml(text) +
			                   "</p></div>");
			render_dirty_ = render_urgent_ = true;
		}
		property(panel, "display", station_loading_ ? "block" : "none");
		if (station_loading_)
		{
			std::erase_if(app_->screen_controls,
			              [&](const auto &row)
			              {
				              return row.first.starts_with(id(""));
			              });
			std::erase_if(app_->screen_readouts,
			              [&](const auto &row)
			              {
				              return row.first.starts_with(id(""));
			              });
			app_->screen_readouts.emplace_back(id("loading"), localise("Loading Console") + ": " + text);
			std::erase_if(app_->screen_inputs,
			              [&](const auto &field)
			              {
				              return field.id.starts_with(id(""));
			              });
			context_->Update();
			place(*app_, region, id("loading"));
		}
	}
	void prepare_current(Application &app, const ConsoleRegion &physical_region, const std::set<std::string> &pads)
	{
		// Embedded Command uses the scale of its parent Viewscreen.
		const float display_scale = std::max(
		    1.f, std::min(physical_region.width * (route_ == 31 ? 4.f : 1.f) / 1280.f, physical_region.height / 720.f));
		context_->SetDensityIndependentPixelRatio(display_scale);
		const ConsoleRegion region{physical_region.console, physical_region.x, physical_region.y,
		                           physical_region.width / display_scale, physical_region.height / display_scale};
		app_ = &app;
		const auto preparation_started = GetTime();
		std::erase_if(leaf_markup_,
		              [](const auto &entry)
		              {
			              return !entry.second.element;
		              });
		const auto previous_announcement = model_.announcement();
		model_.refresh(app.session, route_);
		refresh_inline_console(app);
		document_->SetClass("integrated-helm", !inline_console_.empty());
		sync_console_browsing();
		app.frame_metrics["console/" + std::to_string(route_) + "/projection"] = GetTime() - preparation_started;
		const auto layout_started = GetTime();
		if (app.screen == Screen::settings)
		{
			cancel_pointer();
			return;
		}
		if (model_.announcement() != previous_announcement && !model_.announcement().empty())
			app.accessible_status = model_.announcement();
		if (owner_.lock() != app.session || (app.session && (generation_ != app.session->generation() ||
		                                                     content_packs_ != app.session->catalog().ordered_packs)))
		{
			surface_browsing_.clear();
			browsing_console_.clear();
			owner_ = app.session;
			content_packs_ =
			    app.session ? app.session->catalog().ordered_packs : std::vector<std::pair<std::string, std::string>>{};
			resolved_consoles_.clear();
			prepared_layout_ = nullptr;
			prepared_layout_revision_ = ~std::uint64_t{0};
			command_choices_.clear();
			command_language_.clear();
			command_markup_.clear();
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
		context_->SetDimensions({static_cast<int>(physical_region.width), static_cast<int>(physical_region.height)});
		select_language(local_language_, app.pseudo_locale);
		struct RestoreLanguage
		{
			Application &app;
			~RestoreLanguage()
			{
				select_language(app.language, app.pseudo_locale);
			}
		} restore_language{app};
		property(document_, "font-size",
		         std::to_string(
		             text_scale_ *
		             (active_theme_.contains("tokens") ? active_theme_.at("tokens").value("font_size", 20.f) : 20.f) /
		             100.f) +
		             "dp");
		if (settings_open_)
		{
			const bool operations = local_settings_tab_ == "logistics" || local_settings_tab_ == "docking" ||
			                        local_settings_tab_ == "away" || local_settings_tab_ == "craft" ||
			                        local_settings_tab_ == "personnel";
			document_->SetClass("station-page-open", operations);
			if (operations)
				prepare_console_tabs(region);
			prepare_local_settings(app, region);
			return;
		}
		const auto dispatch_map = model_.team_map();
		const bool team_dispatch = !dispatch_map.empty() && dispatch_map[0].value("dispatch", false);
		document_->SetClass("team-dispatch", team_dispatch);
		const bool pregame = app.session && app.session->lobby().active && !app.session->lobby().launched;
		const Json *layout = model_.layout();
		const bool command_console = layout && layout->value("command", false);
		document_->SetClass("command-console", command_console);
		document_->SetClass("narrow-command", command_console && region.width < 310.f);
		document_->SetClass("embedded-console", route_ == 31);
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
		const auto cards_started = GetTime();
		const auto cards = !pregame && (!helm || (layout && layout->contains("layout"))) && model_.observation()
		                       ? engineering_cards(*model_.observation())
		                       : std::vector<EngineeringCard>{};
		app.frame_metrics["console/" + std::to_string(route_) + "/card-model"] = GetTime() - cards_started;
		const bool engineering = !cards.empty();
		document_->SetClass("engineering", engineering);
		std::string title = "Console", description, briefing;
		std::string complexity;
		std::vector<std::string> complexity_choices;
		std::map<std::string, int> complexity_levels;
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
					complexity_levels = seat.profile_stars;
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
						{
							briefing = slot.briefing;
							for (const auto &spec : console_specs(app, slot.ship))
								if (spec.id == seat.id)
									layout = &app.session->catalog().consoles.at(spec.definition);
						}
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
		if (prepared_layout_ != layout || prepared_layout_revision_ != model_.layout_revision())
		{
			prepared_layout_ = layout;
			prepared_layout_revision_ = model_.layout_revision();
			++layout_identity_;
			command_language_.clear();
		}
		std::string signature = std::to_string(layout_identity_) + (app.pseudo_locale ? "pseudo" : "normal") +
		                        (pregame ? ready ? "ready" : "waiting" : "running") + (profile_open_ ? "profile" : "") +
		                        (profile_open_ ? "" : profile_name_) + "/complexity/" + complexity +
		                        (manual_toggle_ ? "/manual" : "") + (away_attention() ? "/away-attention" : "") +
		                        briefing;
		for (const auto &choice : complexity_choices)
			signature += "/" + choice;
		if (model_.observation())
			signature += "/" + std::to_string(model_.observation()->own.id) + "/" + model_.observation()->console;
		for (const auto &pad : pads)
			signature += "/pad/" + pad;
		if (signature != signature_)
		{
			restore_authored_components();
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
			if (toolbar_enabled_ && route_ != 31 && !pregame)
			{
				button("settings", "Settings");
				bool teams = false;
				if (model_.observation())
					for (const auto &t : model_.observation()->gameplay.value("teams", Json::array()))
						teams |= !t.at("orders").empty();
				toolbar += "<div style=\"position:absolute;right:0;top:0;\">" + button("systems", "Systems") +
				           (teams ? button("game.teams", "Teams") : std::string{}) + "</div>";
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
			{
				lobby = "<div class=\"ready-card\"><h1 id=\"" + id("lobby-title") + "\">" +
				        Rml::StringUtilities::EncodeRml(localise(title)) + "</h1><p>" +
				        Rml::StringUtilities::EncodeRml(localise(description)) + "</p><p>" +
				        Rml::StringUtilities::EncodeRml(localise(briefing)) + "</p><p>" +
				        Rml::StringUtilities::EncodeRml(
				            localise(ready ? "Ready. Waiting for the crew." : "Check your Console, then ready up.")) +
				        "</p>";
				if (!complexity_choices.empty() && complexity_levels.empty())
					lobby += "<p>" + localise("Complexity") + ": " +
					         Rml::StringUtilities::EncodeRml(localise(complexity)) + "</p>";
				for (const auto &choice : complexity_choices)
					lobby += star_choice("complexity/" + choice, choice, complexity_levels, complexity, false);
				lobby += button("ready", ready ? "Not Ready" : "Ready") + "<p id=\"countdown\"/></div>";
			}
			if (pregame)
			{
				lobby += button("settings", "Settings");
				button("profile", "Edit profile");
				const float tw = terminal_.value("display_width", 264.f), sw = terminal_.at("size").at(0),
				            sh = terminal_.at("size").at(1);
				const auto &aperture = terminal_.at("screen");
				const float ax = aperture.at("x"), ay = aperture.at("y"), aw = aperture.at("width"),
				            ah = aperture.at("height");
				lobby +=
				    "<button id=\"" + id("profile") + "\" class=\"profile-tv\" style=\"width:" + std::to_string(tw) +
				    "dp;height:" + std::to_string(tw * sh / sw) + "dp\"><img src=\"" +
				    asset_path("../menu/crt-terminal.png") + "\" width=\"" + std::to_string(tw) +
				    "\"/><span style=\"left:" + std::to_string(ax / sw * 100) +
				    "%;top:" + std::to_string((ay + ah * .85f) / sh * 100) +
				    "%;width:" + std::to_string(aw / sw * 100) + "%;height:" + std::to_string(ah * .15f / sh * 100) +
				    "%;background:#101918;font-size:0.8em\">" + Rml::StringUtilities::EncodeRml(profile_name_) +
				    "</span></button>";
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
			app.screen_readouts.emplace_back(id("lobby-title"),
			                                 localise(title) + ". " + localise(description) + " " + localise(briefing));
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
		app.frame_metrics["console/" + std::to_string(route_) + "/chrome"] = GetTime() - layout_started;
		const auto readouts_started = GetTime();
		auto gameplay_controls =
		    pregame ? std::vector<std::pair<std::string, std::string>>{} : model_.gameplay_controls();
		std::erase_if(gameplay_controls,
		              [](const auto &control)
		              {
			              return control.first.starts_with("game.console/") ||
			                     control.first.starts_with("game.status") ||
			                     control.first.starts_with("game.sensor-contact/") ||
			                     control.first.starts_with("game.combat-target/");
		              });
		if (!inline_console_.empty())
		{
			gameplay_controls = helm_weapon_controls();
		}
		std::map<std::string, ConsoleSystemStatus> status_controls;
		const auto owned_systems =
		    model_.observation() ? station_owned_systems(*model_.observation()) : std::set<std::string>{};
		if (!pregame)
			for (const auto &status : model_.system_statuses())
				if (owned_systems.contains(status.system))
					status_controls.emplace("game.status/" + status.system, status);
		std::erase_if(gameplay_controls,
		              [&](const auto &control)
		              {
			              return control.first.starts_with("game.status/") && !status_controls.contains(control.first);
		              });

		enum class ControlDestination
		{
			general,
			manual_flight,
			settings,
			camera,
			dial,
			profile,
			message,
			stance
		};
		std::set<std::pair<std::string, std::string>> dial_bindings;
		if (helm && layout)
			for (const auto &widget : layout->at("widgets"))
			{
				const auto kind = widget.at("kind").get<std::string>();
				if (kind == "throttle" || kind == "heading")
					dial_bindings.emplace(kind == "throttle" ? "speed_mm_s" : "heading_mdeg",
					                      model_.widget_system(widget.at("id")));
			}
		const bool manual_available = model_.manual_available();
		const bool manual_active = manual_available && model_.manual_active();
		const auto destination = [&](const Json &descriptor)
		{
			const auto action = descriptor.value("action", "");
			if (manual_available &&
			    (action == "assisted" || action == "manual_steering" || action == "longitudinal_thrust" ||
			     (manual_active && (action == "speed_mm_s" || action == "heading_mdeg" || action == "destination" ||
			                        action == "follow_contact" || action == "match_speed"))))
				return ControlDestination::manual_flight;
			if (action == "away")
				return ControlDestination::settings;
			if (action == "camera_mode" || action == "camera_direction")
				return ControlDestination::camera;
			if (action == "preset")
				return ControlDestination::profile;
			if (action.starts_with("message."))
				return ControlDestination::message;
			if (action == "stance.set")
				return ControlDestination::stance;
			const auto target = descriptor.value("controller", std::string{}).empty()
			                        ? descriptor.value("system", std::string{})
			                        : "controller/" + descriptor.at("controller").get<std::string>() + "/" +
			                              descriptor.at("channel").get<std::string>();
			return dial_bindings.contains({action, target}) ? ControlDestination::dial : ControlDestination::general;
		};
		std::set<std::string> specialised_controls, stance_controls, sensor_actions, objective_controls;
		std::vector<StanceChoice> choices;
		if (!pregame && model_.observation() && model_.observation()->gameplay.contains("controls"))
			for (const auto &descriptor : model_.observation()->gameplay.at("controls"))
			{
				const auto action = descriptor_action(descriptor.at("id"));
				if (descriptor.value("action", "") == "sensor_target")
					sensor_actions.insert(action);
				if (descriptor.value("action", "") == "objective.priority")
					objective_controls.insert(action);
				switch (destination(descriptor))
				{
				case ControlDestination::general:
					break;
				case ControlDestination::message:
					if (engineering)
						specialised_controls.insert(action);
					break;
				case ControlDestination::stance:
					if (command_console)
					{
						stance_controls.insert(action);
						choices.push_back({action, descriptor.at("system"), descriptor.at("label"),
						                   descriptor.value("selected", false)});
					}
					break;
				default:
					specialised_controls.insert(action);
					break;
				}
			}
		std::erase_if(gameplay_controls,
		              [&](const auto &control)
		              {
			              return specialised_controls.contains(control.first);
		              });
		readout_values_ = pregame ? std::vector<std::pair<std::string, std::string>>{} : model_.gameplay_readouts();
		if (!inline_console_.empty())
			readout_values_ = helm_target_readouts();
		if (model_.observation())
			for (const auto &system : model_.observation()->systems)
				if (!owned_systems.contains(system.id))
				{
					const auto key = "game.readout/system-" + descriptor_action(system.id).substr(13);
					std::erase_if(readout_values_,
					              [&](const auto &readout)
					              {
						              return readout.first == key;
					              });
				}
		if (route_ == 31)
			std::erase_if(readout_values_,
			              [](const auto &row)
			              {
				              return row.first == "alert";
			              });
		if (engineering)
			std::erase_if(readout_values_,
			              [](const auto &readout)
			              {
				              return readout.first.starts_with("game.readout/system-");
			              });
		const auto &gameplay_readouts = readout_values_;
		leaf_markup(document_->GetElementById("sensor-title"),
		            Rml::StringUtilities::EncodeRml(localise("Contact scan")));
		std::set<std::string> sensor_controls;
		for (const auto *section : {"sensor-controls", "sensor-readouts", "sensor-title"})
			property(document_->GetElementById(section), "width", std::to_string(region.width * .30f - 18) + "dp");
		std::string sensor_markup;
		for (const auto &[name, label] : gameplay_controls)
			if (sensor_actions.contains(name))
			{
				sensor_controls.insert(name);
				sensor_markup += "<button id=\"" + Rml::StringUtilities::EncodeRml(id(name)) + "\">" +
				                 Rml::StringUtilities::EncodeRml(label) + "</button>";
			}
		if (sensor_markup != sensor_controls_signature_)
		{
			elements_.clear();
			document_->GetElementById("sensor-controls")->SetInnerRML(sensor_markup);
			sensor_controls_signature_ = sensor_markup;
		}
		if (!(layout && layout->contains("layout")))
			property(document_->GetElementById("sensor-panel"), "display",
			         model_.sensor_contact() && !pregame &&
			                 (!sensor_controls.empty() || std::any_of(readout_values_.begin(), readout_values_.end(),
			                                                          [](const auto &r)
			                                                          {
				                                                          return r.first.starts_with(
				                                                              "game.scan-finding/");
			                                                          }))
			             ? "block"
			             : "none");
		std::set<std::string> engineering_controls;
		property(document_->GetElementById("engineering-cards"), "width",
		         std::to_string(region.width * .92f - 24) + "dp");
		std::string engineering_markup;
		for (const auto &card : cards)
		{
			const int columns = region.width >= 900 ? 2 : 1;
			const float card_width = (region.width * .92f - 40) / columns - 12;
			engineering_markup += "<div class=\"engineering-card\" style=\"width:" + std::to_string(card_width) +
			                      "dp\"><h2>" + Rml::StringUtilities::EncodeRml(localise(card.title)) +
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
		resource_readouts_.clear();
		for (const auto &card : cards)
		{
			const auto name = "resource/" + card.id;
			resource_readouts_.emplace_back(name, card.summary);
			update_readout(name, card.summary, false);
			app.screen_readouts.emplace_back(id(name), localise(card.title) + ". " + card.summary);
			for (const auto &[metric, value] : std::vector<std::pair<std::string, std::optional<int>>>{
			         {"heat", card.heat}, {"battery", card.battery}})
				if (value)
					property(element(id("gauge/" + card.id + "/" + metric)), "width", std::to_string(*value) + "%");
		}
		app.frame_metrics["console/" + std::to_string(route_) + "/readouts"] = GetTime() - readouts_started;
		const auto markup_started = GetTime();
		auto *gameplay = document_->GetElementById("gameplay");
		auto *widgets = document_->GetElementById("widgets");
		const auto messages = pregame ? std::vector<ConsoleMessageView>{} : model_.messages();
		const bool has_gameplay = !gameplay_controls.empty() || !gameplay_readouts.empty() || !messages.empty();
		const auto placement = signature_ + "/" + std::to_string(region.width) + "/" + std::to_string(region.height) +
		                       "/" + std::to_string(text_scale_) + (has_gameplay ? "/gameplay" : "/empty");
		const bool placement_changed = placement_signature_ != placement;
		if (placement_changed && !(layout && layout->contains("layout")))
		{
			placement_signature_ = placement;
			gameplay->SetProperty("display", has_gameplay ? "block" : "none");
			widgets->SetProperty("right", "4%");
			widgets->SetProperty("bottom", "4%");
			gameplay->SetProperty("left", "4%");
			gameplay->SetProperty("right", "4%");
			gameplay->SetProperty("top", "125dp");
			gameplay->SetProperty("bottom", "4%");
			gameplay->RemoveProperty("width");
			gameplay->SetProperty("height", std::to_string(std::max(80.f, region.height * .96f - 125)) + "dp");
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
						gameplay->SetProperty("left", std::to_string(region.width * x) + "dp");
						gameplay->SetProperty("top", std::to_string(125 + available_height * y) + "dp");
						gameplay->SetProperty("width", std::to_string(region.width * width) + "dp");
						gameplay->SetProperty("height", std::to_string(available_height * height) + "dp");
						gameplay->RemoveProperty("right");
						gameplay->RemoveProperty("bottom");
						placed_gameplay = true;
					}
				}
			}
			if (has_gameplay && !command_console && !placed_gameplay && layout && !layout->at("widgets").empty())
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
						        "dp");
						element->SetProperty("top", std::to_string((height - side) / 2) + "dp");
						element->SetProperty("width", std::to_string(side) + "dp");
						element->SetProperty("height", std::to_string(side) + "dp");
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
				gameplay->SetProperty("top", "100dp");
				gameplay->SetProperty("width", std::to_string(region.width * .92f) + "dp");
				gameplay->SetProperty("height", std::to_string(std::max(80.f, region.height * .96f - 100)) + "dp");
				gameplay->RemoveProperty("right");
				gameplay->RemoveProperty("bottom");
			}
			if (team_dispatch)
			{
				gameplay->SetProperty("left", "4%");
				gameplay->SetProperty("top", "100dp");
				gameplay->SetProperty("width", std::to_string(region.width * .92f) + "dp");
				gameplay->SetProperty("height", std::to_string(std::max(80.f, region.height - 140.f)) + "dp");
				gameplay->RemoveProperty("right");
				gameplay->RemoveProperty("bottom");
			}
			if (command_console)
			{
				const float left = route_ == 31 ? 16.f : 48.f;
				gameplay->SetProperty("left", std::to_string(left) + "dp");
				gameplay->SetProperty("top", "112dp");
				gameplay->SetProperty("width", std::to_string(region.width - left - 48.f) + "dp");
				gameplay->SetProperty("height", std::to_string(std::max(80.f, region.height - 224.f)) + "dp");
				gameplay->RemoveProperty("right");
				gameplay->RemoveProperty("bottom");
			}
		}
		if (layout && layout->contains("layout"))
			placement_signature_ = placement;

		Json gameplay_identity = Json::array();
		for (const auto &[name, label] : gameplay_controls)
			if (!engineering_controls.contains(name))
				gameplay_identity.push_back(name);
		const auto command_language = local_language_ + (app.pseudo_locale ? "/pseudo" : "/normal");
		if (choices != command_choices_ || command_language != command_language_)
		{
			std::map<std::string, std::string> groups, titles;
			if (!choices.empty())
				for (const auto &seat : console_specs(app, model_.observation()->own.definition))
					titles[seat.id] = app.session->catalog().consoles.at(seat.definition).value("name", seat.id);
			for (const auto &choice : choices)
				groups[choice.target] +=
				    "<button class=\"stance-button " + std::string(choice.selected ? "selected" : "") + "\" id=\"" +
				    id(choice.action) + "\"><span class=\"stance-label\">" +
				    Rml::StringUtilities::EncodeRml(localise(choice.label)) + "</span><span class=\"stance-state\">" +
				    (choice.selected ? Rml::StringUtilities::EncodeRml(localise("Selected")) : std::string("&nbsp;")) +
				    "</span></button>";
			command_markup_.clear();
			for (const auto &[target, buttons] : groups)
				command_markup_ +=
				    "<div class=\"command-card\"><h2>" +
				    Rml::StringUtilities::EncodeRml(localise(titles.contains(target) ? titles.at(target) : target)) +
				    "</h2><div class=\"stance-row\">" + buttons + "</div></div>";
			command_choices_ = std::move(choices);
			command_language_ = command_language;
		}
		std::string objective_markup;
		if (model_.observation() && model_.observation()->gameplay.contains("objective_board"))
		{
			objective_markup = "<div class=\"command-card objective-card\"><h2>" +
			                   Rml::StringUtilities::EncodeRml(localise("Objectives")) + "</h2>";
			for (const auto &[name, text] : gameplay_readouts)
				if (name.starts_with("game.objective/"))
					objective_markup += "<p id=\"" + Rml::StringUtilities::EncodeRml(id(name)) + "\">" +
					                    Rml::StringUtilities::EncodeRml(text) + "</p>";
			for (const auto &[name, label] : gameplay_controls)
				if (objective_controls.contains(name))
					objective_markup += "<button id=\"" + Rml::StringUtilities::EncodeRml(id(name)) + "\">" +
					                    Rml::StringUtilities::EncodeRml(label) + "</button>";
			objective_markup += "</div>";
		}
		const auto gameplay_signature = gameplay_identity.dump() + command_markup_ + objective_markup;
		if (gameplay_signature_ != gameplay_signature)
		{
			const auto focused = context_->GetFocusElement() ? context_->GetFocusElement()->GetId() : std::string{};
			elements_.clear();
			for (const auto &held : held_clicks_)
				model_.release_control(held.substr(id("").size()));
			held_clicks_.clear();
			std::string markup = objective_markup + command_markup_ +
			                     (!inline_console_.empty() ? "<h2>" + localise("Weapons") + "</h2>" : "");
			if (team_dispatch)
			{
				markup += "<h2>" + Rml::StringUtilities::EncodeRml(localise("Team dispatch")) + "</h2>";
				for (const auto &[name, label] : gameplay_controls)
					if (name.starts_with("game.team-order/") || name == "game.teams-close")
						markup += "<button id=\"" + Rml::StringUtilities::EncodeRml(id(name)) + "\">" +
						          Rml::StringUtilities::EncodeRml(label) + "</button>";
			}
			if (model_.observation())
			{
				const auto compartments = model_.team_map();
				std::set<std::string> decks;
				for (const auto &c : compartments)
					decks.insert(c.at("deck").get<std::string>());
				for (const auto &deck : decks)
				{
					std::int64_t minx = 1000000000, miny = 1000000000, maxx = -1000000000, maxy = -1000000000;
					for (const auto &c : compartments)
						if (c.at("deck") == deck)
						{
							const auto x = c.at("x_mm").get<std::int64_t>(), y = c.at("y_mm").get<std::int64_t>(),
							           hw = c.at("half_width_mm").get<std::int64_t>(),
							           hl = c.at("half_length_mm").get<std::int64_t>();
							minx = std::min(minx, x - hw);
							maxx = std::max(maxx, x + hw);
							miny = std::min(miny, y - hl);
							maxy = std::max(maxy, y + hl);
						}
					markup += "<h2>" + Rml::StringUtilities::EncodeRml(deck) +
					          "</h2><div "
					          "style=\"display:block;position:relative;width:100%;height:300dp;min-height:300dp;clear:"
					          "both;overflow:hidden;\">";
					for (const auto &c : compartments)
						if (c.at("deck") == deck)
						{
							const auto index = c.at("index").get<std::int64_t>();
							const auto x = c.at("x_mm").get<double>(), y = c.at("y_mm").get<double>(),
							           hw = c.at("half_width_mm").get<double>(),
							           hl = c.at("half_length_mm").get<double>();
							const auto name = "game.compartment/" + std::to_string(index);
							std::string label;
							for (const auto &control : gameplay_controls)
								if (control.first == name)
									label = control.second;
							markup +=
							    "<button id=\"" + Rml::StringUtilities::EncodeRml(id(name)) +
							    "\" style=\"position:absolute;left:" +
							    std::to_string(100 * (maxy - y - hl) / std::max<std::int64_t>(1, maxy - miny)) +
							    "%;top:" +
							    std::to_string(100 * (maxx - x - hw) / std::max<std::int64_t>(1, maxx - minx)) +
							    "%;width:" + std::to_string(100 * 2 * hl / std::max<std::int64_t>(1, maxy - miny)) +
							    "%;height:" + std::to_string(100 * 2 * hw / std::max<std::int64_t>(1, maxx - minx)) +
							    "%;margin:0;min-width:0;min-height:0;box-sizing:border-box;background:" +
							    (c.at("disabled").get<bool>() ? std::string("#702c2c") : std::string("#164244")) +
							    ";\">" + Rml::StringUtilities::EncodeRml(label) + "</button>";
						}
					markup += "</div>";
				}
			}

			for (const auto &[name, label] : gameplay_controls)
				if (!stance_controls.contains(name) && !engineering_controls.contains(name) &&
				    !objective_controls.contains(name) && !name.starts_with("game.compartment/") &&
				    name != "game.status-close" &&
				    !(team_dispatch && (name.starts_with("game.team-order/") || name == "game.teams-close")) &&
				    !sensor_controls.contains(name))
				{
					const bool status = status_controls.contains(name);
					const auto &visible = status ? status_controls.at(name).compact_label : label;
					markup += "<button id=\"" + Rml::StringUtilities::EncodeRml(id(name)) + "\"" +
					          (status ? " class=\"status-indicator\"" : "") + ">" +
					          Rml::StringUtilities::EncodeRml(visible) + "</button>";
				}
			document_->GetElementById("gameplay-controls")->SetInnerRML(markup);
			if (auto *previous = document_->GetElementById(focused))
				previous->Focus();
			gameplay_signature_ = gameplay_signature;
			for (const auto &[name, label] : gameplay_controls)
				displayed_values_.erase("control/" + name);
		}
		for (const auto &[name, label] : gameplay_controls)
			if (!stance_controls.contains(name) && !engineering_controls.contains(name) &&
			    displayed_values_["control/" + name] != label)
			{
				if (auto *element = this->element(id(name)))
					element->SetInnerRML(Rml::StringUtilities::EncodeRml(
					    status_controls.contains(name) ? status_controls.at(name).compact_label : label));
				displayed_values_["control/" + name] = label;
			}
		for (const auto &[name, status] : status_controls)
			if (auto *light = element(id(name)))
				property(light, "border-left", status.attention() ? "10dp #f08060" : "10dp #83be90");
		if (model_.observation())
			for (const auto &c : model_.team_map())
			{
				if (auto *compartment =
				        element(id("game.compartment/" + std::to_string(c.at("index").get<std::int64_t>()))))
					property(compartment, "background-color",
					         c.at("disabled").get<bool>()                                         ? "#702c2c"
					         : model_.selected_compartment() == c.at("index").get<std::int64_t>() ? "#486b56"
					                                                                              : "#164244");
			}
		gameplay_identity = Json::array();
		for (const auto &[name, text] : gameplay_readouts)
			gameplay_identity.push_back(name);
		const auto readout_signature = gameplay_identity.dump();
		if (gameplay_readout_signature_ != readout_signature)
		{
			elements_.clear();
			std::string markup, sensor_readouts;
			for (const auto &[name, text] : gameplay_readouts)
			{
				if (name.starts_with("game.objective/"))
					continue;
				(name.starts_with("game.scan-") ? sensor_readouts : markup) +=
				    "<p id=\"" + Rml::StringUtilities::EncodeRml(id(name)) + "\"></p>";
				displayed_values_.erase(name);
			}
			document_->GetElementById("gameplay-readouts")->SetInnerRML(markup);
			document_->GetElementById("sensor-readouts")->SetInnerRML(sensor_readouts);
			gameplay_readout_signature_ = readout_signature;
		}
		message_ui::refresh(*document_, *document_->GetElementById("gameplay-messages"), messages, app, id(""));
		refresh_conversations(app, region);
		for (const auto &[name, text] : gameplay_readouts)
		{
			update_readout(name, text, placement_changed || name.starts_with("game.scan-"));
			if (name.starts_with("game.readout/system-"))
				if (auto *readout = element(id(name)))
				{
					property(readout, "border-left",
					         text.find("[!]") != std::string::npos ? "8dp #f08060" : "8dp #83be90");
					property(readout, "padding-left", "8dp");
				}

			app.screen_readouts.emplace_back(id(name), text);
		}
		app.frame_metrics["console/" + std::to_string(route_) + "/markup"] = GetTime() - markup_started;
		const auto navigation_started = GetTime();
		prepare_navigation(app, region, pregame);
		prepare_manual(app, region, pregame);
		app.frame_metrics["console/" + std::to_string(route_) + "/navigation"] = GetTime() - navigation_started;
		if (!pregame)
		{
			const auto authored_started = GetTime();
			prepare_authored_layout(layout, region);
			fit_conversation_controls(region);
			app.frame_metrics["console/" + std::to_string(route_) + "/authored-layout"] = GetTime() - authored_started;
		}
		restore_console_browsing();
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
				if (auto *element = document_->GetElementById("widget/" + key); !element || !element->IsVisible(true))
					continue;
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
					const float width =
					    std::max(1.f, std::min(housing->GetOffsetWidth() - 16 * display_scale,
					                           (housing->GetOffsetHeight() - 50 * display_scale) * aspect));
					const float height = width / aspect;
					if (auto *panel = document_->GetElementById("camera-panel/" + key))
					{
						property(panel, "left", std::to_string((housing->GetOffsetWidth() - width) / 2) + "px");
						property(panel, "width", std::to_string(width) + "px");
						property(panel, "height", std::to_string(height) + "px");
					}
					if (auto *status = document_->GetElementById(id("camera-status/" + key)))
					{
						property(status, "top", std::to_string(height + 30 * display_scale) + "px");
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
					leaf_markup(readout, Rml::StringUtilities::EncodeRml(value));
					displayed_values_["value/" + key] = value;
				}
			}
		sharing_pods_.clear();
		if (helm && !pregame && layout && !layout->contains("layout"))
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
					const float pod_width =
					    std::clamp(r * .70f, 196.f * display_scale, 240.f * display_scale) * text_scale_ / 100.f;
					const float pod_height = 76.f * display_scale * text_scale_ / 100.f;
					const Rectangle pod{
					    std::min(cx + r * .34f, physical_region.width - pod_width - 24.f * display_scale),
					    std::min(cy + r * 1.02f, physical_region.height - pod_height - 24.f * display_scale), pod_width,
					    pod_height};
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
		const auto semantics_started = GetTime();
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
					const float width = std::clamp(toggle->GetParentNode()->GetOffsetWidth() * .34f,
					                               160.f * display_scale, 220.f * display_scale) *
					                    text_scale_ / 100.f;
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
			if (stance_controls.contains(name))
			{
				auto *choice = document_->GetElementById(id(name));
				app.screen_toggles[id(name)] = choice->IsClassSet("selected");
				choice->SetAttribute("aria-pressed", choice->IsClassSet("selected") ? "true" : "false");
			}
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
		if (layout && layout->contains("layout") && !pregame)
		{
			auto hidden = [&](const std::string &name)
			{
				if (!name.starts_with(id("")))
					return false;
				const auto *e = element(name);
				return !e || !e->IsVisible(true);
			};
			std::erase_if(app.screen_controls,
			              [&](const auto &entry)
			              {
				              return hidden(entry.first);
			              });
			std::erase_if(app.screen_readouts,
			              [&](const auto &entry)
			              {
				              return hidden(entry.first);
			              });
			std::erase_if(app.screen_inputs,
			              [&](const auto &entry)
			              {
				              return hidden(entry.id);
			              });
			if (auto *canvas = document_->GetElementById("navigation-canvas"); canvas && canvas->IsVisible(true))
			{
				const auto at = canvas->GetAbsoluteOffset(Rml::BoxArea::Border);
				navigation_area_ = {at.x, at.y, canvas->GetOffsetWidth(), canvas->GetOffsetHeight()};
			}
			else
				navigation_visible_ = false;
			if (auto *stick = document_->GetElementById("manual-stick"); stick && stick->IsVisible(true))
			{
				const auto at = stick->GetAbsoluteOffset(Rml::BoxArea::Border);
				joystick_area_ = {at.x, at.y, stick->GetOffsetWidth(), stick->GetOffsetHeight()};
			}
			else
				joystick_area_ = {};
		}
		app.frame_metrics["console/" + std::to_string(route_) + "/placements"] = GetTime() - semantics_started;
		if (!pregame && !settings_open_)
		{
			prepare_radar_choices(region);
			prepare_system_popup(region, cards);
		}
		app.frame_metrics["console/" + std::to_string(route_) + "/layout"] = GetTime() - layout_started;
	}
	void draw_profile(const ConsoleRegion &region)
	{
		if (!settings_open_ && app_->session->lobby().active && !app_->session->lobby().launched && !profile_open_ &&
		    !profile_avatar_.empty())
		{
			const auto key = sha256(profile_avatar_);
			const auto &portrait = crew_portraits_.contains(key) ? crew_portraits_.at(key) : profile_avatar_;
			const auto texture = radar_textures_.load(key + "/portrait", portrait);
			const float density = context_->GetDensityIndependentPixelRatio();
			const float tw = terminal_.value("display_width", 264.f) * density, sw = terminal_.at("size").at(0),
			            scale = tw / sw;
			const auto &screen = terminal_.at("screen");
			const float x = screen.at("x"), y = screen.at("y"), w = screen.at("width"), h = screen.at("height");
			const Rectangle area{region.x + region.width - 20 * density - tw + x * scale, region.y + y * scale,
			                     w * scale, h * .85f * scale};
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
		const auto background = active_theme_.contains("tokens")
		                            ? active_theme_.at("tokens").value("background", std::string("#080d0f"))
		                            : std::string("#080d0f");
		DrawRectangleRec(entire, GetColor(static_cast<unsigned>(std::stoul(
		                             background.substr(1) + (background.size() == 7 ? "ff" : ""), nullptr, 16))));
		if (active_theme_.contains("assets") && active_theme_.at("assets").contains("background"))
		{
			const auto ref = active_theme_.at("assets").at("background").get<std::string>();
			image(hud_cache_.load(ref, app_->session->catalog().assets.at(ref)), entire);
		}
		if (!(model_.layout() && model_.layout()->contains("layout")))
		{
			const bool command = model_.layout() && model_.layout()->value("command", false);
			if (command || route_ == 31)
			{
				const auto frame = hud("viewscreen-frame");
				// The embedded console shares the viewscreen's left bulkhead. Its top and
				// bottom rails continue behind that joint instead of adding a second border.
				const float overlap = route_ == 31 ? 112.f : 0.f;
				DrawTextureNPatch(frame,
				                  {{0, 0, static_cast<float>(frame.width), static_cast<float>(frame.height)},
				                   112,
				                   112,
				                   112,
				                   112,
				                   NPATCH_NINE_PATCH},
				                  {entire.x - overlap, entire.y, entire.width + overlap, entire.height}, {0, 0}, 0,
				                  WHITE);
			}
			else
			{
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
			}
		}

		const auto &view = model_.observation();
		const float ox = region.x, oy = region.y;
		std::vector<Rml::Element *> drawn_widgets;
		if (const auto *layout = model_.layout())
			for (const auto &definition : layout->at("widgets"))
				if (auto *widget = document_->GetElementById("widget/" + definition.at("id").get<std::string>());
				    widget && widget->IsVisible(true))
					drawn_widgets.push_back(widget);
		for (auto *widget : drawn_widgets)
		{
			const auto key = widget->GetId().substr(7);
			if (auto *dial = document_->GetElementById("dial/" + key))
			{
				const auto at = dial->GetAbsoluteOffset(Rml::BoxArea::Border);
				const float radius = std::min(dial->GetOffsetWidth(), dial->GetOffsetHeight()) * .5f;
				const Vector2 centre{ox + at.x + dial->GetOffsetWidth() / 2, oy + at.y + dial->GetOffsetHeight() / 2};
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
									{
										settings = w.value("sweep", Json::object());
										settings["fire_arcs"] =
										    w.value("fire_arcs", false) && model_.fire_arcs_visible();
									}
							settings["contact_arcs"] = model_.contact_arcs_visible();
							settings["sensor_contact"] = model_.sensor_contact();
							auto &sweep = radar_sweeps_[key];
							if (!sweep)
								sweep = std::make_unique<RadarSweep>();
							draw_radar_plot(radar_textures_, app_->session->catalog(), *view, sensor, model_.waypoint(),
							                *sweep, settings, centre, radius * .66f, GetTime(), app_->reduced_motion);
						}
				}
				if (sharing_pods_.contains(key))
				{
					auto pod = sharing_pods_.at(key);
					pod.x += ox;
					pod.y += oy;
					// Two mounting arms join the external pod to the casing, with their roots tucked behind its outer rim.
					for (const auto &[angle, end_x] :
					     std::array<std::pair<float, float>, 2>{{{65.f, pod.x + 25}, {48.f, pod.x + pod.width - 25}}})
					{
						const float radians = angle * 3.141592653589793f / 180;
						const Vector2 start{centre.x + std::cos(radians) * radius * .90f,
						                    centre.y + std::sin(radians) * radius * .90f};
						const Vector2 end{end_x, pod.y + 9};
						const float length = std::hypot(end.x - start.x, end.y - start.y);
						const float rotation = std::atan2(end.y - start.y, end.x - start.x) * 180 / 3.141592653589793f;
						// Reuse the readout's inked ivory rail, including its chipped paint and halftone.
						DrawTexturePro(hud("readout-panel"), {500, 119, 220, 52}, {start.x, start.y, length, 16},
						               {0, 8}, rotation, WHITE);
					}
				}
				image(hud("helm-radar-ring"), {centre.x - radius, centre.y - radius, radius * 2, radius * 2});
				if (sharing_pods_.contains(key))
				{
					const bool active = model_.is_on_screen(key);
					auto pod = sharing_pods_.at(key);
					pod.x += ox;
					pod.y += oy;
					// Use the same illustrated casing as the other live instrument readouts.
					DrawTexturePro(hud("readout-panel"), {24, 80, 2124, 555}, pod, {0, 0}, 0, WHITE);
					const Vector2 lamp{pod.x + pod.width - 31, pod.y + pod.height / 2};
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
				    maximum > minimum
				        ? std::clamp(static_cast<float>(model_.system_request(system) - minimum) / (maximum - minimum),
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
					    {track.x - track.width * .1f, y - track.width * .2f, track.width * 1.2f, track.width * .4f}, 2,
					    {255, 190, 94, 255});
			}
			if (auto *readout = document_->GetElementById("value/" + key);
			    readout && readout->GetClassNames() == "readout" && !readout->GetInnerRML().empty())
			{
				const auto at = readout->GetAbsoluteOffset(Rml::BoxArea::Border);
				image(hud("readout-panel"),
				      {ox + at.x - 8, oy + at.y - 8, readout->GetOffsetWidth() + 16, readout->GetOffsetHeight() + 16});
			}
		}
		draw_navigation(ox, oy);
		EndScissorMode();
	}
};
} // namespace phoenix
