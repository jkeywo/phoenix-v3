module;
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <raylib.h>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

export module phoenix.workshop.shell;
export import phoenix.content.catalog;
export import phoenix.application.preferences;

export namespace phoenix
{
struct ToolMessage
{
	std::string key;
	std::map<std::string, std::string> parameters;
	mutable std::string rendered;
	ToolMessage() = default;
	ToolMessage(const char *text) : key(text)
	{
	}
	ToolMessage(std::string text, std::map<std::string, std::string> values = {})
	    : key(std::move(text)), parameters(std::move(values))
	{
	}
	const char *c_str() const
	{
		const auto translated = localise(key);
		rendered.clear();
		for (std::size_t i = 0; i < translated.size();)
		{
			const auto end = translated[i] == '{' ? translated.find('}', i + 1) : std::string::npos;
			if (end != std::string::npos)
				if (const auto found = parameters.find(translated.substr(i + 1, end - i - 1));
				    found != parameters.end())
				{
					rendered += found->second;
					i = end + 1;
					continue;
				}
			rendered += translated[i++];
		}
		return rendered.c_str();
	}
};

struct ScopedId
{
	inline explicit ScopedId(const char *value)
	{
		ImGui::PushID(value);
	}
	inline explicit ScopedId(int value)
	{
		ImGui::PushID(value);
	}
	inline ~ScopedId()
	{
		ImGui::PopID();
	}
	ScopedId(const ScopedId &) = delete;
};

inline bool edit_value(const char *label, std::string &value)
{
	return ImGui::InputText(label, &value);
}
inline bool edit_value(const char *label, std::int64_t &value)
{
	return ImGui::InputScalar(label, ImGuiDataType_S64, &value);
}
inline bool edit_value(const char *label, bool &value)
{
	return ImGui::Checkbox(label, &value);
}
inline bool edit_value(const char *label, double &value)
{
	return ImGui::InputDouble(label, &value, 0.01, 0.1, "%.3f");
}
bool edit_field(const char *label, Json &object, const char *key, std::string fallback);
bool edit_field(const char *label, Json &object, const char *key, std::int64_t fallback);
bool edit_field(const char *label, Json &object, const char *key, bool fallback);
bool edit_field(const char *label, Json &object, const char *key, double fallback);
void array_rows(Json &rows, const std::function<bool(Json &, std::size_t)> &inspect);
void object_rows(Json &rows, const std::function<bool(const std::string &, Json &)> &inspect);
void append_row(Json &parent, const char *key, Json value);

// Keep long translated headings reachable at larger type sizes.
inline bool tool_window(const char *label)
{
	if (std::string(label).ends_with("###Properties"))
		ImGui::SetNextWindowContentSize({ImGui::GetFontSize() * 32.f, 0});
	return ImGui::Begin(label, nullptr, ImGuiWindowFlags_HorizontalScrollbar);
}

inline std::string tool_label(const std::string &text)
{
	return localise(text) + "###" + text;
}

struct ToolPointerSample
{
	enum Phase
	{
		down,
		move,
		up,
		cancel
	};
	Phase phase;
	std::uint64_t id;
	float x, y;
};
struct ToolFrame
{
	Rectangle area{};
	bool keyboard = false;
	int scale = 100;
	std::string language = "en";
	bool pseudo = false;
	std::span<const ToolPointerSample> pointers;
};
struct ToolPreferences
{
	int scale = 100;
	std::string language = "en";
	bool pseudo = false;
};
bool draw_tool_preferences(ToolPreferences &);
void tool_real_field(const char *, Json &, const char *, float fallback, float minimum, float maximum);
void tool_colour_field(const char *, Json &, const char *, const Json &fallback, bool alpha);
void apply_tool_theme(const Catalog &);
bool tool_button(const char *label, const ImVec2 &size = ImVec2(0, 0));
bool tool_small_button(const char *label);
void begin_tool_frame_runtime();
void draw_tool_frame(const ToolFrame &, const std::function<void()> &);
void end_tool_frame_runtime();
bool probe_tool_frame_presentation(int scale, bool pseudo);
} // namespace phoenix
