#include <phoenix/presentation.hpp>
#include <raylib.h>
#include <RmlUi/Core.h>
#include "rml_backend.hpp"
#include "game.hpp"
#include <phoenix/semantics.hpp>
#include <stdexcept>
#include <filesystem>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_JS(bool, phoenix_canvas_has_focus, (), { return document.activeElement === Module.canvas; });
EM_JS(int, phoenix_canvas_width, (), { return Math.max(1,Module.canvas.clientWidth); });
EM_JS(int, phoenix_canvas_height, (), { return Math.max(1,Module.canvas.clientHeight); });
#endif
namespace phoenix {
namespace {
UiSystem system;
UiRenderer renderer;
Rml::Context* context = nullptr;
Rml::ElementDocument* document = nullptr;
Application* current_app = nullptr;
Screen shown = Screen::gm;
struct Navigation : Rml::EventListener {
    void ProcessEvent(Rml::Event& event) override {
        if (!current_app) return;
        invoke_navigation(*current_app, event.GetTargetElement()->GetId());
    }
} navigation;
void update_document(Application& app) {
    static bool labels_ready = false;
    static bool pseudo_shown = false;
    if (!labels_ready || pseudo_shown != app.pseudo_locale) {
        for (const auto& control : navigation_controls(app)) if (auto* element = document->GetElementById(control.id)) element->SetInnerRML(control.label);
        labels_ready = true;
        pseudo_shown = app.pseudo_locale;
    }
    if (shown == app.screen) return;
    shown = app.screen;
    document->GetElementById("heading")->SetInnerRML(screen_name(app.screen));
    document->GetElementById("host")->SetProperty("display", app.can_host ? "inline-block" : "none");
    document->GetElementById("edit")->SetProperty("display", app.can_edit ? "inline-block" : "none");
}
}
void open_window() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "Phoenix V3");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);
    install_native_accessibility(GetWindowHandle());
    Rml::SetSystemInterface(&system);
    Rml::SetRenderInterface(&renderer);
    if (!Rml::Initialise()) throw std::runtime_error("RmlUi initialisation failed");
#ifdef __EMSCRIPTEN__
    const std::string font = "/assets/LatoLatin-Regular.ttf";
#else
    const std::string font = std::string(GetApplicationDirectory()) + "assets/LatoLatin-Regular.ttf";
#endif
    if (!Rml::LoadFontFace(font)) throw std::runtime_error("Could not load player UI font: " + font);
    context = Rml::CreateContext("navigation", {GetScreenWidth(), GetScreenHeight()});
    document = context->LoadDocumentFromMemory(R"(<rml><head><style>
    body { display:block; width:100%; box-sizing:border-box; font-family:LatoLatin; font-size:20px; color:#d9e9ef; padding:20px; }
    h1, h2, p { display:block; }
    h1 { font-size:30px; color:#76c9e8; margin-bottom:12px; }
    h2 { font-size:24px; margin:12px 0; }
    #nav { display:flex; flex-wrap:wrap; }
    button { display:block; flex-shrink:0; width:170px; max-width:90%; padding:8px 12px; margin:0 8px 8px 0; background:#173142; border:1px #76c9e8; }
    button:hover, button:focus { background:#315a70; }
    p { max-width:1100px; line-height:1.5; }
    #description { max-height:80px; overflow:auto; }
    #game { position:absolute; top:300px; left:20px; right:20px; bottom:20px; display:flex; flex-wrap:wrap; align-content:flex-start; }
    #game button { width:250px; }
    .widget { position:absolute; box-sizing:border-box; border:1px #315a70; padding:10px; overflow:auto; }
    .widget h3 { display:block; font-size:20px; margin-bottom:8px; }
    .widget button { width:90%; }
    </style></head><body><h1>PHOENIX / SETTLED DARK</h1>
    <div id="nav"><button id="home">Home</button><button id="host">New game</button><button id="join">Join console</button>
    <button id="view">Viewscreen</button><button id="edit">Workshop</button><button id="locale">Toggle pseudo-localisation</button></div>
    <h2 id="heading">Home</h2><p id="description"></p><div id="game"></div></body></rml>)");
    if (!document) throw std::runtime_error("Could not load home screen");
    document->AddEventListener("click", &navigation);
    document->Show();
}
bool close_requested() { return WindowShouldClose(); }
bool graphics_has_keyboard_focus() {
#ifdef __EMSCRIPTEN__
    return phoenix_canvas_has_focus();
#else
    return IsWindowFocused();
#endif
}
void draw_screen(Application& app) {
#ifdef __EMSCRIPTEN__
    const int width = phoenix_canvas_width(), height = phoenix_canvas_height();
    if (width != GetScreenWidth() || height != GetScreenHeight()) SetWindowSize(width,height);
#endif
    ClearBackground({8, 15, 25, 255});
    current_app = &app;
    drain_semantic_actions(app);
    update_document(app);
    prepare_game_screen(app,*document);
    publish_semantics(app);
    if (app.screen == Screen::workshop || app.screen == Screen::gm) return;
    draw_game_scene(app);
    context->SetDimensions({GetScreenWidth(), GetScreenHeight()});
    int modifiers = 0;
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) modifiers |= Rml::Input::KM_SHIFT;
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) modifiers |= Rml::Input::KM_CTRL;
    context->ProcessMouseMove(GetMouseX(), GetMouseY(), modifiers);
    for (int i = 0; i < 3; ++i) {
        if (IsMouseButtonPressed(i)) context->ProcessMouseButtonDown(i, modifiers);
        if (IsMouseButtonReleased(i)) context->ProcessMouseButtonUp(i, modifiers);
    }
    context->ProcessMouseWheel(-GetMouseWheelMove(), modifiers);
    const std::pair<int, Rml::Input::KeyIdentifier> keys[] = {
        {KEY_TAB,Rml::Input::KI_TAB}, {KEY_ENTER,Rml::Input::KI_RETURN}, {KEY_SPACE,Rml::Input::KI_SPACE},
        {KEY_BACKSPACE,Rml::Input::KI_BACK}, {KEY_ESCAPE,Rml::Input::KI_ESCAPE},
        {KEY_LEFT,Rml::Input::KI_LEFT}, {KEY_RIGHT,Rml::Input::KI_RIGHT}, {KEY_UP,Rml::Input::KI_UP}, {KEY_DOWN,Rml::Input::KI_DOWN}
    };
    const bool keyboard = graphics_has_keyboard_focus();
    if (!keyboard && context->GetFocusElement()) context->GetFocusElement()->Blur();
    for (auto [key, rml_key] : keys) {
        if (keyboard && IsKeyPressed(key)) context->ProcessKeyDown(rml_key, modifiers);
        if (!keyboard || IsKeyReleased(key)) context->ProcessKeyUp(rml_key, modifiers);
    }
    for (int character; (character = GetCharPressed()) != 0;) if (keyboard) context->ProcessTextInput(static_cast<Rml::Character>(character));
    context->Update();
    context->Render();
}
void close_window() { close_game_scene(); remove_native_accessibility(GetWindowHandle()); Rml::Shutdown(); current_app = nullptr; context = nullptr; document = nullptr; CloseWindow(); }
}
