module;
#include <raylib.h>
#include <imgui.h>
#include <functional>
#include <vector>

module phoenix.tools;
import phoenix.workshop.shell;
import phoenix.presentation;
import phoenix.navigation;
import phoenix.workshop.gm;
import phoenix.workshop.workflow;

namespace phoenix
{
namespace
{
GmInspector gm_inspector;
WorkshopWorkspace workspace;
void draw_composed_tools(Application &app, bool embedded, const std::function<void(const ToolFrame &)> &render)
{
	ToolFrame frame;
	frame.area = {0, 0, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())};
	if (embedded && app.gm_region)
	{
		const auto &region = *app.gm_region;
		frame.area = {region.x, region.y, region.width, region.height};
	}
	frame.keyboard = graphics_has_keyboard_focus() && (!embedded || app.tools_keyboard_focus);
	frame.scale = app.tools_scale;
	frame.language = app.language;
	frame.pseudo = app.pseudo_locale;
	std::vector<ToolPointerSample> pointers;
	for (const auto &event : app.tools_pointers)
		pointers.push_back({static_cast<ToolPointerSample::Phase>(event.phase), event.id, event.x, event.y});
	frame.pointers = pointers;
	draw_tool_frame(frame,
	                [&]
	                {
		                render(frame);
	                });
}
void draw_gm(Application &app, const ToolFrame &frame, bool embedded)
{
	if (ImGui::BeginMainMenuBar())
	{
		if (!embedded && ImGui::BeginMenu(tool_label("Phoenix").c_str()))
		{
			if (ImGui::MenuItem(tool_label(game_active(app) ? "Settings" : "Home").c_str()))
				invoke_navigation(app, game_active(app) ? "settings" : "home");
			ImGui::EndMenu();
		}
		ToolPreferences preferences{app.tools_scale, app.language, app.pseudo_locale};
		if (draw_tool_preferences(preferences))
		{
			app.tools_scale = preferences.scale;
			app.language = preferences.language;
			app.pseudo_locale = preferences.pseudo;
			remember_preferences(app);
		}
		ImGui::EndMainMenuBar();
	}
	gm_inspector.draw(app.session, app.status, frame);
}
void draw_authoring(Application &app, const ToolFrame &frame)
{
	WorkshopEnvironment environment;
	environment.library = app.library;
	environment.session = app.session;
	environment.status = app.status;
	environment.active = game_active(app);
	environment.embedded = app.embedded_tools;
	environment.frame = frame;
	environment.preferences = {app.tools_scale, app.language, app.pseudo_locale};
	environment.navigate = [&](const std::string &action)
	{
		invoke_navigation(app, action);
	};
	environment.persist_preferences = [&](const ToolPreferences &preferences)
	{
		app.tools_scale = preferences.scale;
		app.language = preferences.language;
		app.pseudo_locale = preferences.pseudo;
		remember_preferences(app);
	};
	workspace.draw(environment);
}
} // namespace

WorkshopWorkspace &tool_workspace()
{
	return workspace;
}

void begin_tools()
{
	begin_tool_frame_runtime();
}
void draw_workshop(Application &app)
{
	draw_composed_tools(app, app.embedded_tools,
	                    [&](const ToolFrame &frame)
	                    {
		                    if (app.screen == Screen::gm)
			                    draw_gm(app, frame, app.embedded_tools);
		                    else
			                    draw_authoring(app, frame);
	                    });
}
void draw_gm_region(Application &app)
{
	if (!app.can_host || !app.session || (!app.session->running() && !app.session->lobby().active))
		return;
	draw_composed_tools(app, true,
	                    [&](const ToolFrame &frame)
	                    {
		                    draw_gm(app, frame, true);
	                    });
}
bool probe_tool_presentation(const Application &app)
{
	return probe_tool_frame_presentation(app.tools_scale, app.pseudo_locale);
}
void end_tools()
{
	workspace.close();
	gm_inspector.clear();
	end_tool_frame_runtime();
}
} // namespace phoenix
