module;
#include <utility>
#include <RmlUi/Core.h>
#include <raylib.h>
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.ui.preview;
import phoenix.content.catalog;
import phoenix.content.ui;
import phoenix.content.controllers;

import phoenix.application;
import phoenix.console;
import phoenix.ui.console_surface;
import phoenix.ui.rml;
namespace phoenix
{
namespace
{
class PreviewSession final : public SessionPort
{
	Catalog catalog_;
	Observation view_;
	std::vector<SystemView> base_systems_;
	std::string preview_state_;
	std::shared_ptr<const Catalog> authoring_identity_;
	std::uint64_t generation_ = 1;
	std::string widget_context_;
	std::map<std::string, std::int64_t> widget_settings_;
	std::map<std::string, std::uint64_t> widget_requests_;

	void widget_layout(const std::string &context, int stars)
	{
		if (context.empty() || !authoring_identity_)
			return;
		const auto split = context.find('/');
		if (split == std::string::npos || !authoring_identity_->entities.contains(context.substr(0, split)))
			throw std::runtime_error("Select a valid widget ship and Console context");
		const auto &ship = authoring_identity_->entities.at(context.substr(0, split));
		const auto instances = ship.value("control_layout", Json::object()).value("controllers", Json::array());
		if (std::none_of(instances.begin(), instances.end(),
		                 [&](const auto &instance)
		                 {
			                 return instance.value("console", std::string{}) == context.substr(split + 1) &&
			                        instance.contains("widget");
		                 }))
			return;
		view_.systems.clear();
		view_.gameplay["controllers"] = Json::array();
		view_.gameplay["controls"] = Json::array();
		for (const auto &instance : instances)
		{
			if (instance.value("console", std::string{}) != context.substr(split + 1) || !instance.contains("widget"))
				continue;
			const auto definition =
			    compile_control_widget(*authoring_identity_, instance.at("widget").get<std::string>());
			const auto bound = bind_control_widget(*authoring_identity_, instance);
			const auto level = instance.at("star_levels").at(std::to_string(stars)).get<std::string>();
			const auto selected = std::find_if(definition.levels.begin(), definition.levels.end(),
			                                   [&](const auto &candidate)
			                                   {
				                                   return candidate.id == level;
			                                   });
			if (selected == definition.levels.end())
				throw std::runtime_error("Unknown widget responsibility level in preview");
			Json row{{"id", instance.at("id")},           {"console", view_.console},
			         {"widget", definition.id},           {"generation", 1},
			         {"human", !selected->human.empty()}, {"commands", Json::object()},
			         {"equipment", Json::object()},       {"star_levels", instance.at("star_levels")},
			         {"data_placement", Json::object()}};
			const auto equipment = instance.at("equipment");
			for (const auto &[role, identities] : equipment.items())
				row["equipment"][role] = identities.is_array() ? identities : Json::array({identities});
			for (const auto &[item, target] : control_widget_data_placement(
			         definition, level, stars, instance.value("data_placement", Json::object())))
				row["data_placement"][std::to_string(stars)][item] = target == WidgetDataDestination::Main ? "main"
				                                                     : target == WidgetDataDestination::Details
				                                                         ? "details"
				                                                         : "hidden";
			for (const auto &command : bound.at("commands"))
			{
				const auto name = command.at("id").get<std::string>();
				const auto *action = operating_action(command.at("action").get<std::string>());
				const auto key = instance.at("id").get<std::string>() + "/" + name;
				const auto initial = command.value("initial", std::int64_t{0});
				const auto value = widget_settings_.contains(key) ? widget_settings_.at(key) : initial;
				const auto request = widget_requests_.contains(key) ? widget_requests_.at(key) : 0;
				auto minimum = command.value("minimum", action->minimum),
				     maximum = command.value("maximum", action->maximum);
				const bool human = selected->human.contains(name);
				const auto consumers = command.value("consumers", Json::array());
				if (action->action == OperatingAction::Speed)
					for (const auto &consumer : consumers)
					{
						const auto fitted =
						    ship.value("systems", Json::object()).value(consumer.get<std::string>(), Json::object());
						if (fitted.contains("max_speed_mm_s"))
						{
							minimum = std::max(minimum, -fitted.value("reverse_speed_mm_s", std::int64_t{0}));
							maximum = std::min(maximum, fitted.at("max_speed_mm_s").get<std::int64_t>());
						}
					}
				if (minimum > maximum || value < minimum || value > maximum)
					throw std::runtime_error("Widget preview command is outside the attached equipment range: " + key);
				row["commands"][name] = {
				    {"value", value},
				    {"arguments", command.value("arguments", Json::array())},
				    {"human", human},
				    {"action", command.at("action")},
				    {"minimum", minimum},
				    {"maximum", maximum},
				    {"semantics", action->semantics == CommandSemantics::Setting ? "setting" : "request"},
				    {"consumers", consumers},
				    {"revision", 1},
				    {"consumed", 1},
				    {"accepted", true},
				    {"request_id", request}};
				for (const auto &consumer : consumers)
				{
					const auto identity = consumer.get<std::string>();
					if (std::none_of(view_.systems.begin(), view_.systems.end(),
					                 [&](const auto &system)
					                 {
						                 return system.id == identity;
					                 }))
					{
						const auto fitted = ship.value("systems", Json::object()).value(identity, Json::object());
						view_.systems.push_back(
						    {identity, action->system, value, fitted.value("max_speed_mm_s", maximum), true, human});
					}
				}
				if (!human)
					continue;
				std::set<std::int64_t> values{minimum, value, maximum};
				const auto choices = command.value("choices", Json::array());
				if (!choices.empty())
				{
					values.clear();
					for (const auto &choice : choices)
						values.insert(choice.at("value").get<std::int64_t>());
				}
				for (const auto setting : values)
				{
					OperatingCommand proposal{
					    1, std::string(action->name), setting, {}, instance.at("id").get<std::string>(), name, 1};
					proposal.arguments = command.value("arguments", std::vector<std::int64_t>{});
					if (action->semantics == CommandSemantics::Request)
						proposal.request_id = request + 1;
					auto descriptor = encode_operating_command(proposal);
					descriptor["id"] = "sample." + key + "." + std::to_string(setting);
					descriptor["label"] = command.value("label_key", name) + " " + std::to_string(setting);
					view_.gameplay["controls"].push_back(std::move(descriptor));
				}
			}
			view_.gameplay["controllers"].push_back(std::move(row));
		}
	}

public:
	PreviewSession()
	{
		catalog_.entities["preview.ship"] = {{"id", "preview.ship"},
		                                     {"console", "preview.console"},
		                                     {"systems",
		                                      {{"engine", {{"max_speed_mm_s", 30000}, {"acceleration_mm_s2", 2000}}},
		                                       {"steering", {{"turn_mdeg_s", 90000}}},
		                                       {"radar", {{"range_mm", 100000}}}}}};
		view_.own = {1, "preview.ship", 0, 0, 0, 0, 0, RadarIcon::ship};
		view_.console = "main";
		view_.controls = view_.feeds = 7;
		view_.systems = {{"engine", OperatingSystem::Engine, 0, 30000, true, true},
		                 {"steering", OperatingSystem::Steering, 0, 359999, true, true},
		                 {"radar", OperatingSystem::Radar, 0, 100000, true, true}};
		base_systems_ = view_.systems;
		view_.engine_enabled = view_.radar_enabled = true;
		for (unsigned i = 2; i < 16; ++i)
			view_.contacts.push_back({i, "preview.ship", static_cast<std::int64_t>(i % 4) * 17000 - 25000,
			                          static_cast<std::int64_t>(i / 4) * 19000 - 25000, 0, 0,
			                          static_cast<std::int64_t>(i) * 23000, static_cast<RadarIcon>(1 + i % 3)});
	}
	void layout(Json value, std::shared_ptr<const Catalog> authoring = {})
	{
		if (!authoring && authoring_identity_)
		{
			catalog_.layouts.clear();
			catalog_.ui_groups.clear();
			catalog_.themes.clear();
			catalog_.assets.clear();
			catalog_.control_widgets.clear();
			catalog_.ui_theme.clear();
			authoring_identity_ = nullptr;
			++generation_;
		}
		if (authoring && authoring_identity_ != authoring)
		{
			authoring_identity_ = authoring;
			catalog_.layouts = authoring->layouts;
			catalog_.ui_groups = authoring->ui_groups;
			catalog_.themes = authoring->themes;
			catalog_.ui_theme = authoring->ui_theme;
			catalog_.assets = authoring->assets;
			catalog_.control_widgets = authoring->control_widgets;
			widget_settings_.clear();
			widget_requests_.clear();
			++generation_;
		}
		value.erase("_preview_pages");
		value.erase("_preview_interact");
		value.erase("_preview_header");
		const auto sample_state = value.value("_preview_state", std::string("normal"));
		value.erase("_preview_state");
		const int stars = value.value("_preview_stars", 3);
		value.erase("_preview_stars");
		const auto widget_context = value.value("_preview_widget_context", std::string{});
		value.erase("_preview_widget_context");
		auto &preview_ship = catalog_.entities.at("preview.ship");
		if (widget_context.empty())
		{
			view_.console = "main";
			preview_ship.erase("stations");
			preview_ship["console"] = "preview.console";
		}
		else
		{
			view_.console = widget_context.substr(widget_context.find('/') + 1);
			preview_ship.erase("console");
			const Json console{{"id", view_.console},
			                   {"definition", "preview.console"},
			                   {"systems", {"engine", "steering", "radar"}},
			                   {"feeds", {"engine", "steering", "radar"}}};
			preview_ship["stations"] = Json::array({{{"id", "preview"}, {"consoles", Json::array({console})}}});
		}
		const auto widget_identity = widget_context + "/" + std::to_string(stars);
		if (widget_identity != widget_context_)
		{
			widget_context_ = widget_identity;
			widget_settings_.clear();
			widget_requests_.clear();
			++generation_;
		}
		view_.gameplay["profile"] = "preview";
		view_.gameplay["profile_stars"] = {{"preview", stars}};
		view_.gameplay["controllers"] = Json::array();
		view_.gameplay["power"] = Json::object();
		view_.gameplay["teams"] = Json::array();
		view_.gameplay["combat"] = Json::object();

		if (sample_state != preview_state_)
		{
			preview_state_ = sample_state;
			++generation_;
		}
		if (sample_state == "empty")
			view_.contacts.clear();
		else if (view_.contacts.empty())
			for (unsigned i = 2; i < 16; ++i)
				view_.contacts.push_back({i, "preview.ship", static_cast<std::int64_t>(i % 4) * 17000 - 25000,
				                          static_cast<std::int64_t>(i / 4) * 19000 - 25000, 0, 0,
				                          static_cast<std::int64_t>(i) * 23000, static_cast<RadarIcon>(1 + i % 3)});
		view_.systems = base_systems_;
		if (sample_state == "engineering" || sample_state == "faults")
		{
			SystemView generator{"reactor", OperatingSystem::Generator, 0, 100, true, true};
			generator.telemetry = {
			    {"heat", sample_state == "faults" ? 90 : 30}, {"heat_capacity", 100}, {"actual_output", 70}};
			view_.systems.push_back(generator);
			view_.gameplay["controllers"] = Json::array({{{"id", "reactor"},
			                                              {"generation", 1},
			                                              {"human", stars >= 2},
			                                              {"commands",
			                                               {{"output",
			                                                 {{"action", "generator_output"},
			                                                  {"consumers", {"reactor"}},
			                                                  {"minimum", 0},
			                                                  {"maximum", 100},
			                                                  {"value", 70}}}}}}});
			view_.gameplay["power"] = {
			    {"reactors", Json::array({{{"id", "reactor"}, {"output", 70}, {"maximum", 100}}})},
			    {"reserves", Json::array({{{"id", "battery"}, {"energy", 600}, {"capacity", 1000}}})},
			    {"coolant", Json::array({{{"id", "coolant"}, {"remaining", 800}, {"capacity", 1000}}})}};
			view_.gameplay["teams"] = Json::array({{{"id", "repair-team"},
			                                        {"travel", 0},
			                                        {"reserved", false},
			                                        {"manual", false},
			                                        {"orders", Json::array()},
			                                        {"order", ""},
			                                        {"patients", Json::array()},
			                                        {"repair", true},
			                                        {"location", 0},
			                                        {"destination", 0},
			                                        {"work", "Ready"}}});
			view_.gameplay["combat"] = {{"compartments", Json::array({{{"id", "engineering"},
			                                                           {"deck", "Engineering"},
			                                                           {"disabled", false},
			                                                           {"index", 1},
			                                                           {"hp", sample_state == "faults" ? 60 : 100},
			                                                           {"maximum", 100},
			                                                           {"damaged", sample_state == "faults"}}})}};
		}
		bool conversations = sample_state == "conversations";
		for (const auto &widget : value.at("widgets"))
			conversations |= widget.at("kind") == "conversations";
		view_.gameplay["conversations"] = Json::object();
		if (conversations && sample_state != "empty")
		{
			std::string text = "We have received your signal. Please confirm your approach.";
			if (sample_state == "long text")
				text += " This isolated example deliberately includes a longer report, additional operational detail "
				        "and a request for clarification to preview wrapping and scrolling at enlarged text sizes.";
			view_.gameplay["conversations"] = {
			    {"selected", 1},
			    {"directory", Json::array()},
			    {"threads",
			     Json::array({{{"id", 1},
			                   {"prompt", 1},
			                   {"title", "Approach clearance"},
			                   {"unread", true},
			                   {"text_allowed", true},
			                   {"members", Json::array({{{"name", "Harbour control"}, {"connected", true}}})},
			                   {"messages", Json::array({{{"id", 1},
			                                              {"name", "Harbour control"},
			                                              {"text", text},
			                                              {"own", false},
			                                              {"literal", true}}})},
			                   {"incoming", Json::array()},
			                   {"choices", Json::array({{{"id", "confirm"}, {"text", "Confirm approach"}},
			                                            {{"id", "wait"},
			                                             {"text", "Request holding position"},
			                                             {"enabled", false},
			                                             {"reason", "Waiting for clearance"}}})}}})}};
		}
		if (!value.contains("name"))
			value["name"] = value.at("id");
		value["id"] = "preview.console";
		if (catalog_.consoles["preview.console"] != value)
		{
			catalog_.consoles["preview.console"] = std::move(value);
			++generation_;
		}
		view_.gameplay["controls"] = Json::array();
		if ((sample_state == "engineering" || sample_state == "faults") && stars >= 2)
			view_.gameplay["controls"].push_back({{"id", "reactor.output"},
			                                      {"action", "generator_output"},
			                                      {"controller", "reactor"},
			                                      {"channel", "output"},
			                                      {"value", 70},
			                                      {"label", "Reactor output"}});

		for (const auto &widget : catalog_.consoles.at("preview.console").at("widgets"))
		{
			const auto kind = widget.at("kind").get<std::string>();
			if (kind != "camera_mode" && kind != "camera_direction")
				continue;
			for (int i = 0; i < (kind == "camera_mode" ? 2 : 8); ++i)
				view_.gameplay["controls"].push_back(
				    {{"id", kind + "." + std::to_string(i)},
				     {"action", kind},
				     {"system", ""},
				     {"value", i},
				     {"label", kind == "camera_mode" ? (i ? "Third person" : "First person") : camera_directions[i]}});
		}
		widget_layout(widget_context, stars);
	}
	void add_pack(const Bytes &) override
	{
	}
	void clear_packs() override
	{
	}
	const Catalog &catalog() const override
	{
		return catalog_;
	}
	void start(const GameSetup &) override
	{
	}
	bool running() const override
	{
		return true;
	}
	bool recovery_pending() const override
	{
		return false;
	}
	Bytes save() const override
	{
		return {};
	}
	void restore(const Bytes &) override
	{
	}
	void resume_local() override
	{
	}
	void tick() override
	{
	}
	std::uint64_t ticks() const override
	{
		return 1;
	}
	std::uint64_t generation() const override
	{
		return generation_;
	}
	bool command(const OperatingCommand &value) override
	{
		if (value.target != 1)
			return false;
		if (!value.controller.empty())
		{
			for (const auto &controller : view_.gameplay.at("controllers"))
				if (controller.at("id") == value.controller && controller.at("generation") == value.generation &&
				    controller.at("commands").contains(value.channel))
				{
					const auto &channel = controller.at("commands").at(value.channel);
					if (!channel.at("human").get<bool>() || channel.at("action") != value.action ||
					    value.value < channel.at("minimum") || value.value > channel.at("maximum"))
						return false;
					widget_settings_[value.controller + "/" + value.channel] = value.value;
					widget_requests_[value.controller + "/" + value.channel] = value.request_id;
					++generation_;
					return true;
				}
			return false;
		}
		if (value.action == "speed_mm_s")
			view_.requested_speed = std::clamp<std::int64_t>(value.value, 0, 30000);
		else if (value.action == "heading_mdeg")
			view_.requested_heading = value.value;
		else if (value.action == "camera_mode" || value.action == "camera_direction")
		{
			if (value.value < 0 || value.value > (value.action == "camera_mode" ? 1 : 7))
				return false;
			auto camera = ship_camera_state(view_.gameplay);
			if (value.action == "camera_mode")
				camera.mode = static_cast<int>(value.value);
			else
				camera.direction = static_cast<int>(value.value);
			view_.gameplay["shared"] = Json::array(
			    {{{"schema", "phoenix.camera"}, {"fields", {{"mode", camera.mode}, {"direction", camera.direction}}}}});
		}
		else
			return false;
		return true;
	}
	std::optional<Observation> observation() const override
	{
		return view_;
	}
	std::vector<BodyView> viewscreen() const override
	{
		return {};
	}
};
std::shared_ptr<PreviewSession> preview_session;
Application preview_app;
std::unique_ptr<ConsoleSurface> preview_surface;
RenderTexture2D preview_texture{};
UiRenderer renderer;
} // namespace
const UiResolved &console_preview_geometry()
{
	return preview_surface->authored_geometry();
}
Texture2D draw_console_preview(const Json &layout, int width, int height, Vector2 pointer, bool pressed, bool released,
                               float wheel, bool pseudo, std::shared_ptr<const Catalog> authoring, int text_scale)
{
	width = std::clamp(width, 100, 1920);
	height = std::clamp(height, 100, 1920);
	if (!preview_session)
	{
		preview_session = std::make_shared<PreviewSession>();
		preview_app.session = preview_session;
		preview_app.screen = Screen::console;
		preview_surface = std::make_unique<ConsoleSurface>(1, "preview-", false, &renderer);
	}
	preview_session->layout(layout, authoring);
	preview_surface->preview_text_scale(text_scale);
	preview_surface->preview_pages(layout);
	preview_app.pseudo_locale = pseudo;
	preview_app.screen_controls.clear();
	preview_app.screen_toggles.clear();
	preview_app.control_placements.clear();
	ConsoleRegion region{1, 0, 0, static_cast<float>(width), static_cast<float>(height)};
	preview_surface->prepare(preview_app, region, {});
	auto &context = preview_surface->context();
	context.ProcessMouseMove(static_cast<int>(pointer.x), static_cast<int>(pointer.y), 0);
	const bool heading = preview_surface->pointer(0, pointer.x, pointer.y,
	                                              pressed    ? DeviceEventKind::Down
	                                              : released ? DeviceEventKind::Up
	                                                         : DeviceEventKind::Move);
	if (pressed && !heading)
		context.ProcessMouseButtonDown(0, 0);
	if (released && !heading)
		context.ProcessMouseButtonUp(0, 0);
	if (wheel)
		context.ProcessMouseWheel(-wheel, 0);
	preview_surface->update_view();
	if (!preview_texture.id || preview_texture.texture.width != width || preview_texture.texture.height != height)
	{
		if (preview_texture.id)
			UnloadRenderTexture(preview_texture);
		preview_texture = LoadRenderTexture(width, height);
	}
	BeginTextureMode(preview_texture);
	ClearBackground({8, 15, 25, 255});
	preview_surface->draw(region);
	renderer.viewport({0, 0, static_cast<float>(width), static_cast<float>(height)});
	context.Render();
	renderer.finish();
	EndTextureMode();
	return preview_texture.texture;
}

void clear_console_preview()
{
	preview_surface.reset();
	preview_session.reset();
	preview_app.session.reset();
	if (preview_texture.id)
		UnloadRenderTexture(preview_texture);
	preview_texture = {};
}
Json console_preview_state()
{
	return preview_session ? preview_session->observation()->gameplay : Json::object();
}
} // namespace phoenix
