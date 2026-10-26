module;
#include <imgui.h>
#include <imgui_internal.h>
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
#include "tool_theme.hpp"
void render_editor(Application &app);
void close_editor();
namespace
{
Texture2D font_texture{};
ImGuiStyle base_style;
int applied_scale = 0;
struct ToolArtwork
{
	Texture2D texture{};
	std::array<float, 4> slices{};
	float scale = 1;
};
std::map<std::string, ToolArtwork> tool_artwork;
std::map<std::string, Texture2D> tool_textures;
std::string tool_theme_identity;
Json tool_theme;
void clear_tool_artwork()
{
	for (auto &[ref, texture] : tool_textures)
		if (texture.id)
			UnloadTexture(texture);
	tool_textures.clear();
	tool_artwork.clear();
}
ImVec4 tool_colour(const char *name)
{
	const auto value = tool_theme.at("tokens").at(name).get<std::string>();
	const auto c =
	    GetColor(static_cast<unsigned>(std::stoul(value.substr(1) + (value.size() == 7 ? "ff" : ""), nullptr, 16)));
	return {c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f};
}
void draw_tool_artwork(ImDrawList &draw, const ToolArtwork &art, ImVec2 a, ImVec2 b, ImU32 tint)
{
	const float scale = art.scale * applied_scale / 100.f;
	const auto &s = art.slices;
	const float horizontal = std::min(1.f, (b.x - a.x) / std::max(1.f, (s[1] + s[3]) * scale));
	const float vertical = std::min(1.f, (b.y - a.y) / std::max(1.f, (s[0] + s[2]) * scale));
	const float xs[]{a.x, a.x + s[3] * scale * horizontal, b.x - s[1] * scale * horizontal, b.x};
	const float ys[]{a.y, a.y + s[0] * scale * vertical, b.y - s[2] * scale * vertical, b.y};
	const float us[]{0, s[3] / art.texture.width, 1 - s[1] / art.texture.width, 1};
	const float vs[]{0, s[0] / art.texture.height, 1 - s[2] / art.texture.height, 1};
	for (int y = 0; y < 3; ++y)
		for (int x = 0; x < 3; ++x)
			draw.AddImage(static_cast<ImTextureID>(art.texture.id), {xs[x], ys[y]}, {xs[x + 1], ys[y + 1]},
			              {us[x], vs[y]}, {us[x + 1], vs[y + 1]}, tint);
}
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
void apply_tool_theme(const Catalog &catalog)
{
	std::string identity =
	    catalog.ui_theme + std::to_string(applied_scale) + std::to_string(reinterpret_cast<std::uintptr_t>(&catalog));
	for (const auto &[id, theme] : catalog.themes)
		identity += id + theme.dump();
	if (identity == tool_theme_identity)
		return;
	const auto theme = resolved_ui_theme(catalog);
	clear_tool_artwork();
	tool_theme = theme;
	auto &style = ImGui::GetStyle();
	auto &c = style.Colors;
	c[ImGuiCol_Text] = tool_colour("text");
	c[ImGuiCol_TextDisabled] = tool_colour("muted");
	c[ImGuiCol_WindowBg] = tool_colour("background");
	c[ImGuiCol_ChildBg] = tool_colour("background");
	c[ImGuiCol_PopupBg] = tool_colour("panel");
	c[ImGuiCol_MenuBarBg] = tool_colour("panel");
	c[ImGuiCol_Border] = tool_colour("border");
	for (int key : {ImGuiCol_FrameBg, ImGuiCol_Button, ImGuiCol_Header, ImGuiCol_Tab, ImGuiCol_TitleBg,
	                ImGuiCol_TitleBgActive, ImGuiCol_TabDimmed, ImGuiCol_TableHeaderBg})
		c[key] = tool_colour("panel");
	for (int key : {ImGuiCol_ButtonHovered, ImGuiCol_FrameBgHovered, ImGuiCol_HeaderHovered, ImGuiCol_TabHovered})
		c[key] = ImLerp(tool_colour("panel"), tool_colour("accent"), .25f);
	for (int key : {ImGuiCol_ButtonActive, ImGuiCol_FrameBgActive, ImGuiCol_HeaderActive, ImGuiCol_TabSelected,
	                ImGuiCol_TabDimmedSelected, ImGuiCol_DockingPreview, ImGuiCol_SliderGrab, ImGuiCol_CheckMark,
	                ImGuiCol_SeparatorActive, ImGuiCol_ResizeGripActive})
		c[key] = tool_colour("accent");
	c[ImGuiCol_NavCursor] = tool_colour("focus");
	c[ImGuiCol_SliderGrabActive] = tool_colour("focus");
	style.FrameBorderSize = theme.at("tokens").value("border_width", 1.f) * applied_scale / 100.f;
	style.FrameRounding = theme.at("tokens").value("radius", 4.f) * applied_scale / 100.f;
	for (const auto &[slot, reference] : theme.at("assets").items())
		if (slot == "button" || slot.starts_with("button-") || slot == "panel")
		{
			const auto ref = reference.get<std::string>();
			if (!tool_textures.contains(ref))
			{
				const auto &bytes = catalog.assets.at(ref);
				const auto image = decode_asset_image(bytes);
				tool_textures[ref] = LoadTextureFromImage({const_cast<unsigned char *>(image.rgba.data()), image.width,
				                                           image.height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8});
			}
			ToolArtwork art;
			art.texture = tool_textures.at(ref);
			if (theme.contains("slices") && theme.at("slices").contains(slot))
				for (int edge = 0; edge < 4; ++edge)
					art.slices[edge] = theme.at("slices").at(slot).at(edge);
			art.scale = theme.value("asset_scale", Json::object()).value(slot, 1.f);
			tool_artwork[slot] = art;
		}
	tool_theme_identity = identity;
}
bool tool_button(const char *label, const ImVec2 &size)
{
	if (!tool_artwork.contains("button"))
		return ImGui::Button(label, size);
	auto *draw = ImGui::GetWindowDrawList();
	ImDrawListSplitter split;
	split.Split(draw, 2);
	split.SetCurrentChannel(draw, 1);
	for (auto colour : {ImGuiCol_Button, ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive})
		ImGui::PushStyleColor(colour, ImVec4(0, 0, 0, 0));
	const bool pressed = ImGui::Button(label, size);
	ImGui::PopStyleColor(3);
	const bool disabled = (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
	const auto state = disabled                                           ? "button-disabled"
	                   : ImGui::IsItemActive()                            ? "button-pressed"
	                   : ImGui::IsItemHovered() || ImGui::IsItemFocused() ? "button-hover"
	                                                                      : "button";
	auto art = tool_artwork.at(tool_artwork.contains(state) ? state : "button");
	const float height = ImGui::GetItemRectMax().y - ImGui::GetItemRectMin().y;
	const float edge = std::max(1.f, (height - ImGui::GetTextLineHeight()) / 2);
	art.scale = std::min(art.scale, edge / std::max({1.f, art.slices[0], art.slices[2]}) * 100 / applied_scale);
	const auto tint = disabled                                           ? tool_colour("disabled")
	                  : ImGui::IsItemActive()                            ? tool_colour("pressed")
	                  : ImGui::IsItemHovered() || ImGui::IsItemFocused() ? tool_colour("hover")
	                                                                     : ImVec4(1, 1, 1, 1);
	split.SetCurrentChannel(draw, 0);
	draw_tool_artwork(*draw, art, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(tint));
	split.Merge(draw);
	return pressed;
}
bool tool_small_button(const char *label)
{
	const auto padding = ImGui::GetStyle().FramePadding;
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padding.x, 0));
	const bool pressed = tool_button(label);
	ImGui::PopStyleVar();
	return pressed;
}
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
	if (tool_artwork.contains("panel"))
		for (auto *window : ImGui::GetCurrentContext()->Windows)
			if (window->Active && !window->Hidden && !window->Collapsed && !window->DockNodeAsHost &&
			    !(window->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Tooltip)))
			{
				auto &draw = *window->DrawList;
				const ImVec2 end{window->Pos.x + window->Size.x, window->Pos.y + window->Size.y};
				draw.PushClipRect(window->Pos, end, false);
				draw_tool_artwork(draw, tool_artwork.at("panel"), window->Pos, end,
				                  ImGui::GetColorU32(ImVec4(1, 1, 1, 1)));
				draw.PopClipRect();
			}
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
	clear_tool_artwork();
	tool_theme_identity.clear();
	UnloadTexture(font_texture);
	ImGui::DestroyContext();
}
} // namespace phoenix
