module;
#include <imgui.h>
#include <raylib.h>
#include <rlgl.h>
#include <cstdint>
#include <functional>
#include <memory>
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
}
void begin_tools()
{
	enable_workshop_file_shortcuts();
	ImGui::CreateContext();
	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	ImGui::GetIO().IniFilename = nullptr;
	unsigned char *pixels;
	int width, height;
	ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	Image image{pixels, width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
	font_texture = LoadTextureFromImage(image);
	ImGui::GetIO().Fonts->SetTexID(static_cast<ImTextureID>(font_texture.id));
	ImGui::StyleColorsDark();
}
void draw_workshop(Application &app)
{
	auto &io = ImGui::GetIO();
	Rectangle area{0, 0, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())};
	if (app.embedded_tools && app.gm_region)
	{
		const auto &r = *app.gm_region;
		area = {r.x, r.y, r.width, r.height};
	}
	io.DisplaySize = {area.width, area.height};
	io.DeltaTime = GetFrameTime() > 0 ? GetFrameTime() : 1.0f / 60;
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
	bool keyboard = graphics_has_keyboard_focus() && (!app.embedded_tools || app.tools_keyboard_focus);
	static bool previous_focus = true;
	if (keyboard != previous_focus)
	{
		io.AddFocusEvent(keyboard);
		previous_focus = keyboard;
	}
	io.AddKeyEvent(ImGuiMod_Ctrl, keyboard && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)));
	io.AddKeyEvent(ImGuiMod_Shift, keyboard && (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)));
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
	                                         {KEY_Z, ImGuiKey_Z}};
	for (auto [key, imgui_key] : keys)
		io.AddKeyEvent(imgui_key, keyboard && IsKeyDown(key));
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
	if (!app.can_host || !app.session || !app.session->running())
		return;
	Application inspector = app;
	inspector.screen = Screen::gm;
	inspector.embedded_tools = true;
	draw_workshop(inspector);
}
void end_tools()
{
	close_editor();
	UnloadTexture(font_texture);
	ImGui::DestroyContext();
}
} // namespace phoenix
