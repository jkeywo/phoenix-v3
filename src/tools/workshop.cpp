#include <phoenix/presentation.hpp>
#include <imgui.h>
#include <raylib.h>
#include <rlgl.h>
#include <cstdint>
namespace phoenix {
namespace { Texture2D font_texture{}; }
void begin_tools() {
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* pixels; int width, height;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    Image image{pixels, width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    font_texture = LoadTextureFromImage(image);
    ImGui::GetIO().Fonts->SetTexID(static_cast<ImTextureID>(font_texture.id));
    ImGui::StyleColorsDark();
}
void draw_workshop(Application& app) {
    auto& io = ImGui::GetIO();
    io.DisplaySize = {static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())};
    io.DeltaTime = GetFrameTime() > 0 ? GetFrameTime() : 1.0f / 60;
    io.AddMousePosEvent(static_cast<float>(GetMouseX()), static_cast<float>(GetMouseY()));
    for (int i = 0; i < 3; ++i) io.AddMouseButtonEvent(i, IsMouseButtonDown(i));
    io.AddMouseWheelEvent(0, GetMouseWheelMove());
    io.AddKeyEvent(ImGuiMod_Ctrl, IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL));
    io.AddKeyEvent(ImGuiMod_Shift, IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT));
    const std::pair<int, ImGuiKey> keys[] = {
        {KEY_TAB,ImGuiKey_Tab},{KEY_ENTER,ImGuiKey_Enter},{KEY_SPACE,ImGuiKey_Space},
        {KEY_ESCAPE,ImGuiKey_Escape},{KEY_BACKSPACE,ImGuiKey_Backspace},{KEY_DELETE,ImGuiKey_Delete},
        {KEY_LEFT,ImGuiKey_LeftArrow},{KEY_RIGHT,ImGuiKey_RightArrow},{KEY_UP,ImGuiKey_UpArrow},{KEY_DOWN,ImGuiKey_DownArrow},
        {KEY_HOME,ImGuiKey_Home},{KEY_END,ImGuiKey_End},{KEY_A,ImGuiKey_A},{KEY_C,ImGuiKey_C},{KEY_V,ImGuiKey_V},{KEY_X,ImGuiKey_X},{KEY_Z,ImGuiKey_Z}
    };
    for (auto [key, imgui_key] : keys) io.AddKeyEvent(imgui_key, IsKeyDown(key));
    for (int character; (character = GetCharPressed()) != 0;) io.AddInputCharacter(character);
    ImGui::NewFrame();
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Phoenix")) {
            if (ImGui::MenuItem("Home")) app.screen = Screen::home;
            ImGui::EndMenu();
        }
        ImGui::TextUnformatted(app.screen == Screen::gm ? "GM inspector" : "Workshop");
        ImGui::EndMainMenuBar();
    }
    ImGui::SetNextWindowPos({20,60}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({220,430}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Entities");
    ImGui::TextUnformatted("No map loaded.");
    ImGui::End();
    ImGui::SetNextWindowPos({260,60}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({650,570}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Map");
    ImGui::TextUnformatted("Map authoring follows content integration.");
    auto origin = ImGui::GetCursorScreenPos();
    auto size = ImGui::GetContentRegionAvail();
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, {origin.x + size.x,origin.y + size.y}, IM_COL32(8,15,25,255));
    for (float x = 0; x < size.x; x += 40) draw->AddLine({origin.x+x,origin.y},{origin.x+x,origin.y+size.y},IM_COL32(32,58,70,255));
    for (float y = 0; y < size.y; y += 40) draw->AddLine({origin.x,origin.y+y},{origin.x+size.x,origin.y+y},IM_COL32(32,58,70,255));
    ImGui::End();
    ImGui::SetNextWindowPos({930,60}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({300,430}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Properties"); ImGui::TextWrapped("Select an entity to inspect its properties."); ImGui::End();
    ImGui::Render();
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    const ImDrawData* data = ImGui::GetDrawData();
    for (const auto* list : data->CmdLists) {
        for (const auto& command : list->CmdBuffer) {
            if (command.UserCallback) {
                if (command.UserCallback != ImDrawCallback_ResetRenderState) command.UserCallback(list, &command);
                continue;
            }
            const auto clip = command.ClipRect;
            BeginScissorMode(static_cast<int>(clip.x), static_cast<int>(clip.y), static_cast<int>(clip.z-clip.x), static_cast<int>(clip.w-clip.y));
            rlDrawRenderBatchActive();
            rlSetTexture(static_cast<unsigned>(command.GetTexID()));
            rlBegin(RL_TRIANGLES);
            for (unsigned i = 0; i < command.ElemCount; ++i) {
                const auto& v = list->VtxBuffer[list->IdxBuffer[command.IdxOffset+i] + command.VtxOffset];
                const auto c = v.col;
                rlColor4ub(c & 255, (c >> 8) & 255, (c >> 16) & 255, (c >> 24) & 255);
                rlTexCoord2f(v.uv.x, v.uv.y); rlVertex2f(v.pos.x, v.pos.y);
            }
            rlEnd(); rlSetTexture(0); EndScissorMode();
        }
    }
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}
void end_tools() { UnloadTexture(font_texture); ImGui::DestroyContext(); }
}
