module;
#include <imgui.h>
#include <string>

module phoenix.workshop.shell;

namespace phoenix
{
bool draw_tool_preferences(ToolPreferences &preferences)
{
	bool changed = false;
	if (ImGui::BeginMenu(tool_label("Presentation").c_str()))
	{
		if (ImGui::BeginMenu(tool_label("Tool scale").c_str()))
		{
			for (int scale = 75; scale <= 200; scale += 25)
				if (ImGui::MenuItem((std::to_string(scale) + "%").c_str(), nullptr, preferences.scale == scale))
				{
					preferences.scale = scale;
					changed = true;
				}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(tool_label("Language").c_str()))
		{
			for (const auto &language : installed_languages())
				if (ImGui::MenuItem(tool_label(language == "en" ? "English" : language).c_str(), nullptr,
				                    preferences.language == language))
				{
					preferences.language = language;
					changed = true;
				}
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem(tool_label("Pseudo-localisation").c_str(), nullptr, preferences.pseudo))
		{
			preferences.pseudo = !preferences.pseudo;
			changed = true;
		}
		ImGui::EndMenu();
	}

	if (changed)
		select_language(preferences.language, preferences.pseudo);
	return changed;
}
} // namespace phoenix
