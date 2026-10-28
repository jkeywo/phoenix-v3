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
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.ui.preview;
import phoenix.content.catalog;
import phoenix.content.ui;

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
	std::string preview_state_;
	std::shared_ptr<const Catalog> authoring_identity_;
	std::uint64_t generation_ = 1;

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
			++generation_;
		}
		value.erase("_preview_pages");
		value.erase("_preview_interact");
		value.erase("_preview_header");
		const auto sample_state = value.value("_preview_state", std::string("normal"));
		value.erase("_preview_state");
		const int stars = value.value("_preview_stars", 3);
		value.erase("_preview_stars");
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
		if (view_.systems.size() > 3)
			view_.systems.resize(3);
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
		preview_surface = std::make_unique<ConsoleSurface>(1, "preview-", false);
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
} // namespace phoenix
