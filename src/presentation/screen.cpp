#include <phoenix/presentation.hpp>
#include <raylib.h>
#include <RmlUi/Core.h>
#include "rml_backend.hpp"
#include <phoenix/semantics.hpp>
#include <stdexcept>
#include <filesystem>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_JS(bool, phoenix_canvas_has_focus, (), { return document.activeElement === Module.canvas; });
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
        for (const auto& control : navigation_controls(app)) document->GetElementById(control.id)->SetInnerRML(control.label);
        labels_ready = true;
        pseudo_shown = app.pseudo_locale;
    }
    if (shown == app.screen) return;
    shown = app.screen;
    document->GetElementById("heading")->SetInnerRML(screen_name(app.screen));
    const char* text = "Choose a mode to begin. This foundation build has no network session yet.";
    if (shown == Screen::server_lobby) text = "Ship server lobby. Content selection, peer readiness and QR joining arrive in the session increment.";
    if (shown == Screen::client_lobby) text = "Console lobby. Connection and authorised station claims arrive in the session increment.";
    if (shown == Screen::viewscreen) text = "Viewscreen. No mission is running.";
    document->GetElementById("description")->SetInnerRML(text);
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
    body { display:block; width:100%; box-sizing:border-box; font-family:LatoLatin; font-size:20px; color:#d9e9ef; padding:32px; }
    h1, h2, p { display:block; }
    h1 { font-size:30px; color:#76c9e8; margin-bottom:24px; }
    h2 { font-size:24px; margin:24px 0; }
    #nav { display:flex; flex-wrap:wrap; }
    button { display:block; flex-shrink:0; width:230px; max-width:90%; padding:12px 20px; margin:0 12px 12px 0; background:#173142; border:1px #76c9e8; }
    button:hover, button:focus { background:#315a70; }
    p { max-width:900px; line-height:1.5; }
    </style></head><body><h1>PHOENIX / SETTLED DARK</h1>
    <div id="nav"><button id="home">Home</button><button id="host">New game</button><button id="join">Join console</button>
    <button id="view">Viewscreen</button><button id="edit">Workshop</button><button id="locale">Toggle pseudo-localisation</button></div>
    <h2 id="heading">Home</h2><p id="description"></p></body></rml>)");
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
    ClearBackground({8, 15, 25, 255});
    current_app = &app;
    drain_semantic_actions(app);
    publish_semantics(app);
    if (app.screen == Screen::workshop || app.screen == Screen::gm) return;
    update_document(app);
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
void close_window() { remove_native_accessibility(GetWindowHandle()); Rml::Shutdown(); current_app = nullptr; context = nullptr; document = nullptr; CloseWindow(); }
}
