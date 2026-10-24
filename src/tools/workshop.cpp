module;
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <raylib.h>
#include <rlgl.h>
#include <cstdint>
#include <array>
#include <functional>
#include <memory>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

module phoenix.tools;
import phoenix.presentation;
import phoenix.files;

namespace phoenix
{
void render_editor(Application &app);
void close_editor();
namespace
{
Texture2D font_texture{};
ImGuiStyle base_style;
int applied_scale = 0;
void scale_tools(int percent)
{
	percent = std::clamp(percent, 75, 200);
	if (percent == applied_scale)
		return;
	const float scale = percent / 100.f;
	auto &io = ImGui::GetIO();
	io.Fonts->Clear();
	static const ImWchar ranges[] = {0x20, 0x024f, 0x2000, 0x206f, 0};
#ifdef __EMSCRIPTEN__
	const std::string font = "/assets/LatoLatin-Regular.ttf";
#else
	const std::string font = std::string(GetApplicationDirectory()) + "assets/LatoLatin-Regular.ttf";
#endif
	if (!io.Fonts->AddFontFromFileTTF(font.c_str(), 16.f * scale, nullptr, ranges))
		throw std::runtime_error("Cannot load Workshop font: " + font);
	unsigned char *pixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	const auto next = LoadTextureFromImage({pixels, width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8});
	if (font_texture.id)
		UnloadTexture(font_texture);
	font_texture = next;
	io.Fonts->SetTexID(static_cast<ImTextureID>(font_texture.id));
	ImGui::GetStyle() = base_style;
	ImGui::GetStyle().ScaleAllSizes(scale);
	ImGui::GetStyle().WindowMinSize.y = std::max(ImGui::GetStyle().WindowMinSize.y, 64.f * scale);
	applied_scale = percent;
}
} // namespace
void begin_tools()
{
	enable_workshop_file_shortcuts();
	ImGui::CreateContext();
	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	ImGui::GetIO().IniFilename = nullptr;
	ImGui::StyleColorsDark();
	base_style = ImGui::GetStyle();
	applied_scale = 0;
	scale_tools(100);
}
void draw_workshop(Application &app)
{
	scale_tools(app.tools_scale);
	select_language(app.language, app.pseudo_locale);
	auto &io = ImGui::GetIO();
	Rectangle area{0, 0, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())};
	if (app.embedded_tools && app.gm_region)
	{
		const auto &r = *app.gm_region;
		area = {r.x, r.y, r.width, r.height};
	}
	io.DisplaySize = {area.width, area.height};
	io.DeltaTime = GetFrameTime() > 0 ? GetFrameTime() : 1.0f / 60;
	static std::map<std::uint64_t, Vector2> touches;
	if (touches.empty() && app.tools_pointers.empty())
	{
		io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
		io.AddMousePosEvent(GetMouseX() - area.x, GetMouseY() - area.y);
		const bool hovered = CheckCollisionPointRec(GetMousePosition(), area);
		static bool mouse_capture = false;
		if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
			mouse_capture = true;
		for (int i = 0; i < 3; ++i)
			io.AddMouseButtonEvent(i, (hovered || mouse_capture) && IsMouseButtonDown(i));
		if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
			mouse_capture = false;
		io.AddMouseWheelEvent(0, hovered ? GetMouseWheelMove() : 0);
	}
	if (!IsWindowFocused())
	{
		touches.clear();
		io.AddMouseButtonEvent(0, false);
	}
	for (const auto &event : app.tools_pointers)
	{
		using P = Application::ToolPointer;
		const Vector2 point{event.x - area.x, event.y - area.y};
		if (event.phase == P::down && CheckCollisionPointRec({event.x, event.y}, area))
			touches[event.id] = point;
		else if (touches.contains(event.id))
		{
			if (event.phase == P::up || event.phase == P::cancel)
				touches.erase(event.id);
			else
				touches[event.id] = point;
		}
		else
			continue;
		io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
		io.AddMousePosEvent(point.x, point.y);
		io.AddMouseButtonEvent(0, touches.size() == 1 && event.phase != P::cancel);
	}
	if (!touches.empty())
	{
		const auto point = touches.begin()->second;
		io.AddMousePosEvent(point.x, point.y);
		io.AddMouseButtonEvent(0, touches.size() == 1);
	}
	bool keyboard = graphics_has_keyboard_focus() && (!app.embedded_tools || app.tools_keyboard_focus);
	static bool previous_focus = true;
	if (keyboard != previous_focus)
	{
		io.AddFocusEvent(keyboard);
		previous_focus = keyboard;
	}
	// Key down/up can both arrive between rendered frames (paste and quick
	// shortcuts in particular). Retain queued presses as well as held states.
	std::array<bool, 512> pressed{};
	if (keyboard)
		for (int key; (key = GetKeyPressed()) != 0;)
			if (key > 0 && key < static_cast<int>(pressed.size()))
				pressed[key] = true;
	io.AddKeyEvent(ImGuiMod_Ctrl, keyboard && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
	                                           pressed[KEY_LEFT_CONTROL] || pressed[KEY_RIGHT_CONTROL]));
	io.AddKeyEvent(ImGuiMod_Shift, keyboard && (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ||
	                                            pressed[KEY_LEFT_SHIFT] || pressed[KEY_RIGHT_SHIFT]));
	const std::pair<int, ImGuiKey> keys[] = {{KEY_TAB, ImGuiKey_Tab},
	                                         {KEY_ENTER, ImGuiKey_Enter},
	                                         {KEY_SPACE, ImGuiKey_Space},
	                                         {KEY_ESCAPE, ImGuiKey_Escape},
	                                         {KEY_BACKSPACE, ImGuiKey_Backspace},
	                                         {KEY_DELETE, ImGuiKey_Delete},
	                                         {KEY_LEFT, ImGuiKey_LeftArrow},
	                                         {KEY_RIGHT, ImGuiKey_RightArrow},
	                                         {KEY_UP, ImGuiKey_UpArrow},
	                                         {KEY_DOWN, ImGuiKey_DownArrow},
	                                         {KEY_HOME, ImGuiKey_Home},
	                                         {KEY_END, ImGuiKey_End},
	                                         {KEY_A, ImGuiKey_A},
	                                         {KEY_S, ImGuiKey_S},
	                                         {KEY_C, ImGuiKey_C},
	                                         {KEY_V, ImGuiKey_V},
	                                         {KEY_X, ImGuiKey_X},
	                                         {KEY_Y, ImGuiKey_Y},
	                                         {KEY_Z, ImGuiKey_Z}};
	for (auto [key, imgui_key] : keys)
		io.AddKeyEvent(imgui_key, keyboard && (IsKeyDown(key) || pressed[key]));
	if (!app.embedded_tools || app.tools_keyboard_focus)
		for (int character; (character = GetCharPressed()) != 0;)
			if (keyboard)
				io.AddInputCharacter(character);
	ImGui::NewFrame();
	render_editor(app);
	ImGui::Render();
	rlDrawRenderBatchActive();
	rlDisableBackfaceCulling();
	const ImDrawData *data = ImGui::GetDrawData();
	for (const auto *list : data->CmdLists)
	{
		for (const auto &command : list->CmdBuffer)
		{
			if (command.UserCallback)
			{
				if (command.UserCallback != ImDrawCallback_ResetRenderState)
					command.UserCallback(list, &command);
				continue;
			}
			const auto clip = command.ClipRect;
			BeginScissorMode(static_cast<int>(clip.x + area.x), static_cast<int>(clip.y + area.y),
			                 static_cast<int>(clip.z - clip.x), static_cast<int>(clip.w - clip.y));
			rlDrawRenderBatchActive();
			rlSetTexture(static_cast<unsigned>(command.GetTexID()));
			rlBegin(RL_TRIANGLES);
			for (unsigned i = 0; i < command.ElemCount; ++i)
			{
				const auto &v = list->VtxBuffer[list->IdxBuffer[command.IdxOffset + i] + command.VtxOffset];
				const auto c = v.col;
				rlColor4ub(c & 255, (c >> 8) & 255, (c >> 16) & 255, (c >> 24) & 255);
				rlTexCoord2f(v.uv.x, v.uv.y);
				rlVertex2f(v.pos.x + area.x, v.pos.y + area.y);
			}
			rlEnd();
			rlSetTexture(0);
			EndScissorMode();
		}
	}
	rlDrawRenderBatchActive();
	rlEnableBackfaceCulling();
}
void draw_gm_region(Application &app)
{
	if (!app.can_host || !app.session || (!app.session->running() && !app.session->lobby().active))
		return;
	Application inspector = app;
	inspector.screen = Screen::gm;
	inspector.embedded_tools = true;
	draw_workshop(inspector);
	app.tools_scale = inspector.tools_scale;
	app.language = inspector.language;
	app.pseudo_locale = inspector.pseudo_locale;
}
bool probe_tool_presentation(const Application &app)
{
	const auto &io = ImGui::GetIO();
	if (applied_scale != app.tools_scale || io.Fonts->Fonts.empty() ||
	    std::abs(io.Fonts->Fonts[0]->LegacySize - 16.f * app.tools_scale / 100.f) > .1f ||
	    !io.Fonts->Fonts[0]->IsGlyphInFont(0x00e9) ||
	    std::abs(ImGui::GetStyle().WindowPadding.x - base_style.WindowPadding.x * app.tools_scale / 100.f) > 1.f)
		return false;
	const ToolMessage message("Imported {file}", {{"file", "R\xc3\xa9sum\xc3\xa9-{file}.zip"}});
	const std::string rendered = message.c_str();
	return rendered.find("R\xc3\xa9sum\xc3\xa9-{file}.zip") != std::string::npos &&
	       (rendered.starts_with("[ ") == app.pseudo_locale);
}

void end_tools()
{
	close_editor();
	UnloadTexture(font_texture);
	ImGui::DestroyContext();
}
} // namespace phoenix
