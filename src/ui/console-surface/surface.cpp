module;
#include <RmlUi/Core.h>
#include <RmlUi/Core/StyleSheetContainer.h>
#include <RmlUi/Core/StreamMemory.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/StringUtilities.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.ui.console_surface;
import phoenix.content.assets;
import phoenix.content.controllers;
import phoenix.content.controllers.types;
import phoenix.content.packs;
import phoenix.content.ui;

import phoenix.graphics.assets;
import phoenix.graphics.radar;
import phoenix.graphics.scene;
import phoenix.ui.theme;
import phoenix.ui.engineering_widgets;
import phoenix.ui.information_widgets;
import phoenix.ui.operations_widgets;
import phoenix.ui.tactical_widgets;
import phoenix.ui.flight_widgets;
import phoenix.application.preferences;
import phoenix.devices;
#include "surface_state.hpp"
namespace phoenix
{
bool ConsoleSurface::State::same_observation_scope(const Observation &left, const Observation &right)
{
	if (left.own.id != right.own.id || left.own.definition != right.own.definition || left.console != right.console ||
	    left.controls != right.controls || left.feeds != right.feeds ||
	    left.additional_consoles.size() != right.additional_consoles.size())
		return false;
	for (std::size_t i = 0; i < left.additional_consoles.size(); ++i)
		if (!same_observation_scope(left.additional_consoles[i], right.additional_consoles[i]))
			return false;
	return true;
}
bool ConsoleSurface::State::same_observation_scope(const std::shared_ptr<const Observation> &left,
                                                   const std::shared_ptr<const Observation> &right)
{
	return left && right ? same_observation_scope(*left, *right) : !left && !right;
}
void ConsoleSurface::State::remember_metadata(const Application &app)
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
std::map<std::string, Application::ControlPlacement> ConsoleSurface::State::interactive_placements() const
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
void ConsoleSurface::State::check_painted_controls()
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
void ConsoleSurface::State::restore_metadata(Application &app)
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
std::vector<std::int64_t> ConsoleSurface::State::display_camera(const std::string &widget) const
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
std::optional<std::string> ConsoleSurface::State::manual_preset(bool automatic) const
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
void ConsoleSurface::State::refresh_conversations(Application &app, const ConsoleRegion &region)
{
	auto *root = document_->GetElementById("comms-layer");
	comms_widget_.clear();
	if (model_.layout())
		for (const auto &w : model_.layout()->at("widgets"))
			if (w.at("kind") == "conversations" && ui_widget_available(w, model_.ui_availability()))
				comms_widget_ = w.at("id");
	const auto board = model_.conversations();
	const bool active = (!comms_widget_.empty() || (model_.layout() && model_.layout()->contains("layout"))) &&
	                    !board.empty() && app.screen != Screen::server_lobby;
	const bool authored = model_.layout() && model_.layout()->contains("layout");
	if (!authored)
		property(root, "display", active ? "block" : "none");
	if (!active)
		return;
	const auto board_signature = board.dump();
	if (board_signature != comms_board_signature_)
	{
		elements_.clear();
		properties_.clear();
		comms_board_signature_ = board_signature;
	}
	if (!authored)
	{
		property(document_->GetElementById("gameplay"), "display", "none");
		property(document_->GetElementById("widgets"), "display", "none");
	}
	const auto escape = [](const std::string &s)
	{
		return Rml::StringUtilities::EncodeRml(s);
	};
	const auto button = [&](const std::string &key, const std::string &label)
	{
		comms_controls_.emplace_back(key, label);
		return "<button id=\"" + escape(id(key)) + "\">" + escape(label) + "</button>";
	};
	auto *compose = document_->GetElementById("comms-compose");
	if (!compose->HasChildNodes())
		compose->SetInnerRML("<input id=\"" + id("comms/draft") + "\" type=\"text\" maxlength=\"4096\"/><button id=\"" +
		                     id("comms/send") + "\">" + escape(localise("Send")) + "</button>");
	comms_controls_.clear();
	std::string listing;
	for (const auto &thread : board.at("threads"))
	{
		const auto key = thread.at("id").get<std::uint64_t>();
		auto title = localise(thread.at("title").get<std::string>());
		if (thread.value("unread", false))
			title = "* " + title;
		listing += button("comms/select/" + std::to_string(key), title);
	}
	listing += "<h3>" + escape(localise("Hail")) + "</h3>";
	for (const auto &p : board.at("directory"))
		if (p.value("available", false) && p.at("id") != "ship:" + std::to_string(model_.observation()->own.id))
			listing += button("comms/hail/" + p.at("id").get<std::string>(),
			                  localise(p.at("name").get<std::string>()) + " (#" +
			                      std::to_string(p.at("host").get<EntityId>()) + ")");
	if (listing != comms_list_signature_)
	{
		elements_.clear();
		properties_.clear();
		const auto focused = context_->GetFocusElement() ? context_->GetFocusElement()->GetId() : std::string{};
		document_->GetElementById("comms-list")->SetInnerRML(listing);
		comms_list_signature_ = listing;
		if (!focused.empty())
			if (auto *element = document_->GetElementById(focused))
				element->Focus();
	}
	const auto selected = board.value("selected", std::uint64_t{0});
	const Json *thread = nullptr;
	for (const auto &v : board.at("threads"))
		if (v.at("id") == selected)
			thread = &v;
	const bool split =
	    authored && std::any_of(authored_resolved_.placements.begin(), authored_resolved_.placements.end(),
	                            [](const auto &p)
	                            {
		                            return p.component.starts_with("conversation-");
	                            });
	const auto panel_width =
	    authored ? std::max(100.f, root->GetClientWidth() / context_->GetDensityIndependentPixelRatio()) : region.width;
	const bool narrow = !split && panel_width < 700;
	const bool detail_visible = split ? authored_component_visible("conversation-history")
	                                  : (!authored || authored_component_visible("conversations")) &&
	                                        (!narrow || (thread && !comms_list_only_));
	if (!split)
	{
		property(document_->GetElementById("comms-list"), "width", narrow ? "100%" : "30%");
		property(document_->GetElementById("comms-list"), "display",
		         narrow && thread && !comms_list_only_ ? "none" : "block");
		auto *conversation = document_->GetElementById("comms-conversation");
		property(conversation, "left", narrow ? "0%" : "32%");
		property(conversation, "width", narrow ? "100%" : "68%");
		property(conversation, "display", narrow && (!thread || comms_list_only_) ? "none" : "block");
	}
	const std::string width = "100%";
	std::string header = "<div style=\"display:block;width:" + width + ";height:56dp;\">" +
	                     button("comms/back", localise("Conversations")) +
	                     button("comms/screen", localise("On screen")) + "</div>";
	if (thread)
	{
		header += "<div style=\"display:block;width:" + width + ";font-size:1.1em;margin:4dp 0;\">" +
		          escape(localise(thread->at("title").get<std::string>())) + "</div>";
		header += "<div style=\"display:block;width:" + width + ";\">";
		bool first = true;
		for (const auto &p : thread->at("members"))
		{
			header += (first ? "" : " · ") + escape(localise(p.at("name").get<std::string>())) + " — " +
			          escape(localise(p.at("connected").get<bool>() ? "Connected" : "Out of contact"));
			first = false;
		}
		header += "</div>";
	}
	auto *head = document_->GetElementById("comms-header");
	if (!authored)
		property(head, "width", width);
	if (comms_header_signature_ != header)
	{
		elements_.clear();
		properties_.clear();
		head->SetInnerRML(header);
		comms_header_signature_ = header;
	}
	std::vector<ConsoleMessageView> messages;
	if (thread)
		for (const auto &m : thread->at("messages"))
		{
			messages.push_back(message_ui::conversation_message(
			    m, "comms/message/" + std::to_string(selected) + "/" + std::to_string(m.at("id").get<std::uint64_t>()),
			    app.session->catalog()));
		}
	auto *history = document_->GetElementById("comms-history");
	const bool at_bottom = history->GetScrollTop() + history->GetClientHeight() >= history->GetScrollHeight() - 8;
	const auto readout_count = app.screen_readouts.size();
	message_ui::refresh(*document_, *history, messages, app.session ? &app.session->catalog() : nullptr,
	                    app.screen_readouts, id(""), true);

	if (thread)
		for (const auto &message : thread->at("messages"))
			if (auto *row = document_->GetElementById(id("comms/message/" + std::to_string(selected) + "/" +
			                                             std::to_string(message.at("id").get<std::uint64_t>()))))
			{
				property(row, "width", "100%");
				property(row, "box-sizing", "border-box");
				property(row, "margin-left", "0dp");
				property(row, "margin-right", "0dp");
			}

	if (!detail_visible)
		app.screen_readouts.resize(readout_count);
	if (detail_visible && thread && !thread->at("messages").empty())
	{
		const auto latest = thread->at("messages").back().at("id").get<std::uint64_t>();
		if (comms_read_thread_ != selected || comms_read_message_ != latest)
		{
			if (model_.conversation_action("select", selected))
			{
				comms_read_thread_ = selected;
				comms_read_message_ = latest;
			}
		}
	}
	if (at_bottom || comms_visible_thread_ != selected)
		history->SetScrollTop(history->GetScrollHeight());
	comms_visible_thread_ = selected;
	std::string incoming;
	if (thread)
		for (const auto &p : thread->at("incoming"))
			incoming += localise(p.at("name").get<std::string>()) + ": " + localise("Incoming message") +
			            (app.reduced_motion ? "..." : std::string(1 + static_cast<int>(GetTime() * 2) % 3, '.')) + " ";
	if (displayed_values_["comms-incoming"] != incoming)
	{
		document_->GetElementById("comms-incoming")->SetInnerRML(escape(incoming));
		displayed_values_["comms-incoming"] = incoming;
	}
	std::string choices;
	if (thread)
	{
		std::set<std::uint64_t> prompts;
		for (const auto &c : thread->at("choices"))
			prompts.insert(c.value("prompt", thread->at("prompt")).get<std::uint64_t>());
		std::uint64_t previous = 0;
		for (const auto &c : thread->at("choices"))
		{
			const auto prompt = c.value("prompt", thread->at("prompt")).get<std::uint64_t>();
			if (prompts.size() > 1 && prompt != previous)
			{
				choices += "<h3>" + escape(localise(c.value("question", std::string{}))) + "</h3>";
				previous = prompt;
			}
			const auto key = prompts.size() > 1
			                     ? "comms/reply/" +
			                           std::to_string(c.value("conversation", thread->at("id")).get<std::uint64_t>()) +
			                           "/" + std::to_string(prompt) + "/" + c.at("id").get<std::string>()
			                     : "comms/answer/" + c.at("id").get<std::string>();
			if (c.value("enabled", true))
				choices += button(key, localise(c.at("text").get<std::string>()));
			else
				choices += "<p>" + escape(localise(c.at("text")) + " — " + localise(c.value("reason", "Unavailable"))) +
				           "</p>";
		}
	}
	if (choices != comms_choice_signature_)
	{
		elements_.clear();
		properties_.clear();
		document_->GetElementById("comms-choices")->SetInnerRML(choices);
		comms_choice_signature_ = choices;
	}
	const bool text = (split ? authored_component_visible("conversation-compose") : detail_visible) && thread &&
	                  thread->value("text_allowed", false);
	if (!authored)
		property(document_->GetElementById("comms-compose"), "display", text ? "block" : "none");
	if (text)
	{
		comms_controls_.emplace_back("comms/send", localise("Send"));
		if (auto *field = dynamic_cast<Rml::ElementFormControl *>(document_->GetElementById(id("comms/draft"))))
		{
			app.screen_inputs.push_back({id("comms/draft"), localise("Message"), field->GetValue(), false});
			place(app, region, id("comms/draft"));
		}
	}
	for (const auto &[key, label] : comms_controls_)
	{
		const bool list_control = key.starts_with("comms/select/") || key.starts_with("comms/hail/");
		if (narrow && (list_control == detail_visible))
			continue;
		app.screen_controls.emplace_back(id(key), label);
		place(app, region, id(key));
	}
	app.screen_toggles[id("comms/screen")] = model_.is_on_screen(comms_widget_);
	if (auto *screen = document_->GetElementById(id("comms/screen")))
	{
		screen->SetClass("on-screen-active", model_.is_on_screen(comms_widget_));
		screen->SetAttribute("aria-pressed", model_.is_on_screen(comms_widget_) ? "true" : "false");
	}

	if (detail_visible && !incoming.empty())
		app.screen_readouts.emplace_back("comms-incoming", localise("Incoming message"));
}
// Fit controls after the authored slots have their measured pixel geometry.
// Nested percentage widths can shrink to their intrinsic minimum on a density change.
void ConsoleSurface::State::fit_conversation_controls(const ConsoleRegion &region)
{
	if (!model_.layout() || !model_.layout()->contains("layout") || !authored_component_visible("conversation-history"))
		return;
	auto *history = document_->GetElementById("comms-history");
	const float width = history->GetClientWidth() / context_->GetDensityIndependentPixelRatio();
	const auto units = [](float value)
	{
		return std::to_string(std::max(0.f, value)) + "dp";
	};
	for (const auto *name : {"comms-header", "comms-choices", "comms-incoming", "comms-compose"})
	{
		auto *panel = document_->GetElementById(name);
		property(panel, "width", units(width));
		property(panel, "box-sizing", "border-box");
	}
	auto *header = document_->GetElementById("comms-header");
	for (int i = 0; i < header->GetNumChildren(); ++i)
		property(header->GetChild(i), "width", units(width));
	Rml::ElementList buttons;
	header->GetElementsByTagName(buttons, "button");
	for (auto *button : buttons)
		property(button, "width", units(std::min(190.f, (width - 24) / 2)));
	buttons.clear();
	document_->GetElementById("comms-choices")->GetElementsByTagName(buttons, "button");
	for (auto *button : buttons)
		property(button, "width", units(width));
	if (auto *draft = document_->GetElementById(id("comms/draft")))
		property(draft, "width", units(width - 140));
	context_->Update();
	elements_.clear();
	for (const auto &[name, label] : comms_controls_)
		place(*app_, region, id(name));
	place(*app_, region, id("comms/draft"));
}
const std::vector<ConsoleSpec> &ConsoleSurface::State::console_specs(const Application &app, const std::string &ship)
{
	const auto found = resolved_consoles_.find(ship);
	if (found != resolved_consoles_.end())
		return found->second;
	return resolved_consoles_.emplace(ship, resolve_consoles(app.session->catalog(), ship)).first->second;
}
void ConsoleSurface::State::measure_readouts()
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
				const auto limit = name.starts_with("resource/") ? viewport_bottom : std::min(viewport_bottom, bottom);
				if (at.y + target->GetOffsetHeight() >= top && at.y <= limit)
					visible_readouts_.insert(name);
			}
	};
	measure(readout_values_);
	measure(resource_readouts_);
}
bool ConsoleSurface::State::update_readout(const std::string &name, const std::string &text, bool force)
{
	auto *target = element(id(name));
	if (!target || displayed_values_[name] == text)
		return false;
	// Keep measured offscreen heights; accessible values are published separately.
	if (!force && !displayed_values_[name].empty() && !visible_readouts_.contains(name))
		return false;
	const auto encoded = Rml::StringUtilities::EncodeRml(text);
	if (auto *leaf = target->GetNumChildren() == 1 ? dynamic_cast<Rml::ElementText *>(target->GetChild(0)) : nullptr)
		leaf->SetText(encoded);
	else
		target->SetInnerRML(encoded);
	displayed_values_[name] = text;
	return true;
}
void ConsoleSurface::State::leaf_markup(Rml::Element *target, const std::string &text)
{
	auto &previous = leaf_markup_[target];
	if (previous.element.get() == target && previous.text == text)
		return;
	target->SetInnerRML(text);
	previous = {target->GetObserverPtr(), text};
}
void ConsoleSurface::State::property(Rml::Element *target, const std::string &name, const std::string &value)
{
	auto &previous = properties_[{target, name}];
	if (previous != value)
	{
		target->SetProperty(name, value);
		previous = value;
	}
}
Rml::Element *ConsoleSurface::State::element(const std::string &name)
{
	const auto found = elements_.find(name);
	if (found != elements_.end())
		return found->second;
	return elements_.emplace(name, document_->GetElementById(name)).first->second;
}
void ConsoleSurface::State::return_authored_components()
{
	// Restore parents before legacy generators replace their children.
	if (!authored_parents_.empty())
		properties_.clear();
	for (const auto &[target, parent_id] : authored_parents_)
	{
		auto *component = authored_component(target);
		auto *parent = document_->GetElementById(parent_id);
		if (component && parent && component->GetParentNode() != parent)
			parent->AppendChild(component->GetParentNode()->RemoveChild(component));
		if (component)
			for (const char *field :
			     {"left", "top", "width", "height", "display", "position", "overflow", "pointer-events", "right",
			      "bottom", "min-width", "min-height", "max-width", "max-height", "box-sizing", "flex"})
				component->RemoveProperty(field);
	}
	authored_parents_.clear();
}
void ConsoleSurface::State::restore_authored_components()
{
	return_authored_components();
	document_->GetElementById("authored-body")->SetInnerRML("");
	authored_dom_.clear();
	authored_program_.reset();
	authored_resolved_ = UiResolved{};
	authored_identity_.clear();
	authored_geometry_.clear();
}
void ConsoleSurface::State::apply_console_theme(const Json *layout)
{
	if (!app_->session)
		return;
	const auto &catalog = app_->session->catalog();
	if (catalog.ui_theme.empty() && (!layout || (!layout->contains("layout") && !layout->contains("theme") &&
	                                             !layout->contains("theme_overrides"))))
	{
		theme_renderer_.reset(*document_);
		active_theme_ = Json::object();
		theme_identity_.clear();
		return;
	}
	const auto theme_name = layout ? layout->value("theme", std::string{}) : std::string{};
	const auto overrides = layout ? layout->value("theme_overrides", Json::object()) : Json::object();
	const auto signature = theme_name + overrides.dump() + std::to_string(generation_);
	if (signature == theme_identity_)
		return;
	theme_identity_ = signature;
	try
	{
		active_theme_ = resolved_ui_theme(catalog, theme_name, overrides);
		theme_renderer_.apply(*document_, catalog, theme_name, overrides);
	}
	catch (const std::exception &error)
	{
		app_->accessible_status = error.what();
	}
}
void ConsoleSurface::State::prepare_console_tabs(const ConsoleRegion &region)
{
	station_pages_.clear();
	if (station_program_owner_.lock() != app_->session)
	{
		station_programs_.clear();
		station_program_owner_ = app_->session;
	}
	std::string markup;
	const auto snapshot = app_->session->console_snapshot(route_);
	if (!snapshot)
		return;
	const auto &catalog = app_->session->catalog();
	const auto specs = resolve_consoles(catalog, snapshot->own.definition);
	for (const auto &tab : model_.available_tabs())
	{
		const Observation *view = snapshot.get();
		for (const auto &extra : snapshot->additional_consoles)
			if (extra.console == tab.id)
				view = &extra;
		const Json *definition = nullptr;
		for (const auto &spec : specs)
			if (spec.id == tab.id)
				definition = &catalog.consoles.at(spec.definition);
		if (!definition || (!definition->value("tab", true) && tab.id != snapshot->console))
			continue;
		auto add = [&](const std::string &group, const std::string &page, const std::string &label,
		               const std::string &component)
		{
			station_pages_.push_back({tab.id, group, page,
			                          localise(label) + (tab.attention ? " — " + localise("Unread") : ""), component,
			                          "console-page/" + tab.id + "/" + page});
		};
		if (definition->contains("layout"))
		{
			const auto key = definition->at("id").get<std::string>();
			// Group and layout assets may change without changing the Console definition in Workshop.
			const auto signature = std::to_string(app_->session->generation()) + definition->dump();
			if (!station_programs_.contains(key) || station_programs_.at(key).first != signature)
				station_programs_[key] = {signature, compile_ui_layout(catalog, *definition)};
			auto program = available_ui_layout(station_programs_.at(key).second, console_ui_availability(*view));
			const auto resolved =
			    resolve_console_ui(program, region.width, region.height, text_scale_ / 100.f, authored_pages_, true);
			const auto &root = resolved.root;
			if (root.component.empty() && root.children.empty())
				continue;
			if (root.kind == "tabs")
				for (const auto &page : root.children)
					add(root.id, page.id, page.label, page.component);
			else
				add("", "", tab.label, root.component);
		}
		else
			add("", "", tab.label, "");
	}
	for (const auto &page : station_pages_)
	{
		const auto label = localise(page.label);
		const bool selected = !settings_open_ && model_.observation()->console == page.console &&
		                      (page.group.empty() ||
		                       (authored_pages_.contains(page.group) && authored_pages_.at(page.group) == page.page));
		const auto action = id(page.action);
		markup += "<button id=\"" + Rml::StringUtilities::EncodeRml(action) + "\" class=\"" +
		          (selected ? "selected" : "") + "\">" + Rml::StringUtilities::EncodeRml(label) + "</button>";
		app_->screen_controls.emplace_back(action, label);
		app_->screen_toggles[action] = selected;
	}
	if (!settings_open_)
		markup += authored_tabs_markup_;
	if (markup != console_tabs_markup_)
	{
		elements_.clear();
		document_->GetElementById("console-tabs")->SetInnerRML(markup);
		console_tabs_markup_ = markup;
	}
	for (const auto &page : station_pages_)
		place(*app_, region, id(page.action));
}
void ConsoleSurface::State::build_authored_tree(const UiNode &root)
{
	return_authored_components();
	std::function<std::string(const UiNode &)> markup = [&](const UiNode &node)
	{
		std::string result =
		    "<div id=\"" + Rml::StringUtilities::EncodeRml(layout_node_id(node)) + "\" class=\"layout-node\">";
		for (const auto &child : node.children)
			result += markup(child);
		return result + "</div>";
	};
	document_->GetElementById("authored-body")->SetInnerRML(markup(root));
	if (authored_program_ && authored_program_->header)
	{
		document_->GetElementById("authored-header")->SetInnerRML(markup(*authored_program_->header));
		authored_header_resolved_ =
		    resolve_ui_layout(UiLayout{*authored_program_->header, {}, {}}, {0, 0, 1280, 56}, 1);
	}
	auto placements = authored_resolved_.placements;
	placements.insert(placements.end(), authored_header_resolved_.placements.begin(),
	                  authored_header_resolved_.placements.end());
	for (const auto &placement : placements)
		if (auto *component = authored_component(placement.component))
		{
			authored_parents_[placement.component] = component->GetParentNode()->GetId();
			auto *slot = document_->GetElementById("layout-node/" + placement.id);
			slot->AppendChild(component->GetParentNode()->RemoveChild(component));
			for (const char *field : {"right", "bottom"})
				component->RemoveProperty(field);
		}
}
void ConsoleSurface::State::style_authored_tree(const UiNode &node, const UiNode *parent, bool visible)
{
	auto *box = document_->GetElementById(layout_node_id(node));
	const auto unit = [&](float value)
	{
		return std::to_string(value * text_scale_ / 100.f) + "dp";
	};
	if (node.component == "manual-flight" && !model_.manual_available())
		visible = false;
	property(box, "display", visible ? "flex" : "none");
	property(box, "position",
	         !parent || parent->kind == "instruments" || parent->kind == "cutout" ? "absolute" : "relative");
	property(box, "min-width", unit(node.minimum_width));
	property(box, "min-height", unit(node.minimum_height));
	property(box, "flex-direction", node.kind == "row" || node.kind == "wrap" ? "row" : "column");
	property(box, "flex-wrap", node.kind == "wrap" ? "wrap" : "nowrap");
	property(box, "gap", unit(node.gap));
	property(box, "padding",
	         unit(node.children.empty() || node.kind == "instruments" || node.kind == "cutout" ? 0 : node.padding));
	property(box, "align-self", node.align == "start" ? "flex-start" : node.align == "end" ? "flex-end" : node.align);
	property(box, "pointer-events", node.component == "manual-flight" ? "none" : "auto");
	if (!parent)
	{
		property(box, "left", "0dp");
		property(box, "top", "0dp");
		property(box, "width", "100%");
		property(box, "height", "100%");
	}
	else if (parent->kind != "instruments" && parent->kind != "cutout")
	{
		const bool row = parent->kind == "row" || parent->kind == "wrap";
		const auto basis = row ? std::max(node.minimum_width, node.preferred_width)
		                       : std::max(node.minimum_height, node.preferred_height);
		property(box, "flex", std::to_string(parent->kind == "wrap" ? 0.f : node.stretch) + " 0 " + unit(basis));
		property(box, "width",
		         parent->kind == "wrap"            ? unit(std::max(node.minimum_width, node.preferred_width))
		         : !row && node.align == "stretch" ? "100%"
		                                           : "auto");
		property(box, "height",
		         parent->kind == "wrap" ? unit(std::max(node.minimum_height, node.preferred_height)) : "auto");
	}
	if (!node.component.empty())
		if (auto *component = authored_component(node.component))
		{
			property(component, "display", visible ? "block" : "none");
			property(component, "flex", "1 1 0dp");
			if (node.aspect <= 0)
			{
				property(component, "position", "relative");
				property(component, "left", "0dp");
				property(component, "top", "0dp");
				property(component, "width", "100%");
				property(component, "height", "100%");
			}
			property(component, "box-sizing", "border-box");
			property(component, "max-width", "none");
			property(component, "max-height", "none");
			property(component, "min-width", "0dp");
			property(component, "min-height", "0dp");
			property(component, "overflow", "auto");
			property(component, "pointer-events", node.component == "manual-flight" ? "none" : "auto");
			for (const auto &[key, value] : node.style.items())
				if (key == "background" || key == "text" || key == "font_family")
					property(component,
					         key == "background" ? "background-color"
					         : key == "text"     ? "color"
					                             : "font-family",
					         value.get<std::string>());
			if (node.style.contains("font_size"))
				property(component, "font-size", unit(node.style.at("font_size").get<float>()));
		}
	for (const auto &child : node.children)
		style_authored_tree(child, &node, visible && (node.kind != "tabs" || authored_pages_.at(node.id) == child.id));
}
void ConsoleSurface::State::place_authored_instruments(const UiNode &node)
{
	auto *box = document_->GetElementById(layout_node_id(node));
	if (node.kind == "instruments" || node.kind == "cutout")
	{
		const float density = context_->GetDensityIndependentPixelRatio();
		const float pad = node.padding * text_scale_ / 100.f;
		const float width = std::max(1.f, box->GetClientWidth() / density - pad * 2),
		            height = std::max(1.f, box->GetClientHeight() / density - pad * 2);
		for (const auto &child : node.children)
		{
			UiRect bounds{width * child.rect.x / 1000, height * child.rect.y / 1000, width * child.rect.width / 1000,
			              height * child.rect.height / 1000};
			if (node.kind == "cutout" && !child.region.empty())
			{
				const float x = width * node.circle_x, y = height * node.circle_y;
				const float radius = std::min(width, height) * node.circle_radius, corner = radius / std::sqrt(2.f);
				const bool right = child.region.ends_with("right"), bottom = child.region.starts_with("bottom");
				bounds = child.region == "centre" ? UiRect{x - radius, y - radius, radius * 2, radius * 2}
				                                  : UiRect{right ? x + corner : 0, bottom ? y + corner : 0,
				                                           right ? width - x - corner : x - corner,
				                                           bottom ? height - y - corner : y - corner};
			}
			auto *slot = document_->GetElementById(layout_node_id(child));
			for (const auto &[field, value] : std::map<std::string, float>{{"left", pad + bounds.x},
			                                                               {"top", pad + bounds.y},
			                                                               {"width", bounds.width},
			                                                               {"height", bounds.height}})
				property(slot, field, std::to_string(value) + "dp");
		}
	}
	for (const auto &child : node.children)
		place_authored_instruments(child);
}
void ConsoleSurface::State::fit_authored_aspects(const UiNode &node)
{
	if (node.aspect > 0 && !node.component.empty())
		if (auto *component = authored_component(node.component))
		{
			auto *box = document_->GetElementById(layout_node_id(node));
			const float density = context_->GetDensityIndependentPixelRatio();
			const float box_width = box->GetClientWidth() / density, box_height = box->GetClientHeight() / density;
			const auto minimum = text_scale_ / 100.f;
			const float width = std::max({node.minimum_width * minimum, node.minimum_height * minimum * node.aspect,
			                              std::min(box_width, box_height * node.aspect)});
			property(component, "position", "absolute");
			property(component, "left", std::to_string((box_width - width) / 2) + "dp");
			property(component, "top", std::to_string((box_height - width / node.aspect) / 2) + "dp");
			property(component, "width", std::to_string(width) + "dp");
			property(component, "height", std::to_string(width / node.aspect) + "dp");
		}
	for (const auto &child : node.children)
		fit_authored_aspects(child);
}
void ConsoleSurface::State::measure_authored_geometry()
{
	if (authored_dom_.empty())
		return;
	const auto origin = document_->GetElementById("authored-body")->GetAbsoluteOffset(Rml::BoxArea::Border);
	authored_resolved_.diagnostics.clear();
	for (auto &placement : authored_resolved_.placements)
		if (auto *component = authored_component(placement.component))
		{
			placement.visible = component->IsVisible(true);
			const auto at = component->GetAbsoluteOffset(Rml::BoxArea::Border);
			placement.bounds = {at.x - origin.x, at.y - origin.y, component->GetOffsetWidth(),
			                    component->GetOffsetHeight()};
			if (placement.visible && (component->GetScrollHeight() > component->GetClientHeight() + 1 ||
			                          component->GetScrollWidth() > component->GetClientWidth() + 1))
				authored_resolved_.diagnostics.push_back(placement.id +
				                                         ": content scrolls to retain usable text and control sizes");
		}
	std::function<void(const UiNode &)> measure = [&](const UiNode &node)
	{
		auto *box = document_->GetElementById(layout_node_id(node));
		const auto at = box->GetAbsoluteOffset(Rml::BoxArea::Border);
		authored_resolved_.containers[node.id] = {at.x - origin.x, at.y - origin.y, box->GetOffsetWidth(),
		                                          box->GetOffsetHeight()};
		for (const auto &child : node.children)
			measure(child);
	};
	measure(authored_resolved_.root);
}
void ConsoleSurface::State::prepare_authored_layout(const Json *layout, const ConsoleRegion &region)
{
	ensure_station_components();
	const bool authored = layout && layout->contains("layout") && mission_outcome_.empty();
	document_->SetClass("authored", authored);
	apply_console_theme(layout);
	if (!authored)
	{
		prepare_console_tabs(region);
		if (!mission_outcome_.empty())
		{
			restore_authored_components();
			property(document_->GetElementById("gameplay"), "display", "none");
			property(document_->GetElementById("widgets"), "display", "none");
			property(document_->GetElementById("navigation-layer"), "display", "block");
		}
		return;
	}
	const auto signature = std::to_string(layout_identity_) + "/" + tactical_.console();
	if (authored_identity_ != signature)
	{
		auto compiled = compile_ui_layout(app_->session->catalog(), *layout);
		authored_targets_.clear();
		std::function<void(const UiNode &)> collect = [&](const UiNode &node)
		{
			if (!node.component.empty())
				authored_targets_.insert(node.component);
			for (const auto &child : node.children)
				collect(child);
		};
		collect(compiled.root);
		authored_program_ = available_ui_layout(std::move(compiled), model_.ui_availability());

		authored_identity_ = signature;
	}
	auto geometry_key = [&]
	{
		std::string key = signature + "/" + std::to_string(region.width) + "/" + std::to_string(region.height) + "/" +
		                  std::to_string(text_scale_) + "/" + std::to_string(model_.available_tabs().size());
		for (const auto &[group, page] : authored_pages_)
			key += "/" + group + "/" + page;
		return key;
	};
	const auto geometry = geometry_key();
	if (geometry != authored_geometry_)
	{
		if (!authored_geometry_.empty())
			cancel_pointer();
		authored_resolved_ = resolve_console_ui(*authored_program_, region.width, region.height, text_scale_ / 100.f,
		                                        authored_pages_, model_.available_tabs().size() > 1);
		for (const auto &[group, page] : authored_resolved_.selected_pages)
			authored_pages_[group] = page;
		authored_geometry_ = geometry_key();
	}
	const auto &resolved = authored_resolved_;

	property(document_->GetElementById("authored-body"), "top", std::to_string(resolved.body.y) + "dp");
	property(document_->GetElementById("layout-tabs"), "top",
	         std::to_string(resolved.body.y - 48 * text_scale_ / 100.f) + "dp");
	property(document_->GetElementById("layout-tabs"), "height", std::to_string(48 * text_scale_ / 100.f) + "dp");
	property(document_->GetElementById("console-tabs"), "top", "0dp");
	property(document_->GetElementById("console-tabs"), "height", std::to_string(48 * text_scale_ / 100.f) + "dp");
	auto *host = document_->GetElementById("authored-body");
	// Only hide roots not used as targets: avoid toggling a live root twice per frame.
	std::set<Rml::Element *> targets;
	for (const auto &placement : resolved.placements)
		if (auto *component = authored_component(placement.component))
			targets.insert(component);
	for (const char *panel :
	     {"widgets", "gameplay", "comms-layer", "sensor-panel", "navigation-layer", "manual-flight", "power-panel",
	      "repair-panel", "operations-panel", "docking-panel", "craft-panel", "personnel-panel", "away-panel"})
		if (auto *element = document_->GetElementById(panel); element && !targets.contains(element))
			property(element, "display", "none");
	authored_page_groups_ = resolved.page_groups;
	const auto dom = signature + (resolved.compact ? "/compact" : "/wide");
	if (authored_dom_ != dom)
	{
		build_authored_tree(resolved.root);
		authored_dom_ = dom;
	}
	auto stage_started = GetTime();
	auto stage = [&](const char *name)
	{
		const auto now = GetTime();
		app_->frame_metrics["console/" + std::to_string(route_) + "/" + name] = now - stage_started;
		stage_started = now;
	};
	if (authored_program_->header)
	{
		auto *header = document_->GetElementById("authored-header");
		property(header, "height", std::to_string(resolved.body.y) + "dp");
		style_authored_tree(*authored_program_->header);
		property(document_->GetElementById("console-tabs"), "text-align", "right");
		property(document_->GetElementById("console-tabs"), "overflow", "hidden");
		context_->Update();
		const auto measured =
		    std::max(resolved.body.y, header->GetOffsetHeight() / context_->GetDensityIndependentPixelRatio());
		property(document_->GetElementById("authored-body"), "top", std::to_string(measured) + "dp");
	}
	style_authored_tree(resolved.root);
	for (const auto &target : authored_targets_)
		if (auto *component = authored_component(target); component && !targets.contains(component))
			property(component, "display", "none");
	stage("authored-style");
	context_->Update();
	stage("authored-update-style");
	place_authored_instruments(resolved.root);
	stage("authored-instruments");
	context_->Update();
	stage("authored-update-instruments");
	fit_authored_aspects(resolved.root);
	stage("authored-aspects");
	std::string markup;
	for (const auto &tab : resolved.pages)
	{
		if (resolved.page_groups.at(tab.id) == resolved.root.id)
			continue;
		bool unread = false;
		for (const auto &p : resolved.placements)
			if ((p.id == tab.id || p.id.starts_with(tab.id + ".")) &&
			    (p.component == "conversations" || p.component.starts_with("conversation-")))
				for (const auto &thread : model_.conversations().value("threads", Json::array()))
					unread |= thread.value("unread", false);
		const auto label = localise(tab.label) + (unread ? " — " + localise("Unread") : "");
		const bool selected = authored_pages_.at(resolved.page_groups.at(tab.id)) == tab.id;
		markup += "<button id=\"" + id("layout-page/" + tab.id) + "\" class=\"" + (selected ? "selected" : "") + "\">" +
		          Rml::StringUtilities::EncodeRml(label) + "</button>";
		app_->screen_controls.emplace_back(id("layout-page/" + tab.id), label);
		app_->screen_toggles[id("layout-page/" + tab.id)] = selected;
	}
	if (markup != authored_tabs_markup_)
	{
		authored_tabs_markup_ = markup;
	}
	prepare_console_tabs(region);
	stage("authored-tabs");
	context_->Update();
	stage("authored-update-aspects");
	measure_authored_geometry();
	if (authored_program_->header)
	{
		authored_header_resolved_.body = {0, 0, region.width, resolved.body.y};
		std::function<void(const UiNode &)> measure_header = [&](const UiNode &node)
		{
			if (auto *box = document_->GetElementById(layout_node_id(node)))
			{
				const auto at = box->GetAbsoluteOffset(Rml::BoxArea::Border);
				authored_header_resolved_.containers[node.id] = {at.x, at.y, box->GetOffsetWidth(),
				                                                 box->GetOffsetHeight()};
			}
			for (const auto &child : node.children)
				measure_header(child);
		};
		measure_header(*authored_program_->header);

		for (auto &placement : authored_header_resolved_.placements)
			if (auto *component = authored_component(placement.component))
			{
				const auto at = component->GetAbsoluteOffset(Rml::BoxArea::Border);
				placement.visible = component->IsVisible(true);
				placement.bounds = {at.x, at.y, component->GetOffsetWidth(), component->GetOffsetHeight()};
			}
	}

	stage("authored-geometry");
	if (auto *focus = context_->GetFocusElement(); focus && !focus->IsVisible(true))
		for (const auto &placement : resolved.placements)
		{
			auto *component = authored_component(placement.component);
			bool contains = false;
			for (auto *ancestor = focus; ancestor; ancestor = ancestor->GetParentNode())
				if (ancestor == component)
					contains = true;
			if (contains)
				for (const auto &tab : resolved.pages)
					if (placement.id == tab.id || placement.id.starts_with(tab.id + "."))
						if (auto *button = document_->GetElementById(
						        id(resolved.page_groups.at(tab.id) == resolved.root.id
						               ? "console-page/" + model_.observation()->console + "/" + tab.id
						               : "layout-page/" + tab.id)))
							button->Focus();
		}
	if (page_focus_pending_)
	{
		if (auto *focus = context_->GetFocusElement(); focus && !focus->IsVisible(true))
			for (const auto &[group, page] : authored_pages_)
				if (auto *button = document_->GetElementById(id("layout-page/" + page)))
				{
					button->Focus();
					break;
				}
		page_focus_pending_ = false;
	}
	for (const auto &tab : resolved.pages)
		place(*app_, region, id("layout-page/" + tab.id));
}
std::string ConsoleSurface::State::settings_button(const std::string &name, const std::string &label)
{
	local_settings_controls_.emplace_back(id(name), label);
	return "<button id=\"" + id(name) + "\">" + Rml::StringUtilities::EncodeRml(localise(label)) + "</button>";
}
std::string ConsoleSurface::State::star_choice(const std::string &action, const std::string &profile,
                                               const std::map<std::string, int> &levels, const std::string &selected,
                                               bool settings)
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
bool ConsoleSurface::State::select_lobby_complexity(const std::string &profile)
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

bool ConsoleSurface::State::all_stop(const std::string &key)
{
	if (settings_open_ || systems_open_ || model_.manual_active() || !app_ || !game_active(*app_) || !model_.layout())
		return false;
	for (const auto &widget : model_.layout()->at("widgets"))
		if (widget.at("id") == key && widget.at("kind") == "throttle")
		{
			const auto binding = model_.widget_system(key);
			if (model_.system_minimum(binding) > 0 || model_.system_maximum(binding) < 0)
				return false;
			if (!model_.set_speed_fraction(throttles_.zero(model_, key), binding))
				return false;
			std::erase_if(throttle_contacts_,
			              [&](const auto &contact)
			              {
				              return contact.second == key;
			              });
			throttles_.settle(key, throttles_.zero(model_, key), GetTime(), 0);
			app_->accessible_status = localise("All stop");
			return true;
		}
	return false;
}
bool ConsoleSurface::State::support_active() const
{
	return !support_root_.empty();
}
bool ConsoleSurface::State::support_primary() const
{
	return support_active() && model_.observation() && model_.observation()->console == support_root_;
}
bool ConsoleSurface::State::support_repair_control(const std::string &name, const Json &descriptor) const
{
	if (name.starts_with("game.team") || name.starts_with("game.compartment/"))
		return true;
	if (descriptor.value("controller", "").find("repair") != std::string::npos)
		return true;
	const auto consumers = command_consumers(*model_.observation(), descriptor);
	for (const auto &system : model_.observation()->systems)
		if (system.kind == OperatingSystem::Team &&
		    std::find(consumers.begin(), consumers.end(), system.id) != consumers.end())
			return true;
	return false;
}
void ConsoleSurface::State::finish_support_panels(const ConsoleRegion &region)
{
	const bool detail = support_active() && support_detail_ && !settings_open_ && model_.observation() &&
	                    model_.observation()->console == support_navigation_;
	document_->SetClass("support-detail-open", detail);
	auto *close = document_->GetElementById(id("support-close-details"));
	if (!close)
	{
		auto created = document_->CreateElement("button");
		created->SetId(id("support-close-details"));
		close = document_->AppendChild(std::move(created));
		close->SetInnerRML(Rml::StringUtilities::EncodeRml(localise("Close details")));
		close->SetClass("support-detail-close", true);
	}
	property(close, "display", detail ? "block" : "none");
	auto *body = document_->GetElementById("authored-body");
	property(body, "left", detail ? "3%" : "0dp");
	property(body, "right", detail ? "3%" : "0dp");
	property(body, "bottom", detail ? "3%" : "0dp");
	property(body, "background-color", detail ? "#102027" : "transparent");
	property(body, "border", detail ? "2dp #988b71" : "0dp");
	if (detail)
	{
		property(body, "top", "156dp");
		context_->Update();
		app_->screen_controls.emplace_back(id("support-close-details"), localise("Close details"));
		place(*app_, region, id("support-close-details"));
	}
}
bool ConsoleSurface::State::sharing_hit(const std::string &key, float x, float y) const
{
	const auto found = sharing_pods_.find(key);
	if (found == sharing_pods_.end())
		return false;
	const auto &pod = found->second;
	const float dx = x - pod.x, dy = y - pod.y;
	return CheckCollisionPointRec({x, y}, pod) && std::min(dx, pod.width - dx) + std::min(dy, pod.height - dy) >= 8.f;
}
std::string ConsoleSurface::State::asset_path(const std::string &name) const
{
#ifdef __EMSCRIPTEN__
	return "/assets/helm/" + name;
#else
	return std::string(GetApplicationDirectory()) + "assets/helm/" + name;
#endif
}
Texture2D ConsoleSurface::State::hud(const std::string &name)
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
void ConsoleSurface::State::image(Texture2D texture, Rectangle area)
{
	DrawTexturePro(texture, {0, 0, static_cast<float>(texture.width), static_cast<float>(texture.height)}, area, {0, 0},
	               0, WHITE);
}
Vector2 ConsoleSurface::State::widget_offset(const std::string &key) const
{
	const auto at = document_->GetElementById("widget/" + key)->GetAbsoluteOffset(Rml::BoxArea::Border);
	return {at.x, at.y};
}
Rectangle ConsoleSurface::State::throttle_rect(const std::string &key) const
{
	auto *widget = document_->GetElementById("widget/" + key);
	const auto at = widget_offset(key);
	const auto &size = hud_metadata_.at("helm-throttle-track").at("image_size");
	const float density = context_->GetDensityIndependentPixelRatio();
	Rml::ElementList headings;
	widget->GetElementsByTagName(headings, "h2");
	const float header =
	    std::max(45.f * density, (headings.empty() ? 0.f : headings.front()->GetOffsetHeight()) + 16.f * density);
	const float height = std::max(30.f * density, widget->GetOffsetHeight() - header - 111.f * density),
	            width = height * size[0].get<float>() / size[1].get<float>();
	return {at.x + (widget->GetOffsetWidth() - width) / 2, at.y + header, width, height};
}
void ConsoleSurface::State::place(Application &app, const ConsoleRegion &region, const std::string &name)
{
	if (auto *element = this->element(name))
	{
		const auto at = element->GetAbsoluteOffset(Rml::BoxArea::Border);
		app.control_placements[name] = {region.x + at.x,
		                                region.y + at.y,
		                                element->GetOffsetWidth(),
		                                element->GetOffsetHeight(),
		                                context_->GetFocusElement() == element,
		                                !element->HasAttribute("disabled") &&
		                                    !(dynamic_cast<Rml::ElementFormControl *>(element) &&
		                                      dynamic_cast<Rml::ElementFormControl *>(element)->IsDisabled())};
	}
}
std::string ConsoleSurface::State::id(const std::string &action) const
{
	return "console/" + std::to_string(route_) + "/" + action;
}
std::string ConsoleSurface::State::on_screen_markup(const std::string &action) const
{
	return "<button class=\"on-screen\" id=\"" + Rml::StringUtilities::EncodeRml(id(action)) +
	       "\"><span class=\"share-screw share-screw-left\"/><span class=\"share-screw share-screw-right\"/>"
	       "<span class=\"share-monitor\"><span/></span><span class=\"share-label\">" +
	       Rml::StringUtilities::EncodeRml(localise("On screen")) + "</span><span class=\"share-caption share-idle\">" +
	       Rml::StringUtilities::EncodeRml(localise("Standby")) + "</span><span class=\"share-caption share-live\">" +
	       Rml::StringUtilities::EncodeRml(localise("Live")) +
	       "</span><span class=\"share-lamp-housing\"><span class=\"share-lamp\"/></span></button>";
}
std::string ConsoleSurface::State::button(const std::string &action, const std::string &label,
                                          const std::string &visual)
{
	controls_.emplace_back(id(action), label);
	if (action.starts_with("on-screen/"))
		return on_screen_markup(action);
	return "<button " + std::string(action.starts_with("game.contacts/") ? "class=\"radar-contacts\" " : "") +
	       (action.starts_with("camera/") ? "style=\"decorator:none;background-color:transparent\" " : "") + "id=\"" +
	       Rml::StringUtilities::EncodeRml(id(action)) + "\">" +
	       (visual.empty() ? Rml::StringUtilities::EncodeRml(translate(label, app_->pseudo_locale))
	                       : "<span class=\"camera-icon\">" + Rml::StringUtilities::EncodeRml(visual) + "</span>") +
	       "</button>";
}
std::string ConsoleSurface::State::console_markup(std::string markup) const
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
ConsoleSurface::State::State(ConsoleId route, const std::string &prefix, bool toolbar)
    : route_(route), context_name_(prefix + std::to_string(route)), toolbar_enabled_(toolbar)
{
	terminal_ = parse_json(read_file(asset_path("../menu/menu-scene.json"), 64 * 1024), "menu scene").at("terminal");
	for (int i = 1; i <= 12; ++i)
		crew_portraits_.emplace(sha256(read_file(asset_path("../menu/avatar-" + std::to_string(i) + ".png"), 12288)),
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
scrollbarvertical {width:14dp;background:#263638;}
scrollbarvertical slidertrack {width:14dp;}
scrollbarvertical sliderbar {min-height:32dp;background:#ccc2af;}
scrollbarhorizontal {height:14dp;background:#263638;}
scrollbarhorizontal slidertrack {height:14dp;}
scrollbarhorizontal sliderbar {min-width:32dp;background:#ccc2af;}
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
#gameplay-readouts p.target-motion {display:inline-block;margin-right:12dp;}
.dial {position:absolute;left:12px;right:12px;top:48px;bottom:72px;}
.widget button {position:absolute;bottom:8px;left:8px;}
.radar-widget button.on-screen {position:absolute;left:auto;right:8px;}
.readout {position:absolute;bottom:12px;left:16px;right:16px;height:44px;text-align:center;color:#cbe9c8;}
.heading-value {display:inline-block;vertical-align:middle;margin:0 8px;width:145px;min-height:1.8em;height:auto;background:#0a1719;color:#cbe9c8;border:1px #988b71;font-size:1.3em;}
.throttle-widget .readout {bottom:66dp;}
.throttle-widget button.all-stop {position:absolute;left:8dp;right:8dp;bottom:8dp;width:auto;min-height:44dp;box-sizing:border-box;margin:0;}
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
.station-panel {box-sizing:border-box;padding:4dp;overflow:hidden;}
.station-panel h2,.engineering-board h2 {font-size:1.05em;margin:0 0 6dp 0;}
.task-button:disabled,.task-button.disabled,.task-button.disabled:hover,.task-button.disabled:focus {background:#152128;color:#78868a;border-color:#45545a;}
.task-toggles,.task-choices {display:flex;width:100%;flex-wrap:wrap;gap:6dp;}
.task-toggles .task-button {box-sizing:border-box;width:48%;min-height:44dp;padding:5dp;margin:0;}
.task-state-icon {pointer-events:none;display:inline-block;width:8dp;height:8dp;margin-right:5dp;border:1dp #fff0d4;border-radius:5dp;background:#f0e3c2;}
.off .task-state-icon {background:#102027;}
.task-button.on {background:#235b3e;border-color:#8bd5a6;}
.task-button.off {background:#6f3030;border-color:#e4a39b;}
.task-choices .task-button {box-sizing:border-box;width:23%;min-width:0;min-height:44dp;padding:5dp;margin:2dp;}
.shield-diagram {display:block;width:100%;height:55%;min-height:80dp;}
.condition-slider {width:94%;height:44dp;min-height:44dp;}
.condition-slider slidertrack {height:8dp;margin-top:18dp;background:#253b43;}
.condition-slider sliderbar {width:24dp;height:32dp;margin-top:4dp;background:#ffc16d;border:2dp #ffe4b0;}
.condition-slider sliderarrowdec,.condition-slider sliderarrowinc {display:none;}
.condition-ticks {display:flex;justify-content:space-between;width:94%;font-size:.85em;}
.shield-sector {width:46%;border:2dp #74b5bc;border-radius:24dp;padding:8dp;box-sizing:border-box;}
.health-row {display:inline-flex;flex-direction:column;vertical-align:top;width:48%;gap:3dp;margin:3dp 0;}
.health-row span {width:100%;font-size:.85em;} .health-row progress {width:94%;} progress {height:12dp;width:60%;background:#24404a;}
progress fill {background:#83be90;}
#team-roster-panel .task-button {display:block;box-sizing:border-box;width:100%;margin:3dp 0;padding:6dp;min-height:44dp;}
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
#navigation-layer button,#navigation-layer input,.navigation-canvas {pointer-events:auto;}
#gameplay-controls .status-indicator {width:132px;min-height:30px;height:32px;margin:2px;padding:3px;font-size:0.7em;overflow:hidden;}
.integrated-helm #gameplay-controls button {width:210dp;min-height:48dp;font-size:.85em;padding:6dp;margin:2dp;}
.radar-contact {position:absolute;width:24px;height:24px;min-width:0;min-height:0;margin:0;padding:0;background:transparent;border:1px transparent;z-index:4;}
.radar-contact:focus {border-color:transparent;background:transparent;}
.support-detail-close {position:absolute;right:3%;top:112dp;min-height:44dp;z-index:75;}
.local-settings-open .support-detail-close {visibility:hidden;}
.support-panels .engineering-card p {min-height:0;}
.resource-condition {background:#83be90;}
#fault-dialog {position:absolute;left:10%;top:15%;width:80%;height:65%;padding:24px;box-sizing:border-box;background:#111e22;border:3px #dfa35d;z-index:80;overflow:auto;display:none;}
#fault-dialog p {font-size:1.1em;line-height:1.4;}
#comms-layer {position:absolute;left:1%;top:80px;width:98%;bottom:1%;background:#102027;}
#comms-list {position:absolute;left:0;top:0;height:100%;overflow:auto;}
#comms-list button {display:block;box-sizing:border-box;width:100%;min-height:44dp;margin:4dp 0;}
#comms-conversation {position:absolute;top:0;height:100%;}
#comms-header {display:block;width:100%;height:20%;overflow:auto;}
#comms-choices,#comms-incoming,#comms-compose {display:block;}
#comms-header button {width:44%;height:44dp;min-height:44dp;box-sizing:border-box;padding:4dp;margin:2dp;vertical-align:top;}
#comms-header h3,#comms-list h3 {display:block;margin:6px 4px;font-size:1.1em;}
#comms-history {height:52%;overflow:auto;background:#172b32;padding:8px;}
#comms-incoming {height:30px;}
#comms-choices {max-height:18%;overflow:auto;}
.local-settings-open .engineering-board {display:none;}
.engineering-board input {display:block;width:90%;height:44dp;margin:0 4dp;}
.engineering-board input slidertrack {height:40dp;background:#253b43;}
.engineering-board input sliderprogress {height:40dp;background:#456a75;}
.engineering-board input sliderbar {width:24dp;height:36dp;margin-top:2dp;background:#ffc16d;border:2dp #ffe4b0;}
.engineering-board input sliderarrowdec,.engineering-board input sliderarrowinc {display:none;}
.engineering-board label {display:block;}
.engineering-board select {display:block;height:44dp;width:70%;min-width:280dp;max-width:520dp;}
.engineering-board button span {pointer-events:none;}
.engineering-board button {min-height:44dp;margin:4dp;padding:8dp;}
.engineering-board .power-reserves {display:flex;flex-wrap:wrap;gap:16dp;border-bottom:1dp #988b71;}
.engineering-board .power-reserves > div {min-width:300dp;flex:1;}
.engineering-board .power-row {display:flex;align-items:center;gap:12dp;padding:8dp 0;border-bottom:1dp #526167;}
.engineering-board .power-name {width:24%;font-size:.85em;}
.engineering-board .power-control {flex:1;min-width:90dp;}
.engineering-board .power-control label {display:none;}
.engineering-board .power-control span {font-size:.85em;}
.engineering-board .power-row {gap:5dp;padding:3dp 0;font-size:.9em;}
.engineering-board .power-row input {height:44dp;}
.engineering-board .power-reserves {font-size:.9em;}
.engineering-board .power-reserves h3 {font-size:1em;margin:0;}
.engineering-board .power-reserves p {margin:0;}
.engineering-board .power-battery {width:22%;}
.engineering-board .repair-columns {display:flex;gap:24dp;}
.engineering-board .repair-columns > div {flex:1;min-width:0;}
.engineering-board .repair-columns button {display:block;width:95%;}
#comms-choices button {display:block;width:100%;box-sizing:border-box;min-height:44dp;margin:4dp 0;}
#comms-compose {overflow:hidden;}
#comms-compose input {width:75%;height:44dp;box-sizing:border-box;vertical-align:top;margin:0;}
#comms-compose button {box-sizing:border-box;min-height:44dp;height:44dp;padding:4dp 8dp;margin:0 4dp;vertical-align:top;}
.layout-node {box-sizing:border-box;min-width:0;min-height:0;}
#authored-body {display:none;position:absolute;left:0;right:0;top:160dp;bottom:0;overflow:auto;}
#layout-tabs,#console-tabs {position:absolute;left:1%;right:1%;top:64dp;height:44dp;overflow-x:auto;white-space:nowrap;z-index:8;}
#layout-tabs {display:none;top:110dp;}
#layout-tabs button,#console-tabs button {min-height:44dp;display:inline-block;margin:0 4dp;}
#layout-tabs .selected,#console-tabs .selected {border:2dp #ffe4b0;}
.authored #authored-body,.authored #layout-tabs {display:block;}
.authored #authored-body .widget {display:block;box-sizing:border-box;}
.authored .legacy-tools,.authored #toolbar {display:none;}
#authored-header {position:absolute;left:0;top:0;width:100%;height:56dp;z-index:20;}
#authored-header h1 {font-size:1.1em;margin:0;}
#authored-header .settings-cog {margin:0;padding:0;}
#authored-header button {min-width:44dp;min-height:44dp;padding:4dp 8dp;margin:0 2dp;font-size:1em;}
.authored #toolbar h1 {display:block;font-size:1em;white-space:normal;word-break:break-all;max-width:100%;}
.authored button.on-screen {box-sizing:border-box;width:auto;height:auto;min-height:44dp;min-width:140dp;padding:6dp 10dp;background:#102027;border:1dp #988b71;}
.authored button.on-screen .share-label {position:static;display:inline;font-size:1em;}
.authored button.on-screen .share-caption,.authored button.on-screen .share-monitor,.authored button.on-screen .share-screw,.authored button.on-screen .share-lamp-housing {display:none;}
.authored button.on-screen-active {background:#235b3e;border:2dp #8bd5a6;}
.radar-widget button.radar-systems {opacity:1;position:absolute;left:8dp;top:8dp;bottom:auto;z-index:10;pointer-events:auto;}
.authored .radar-widget button.radar-contacts {left:auto;right:8dp;bottom:8dp;z-index:2;}
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
</style></head><body><div id="authored-header"></div><div id="console-tabs"></div><div id="layout-tabs"></div><div id="authored-body"></div><div id="comms-layer"><div id="comms-list"></div><div id="comms-conversation"><div id="comms-header"></div><div id="comms-history"></div><div id="comms-incoming"></div><div id="comms-choices"></div><div id="comms-compose"></div></div></div><div id="sensor-panel"><h2 id="sensor-title"></h2><div id="sensor-controls"></div><div id="sensor-readouts"></div></div><div id="fault-dialog"></div><div id="toolbar"></div><div id="widgets"></div><div id="gameplay"><div id="engineering-cards"></div><div id="gameplay-controls"></div><div id="gameplay-messages"></div><div id="gameplay-readouts"></div></div><div id="lobby"></div><div id="navigation-layer"></div><div id="manual-flight"></div><div id="local-settings"></div></body></rml>)"));
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
		hud_metadata_[name] = parse_json(read_file(asset_path(std::string(name) + ".sprite.json"), 64 * 1024), name);
	document_->Show();
}
ConsoleSurface::State::~State()
{
	if (ui_texture_.id)
		UnloadRenderTexture(ui_texture_);
	Rml::RemoveContext(context_name_);
}
void ConsoleSurface::State::preview_text_scale(int scale)
{
	text_scale_ = std::clamp(scale, 50, 200);
}
void ConsoleSurface::State::preview_pages(const Json &layout)
{
	preview_header_ = layout.value("_preview_header", false);
	if (!layout.contains("_preview_pages"))
		return;
	const auto hint = layout.at("_preview_pages").dump();
	const bool interact = layout.value("_preview_interact", false);
	if (hint != preview_pages_signature_ || (preview_interact_ && !interact))
		authored_pages_ = layout.at("_preview_pages").get<std::map<std::string, std::string>>();
	preview_pages_signature_ = hint;
	preview_interact_ = interact;
}
Rml::Context &ConsoleSurface::State::context()
{
	return *context_;
}
void ConsoleSurface::State::update_view()
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
bool ConsoleSurface::State::preparation_urgent() const
{
	return prepare_dirty_ || !prepared_cache_;
}
void ConsoleSurface::State::prepare(Application &app, const ConsoleRegion &physical_region,
                                    const std::set<std::string> &pads, bool allow_fresh)
{
	const auto source = app.session ? app.session->console_snapshot(route_) : nullptr;
	const auto environment = std::to_string(physical_region.x) + "/" + std::to_string(physical_region.y) + "/" +
	                         std::to_string(physical_region.width) + "/" + std::to_string(physical_region.height) +
	                         "/" + app.language + "/" + std::to_string(app.pseudo_locale) + "/" +
	                         std::to_string(text_scale_) + "/" + std::to_string(static_cast<int>(app.screen));
	if (prepared_owner_.lock() != app.session || !app.session || prepared_generation_ != app.session->generation() ||
	    !same_observation_scope(prepared_source_, source))
		render_urgent_ = true;
	const auto record_freshness = [&]
	{
		const auto tick = app.session ? app.session->ticks() : 0;
		app.frame_metrics["console/" + std::to_string(route_) + "/data-age"] =
		    prepared_source_ && tick >= prepared_source_->observed_tick ? (tick - prepared_source_->observed_tick) * .05
		                                                                : 0;
	};
	const bool stable = prepared_cache_ && !prepare_dirty_ && source && app.session && app.session->running() &&
	                    app.screen == Screen::console && prepared_owner_.lock() == app.session &&
	                    prepared_generation_ == app.session->generation() && prepared_environment_ == environment &&
	                    prepared_pads_ == pads && same_observation_scope(prepared_source_, source);
	if (stable && (source == prepared_source_ || (app.budget_console_preparation && (!allow_fresh || render_dirty_))))
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
bool ConsoleSurface::State::paint_pending()
{
	if (context_->GetNextUpdateDelay() <= 0)
		render_dirty_ = true;
	if (render_dirty_ && !render_dirty_since_)
		render_dirty_since_ = GetTime();
	return render_dirty_;
}
double ConsoleSurface::State::paint_wait() const
{
	return render_dirty_since_ ? GetTime() - render_dirty_since_ : 0;
}
double ConsoleSurface::State::paint_cost() const
{
	return last_paint_cost_;
}
void ConsoleSurface::State::prepare_loading(const LoadingProgress *loading, const ConsoleRegion &region)
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
ConsoleSurface::ConsoleSurface(ConsoleId route, const std::string &prefix, bool toolbar)
    : state_(std::make_unique<State>(route, prefix, toolbar))
{
}
ConsoleSurface::~ConsoleSurface() = default;
void ConsoleSurface::preview_text_scale(int scale)
{
	state_->preview_text_scale(scale);
}
const UiResolved &ConsoleSurface::authored_geometry() const
{
	return state_->authored_geometry();
}
void ConsoleSurface::preview_pages(const Json &layout)
{
	state_->preview_pages(layout);
}
Rml::Context &ConsoleSurface::context()
{
	return state_->context();
}
bool ConsoleSurface::action(const std::string &name)
{
	return state_->action(name);
}
bool ConsoleSurface::controller_hold(const std::string &name, bool pressed)
{
	return state_->controller_hold(name, pressed);
}
std::string ConsoleSurface::controller_identity() const
{
	return state_->controller_identity();
}
bool ConsoleSurface::controller_flight() const
{
	return state_->controller_flight();
}
Rml::Element *ConsoleSurface::controller_element(const std::string &name)
{
	return state_->controller_element(name);
}
void ConsoleSurface::controller_cancel()
{
	state_->controller_cancel();
}
bool ConsoleSurface::controller_back()
{
	return state_->controller_back();
}
void ConsoleSurface::focus(const std::string &name)
{
	state_->focus(name);
}
bool ConsoleSurface::set_value(const std::string &name, const std::string &text)
{
	return state_->set_value(name, text);
}
void ConsoleSurface::flush_manual()
{
	state_->flush_manual();
}
void ConsoleSurface::cancel_gamepad(const std::string &device)
{
	state_->cancel_gamepad(device);
}
void ConsoleSurface::gamepad(const std::string &device, const std::string &control, float value)
{
	state_->gamepad(device, control, value);
}
void ConsoleSurface::manual_frame(bool keyboard_selected, FlightAxes keyboard_axes, bool focused)
{
	state_->manual_frame(keyboard_selected, keyboard_axes, focused);
}
void ConsoleSurface::prepare_manual(Application &app, const ConsoleRegion &region, bool pregame)
{
	state_->prepare_manual(app, region, pregame);
}
bool ConsoleSurface::pointer(std::uint64_t contact, float x, float y, DeviceEventKind kind)
{
	return state_->pointer(contact, x, y, kind);
}
void ConsoleSurface::cancel_pointer()
{
	state_->cancel_pointer();
}
void ConsoleSurface::update_view()
{
	state_->update_view();
}
bool ConsoleSurface::probe_throttle(unsigned stage)
{
	return state_->probe_throttle(stage);
}
bool ConsoleSurface::probe_conversation() const
{
	return state_->probe_conversation();
}
bool ConsoleSurface::probe_tab(const std::string &action)
{
	return state_->probe_tab(action);
}
bool ConsoleSurface::probe_navigation(unsigned stage)
{
	return state_->probe_navigation(stage);
}
bool ConsoleSurface::probe_fit() const
{
	return state_->probe_fit();
}
bool ConsoleSurface::probe_support_visuals()
{
	return state_->probe_support_visuals();
}
bool ConsoleSurface::probe_readout_scroll()
{
	return state_->probe_readout_scroll();
}
bool ConsoleSurface::probe_report()
{
	return state_->probe_report();
}
bool ConsoleSurface::probe_ready()
{
	return state_->probe_ready();
}
bool ConsoleSurface::probe_on_screen(const std::string &widget)
{
	return state_->probe_on_screen(widget);
}
bool ConsoleSurface::probe_helm(bool verify)
{
	return state_->probe_helm(verify);
}
bool ConsoleSurface::probe_manual(unsigned stage)
{
	return state_->probe_manual(stage);
}
bool ConsoleSurface::probe_engineering(unsigned stage)
{
	return state_->probe_engineering(stage);
}
bool ConsoleSurface::preparation_urgent() const
{
	return state_->preparation_urgent();
}
void ConsoleSurface::prepare(Application &app, const ConsoleRegion &physical_region, const std::set<std::string> &pads,
                             bool allow_fresh)
{
	state_->prepare(app, physical_region, pads, allow_fresh);
}
bool ConsoleSurface::paint_pending()
{
	return state_->paint_pending();
}
double ConsoleSurface::paint_wait() const
{
	return state_->paint_wait();
}
double ConsoleSurface::paint_cost() const
{
	return state_->paint_cost();
}
void ConsoleSurface::render_ui(UiRenderer &renderer, const ConsoleRegion &region, bool allow_paint)
{
	state_->render_ui(renderer, region, allow_paint);
}
void ConsoleSurface::prepare_loading(const LoadingProgress *loading, const ConsoleRegion &region)
{
	state_->prepare_loading(loading, region);
}
void ConsoleSurface::prepare_current(Application &app, const ConsoleRegion &physical_region,
                                     const std::set<std::string> &pads)
{
	state_->prepare_current(app, physical_region, pads);
}
void ConsoleSurface::draw_profile(const ConsoleRegion &region)
{
	state_->draw_profile(region);
}
void ConsoleSurface::draw(const ConsoleRegion &region)
{
	state_->draw(region);
}
OperationsWidgetContext ConsoleSurface::State::operations_context()
{
	return {model_,
	        app_ && app_->session ? &app_->session->catalog() : nullptr,
	        id(""),
	        station_component_scope_,
	        [this](const std::string &action, const std::string &label)
	        {
		        return settings_button(action, label);
	        },
	        [this](const std::string &name)
	        {
		        return asset_path(name);
	        },
	        [this](const std::string &name)
	        {
		        return station_component_active(name);
	        },
	        [this](const std::string &name)
	        {
		        return authored_action(name);
	        }};
}
} // namespace phoenix
