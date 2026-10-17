#include <phoenix/presentation.hpp>
#include <raylib.h>
#include <emscripten.h>
#include <algorithm>
#include <phoenix/session.hpp>
phoenix::Application app;
void frame() {
    if (app.session) app.session->poll();
#ifdef PHOENIX_SERVER
    static double accumulated = 0;
    accumulated += std::min(static_cast<double>(GetFrameTime()),0.25);
    while (accumulated >= 1.0/20) { app.session->tick(); accumulated -= 1.0/20; }
#endif
    BeginDrawing();
    phoenix::draw_screen(app);
#ifdef PHOENIX_WORKSHOP
    if (app.screen == phoenix::Screen::workshop || app.screen == phoenix::Screen::gm) phoenix::draw_workshop(app);
#endif
    EndDrawing();
}
int main(int argc, char** argv) {
    phoenix::NetworkOptions network;
    app.status = PHOENIX_ROLE;
#ifdef PHOENIX_SERVER
    app.can_host = true;
#endif
#ifdef PHOENIX_WORKSHOP
    app.can_edit = true;
    app.screen = phoenix::Screen::workshop;
#endif
    for (int i = 1; i + 1 < argc; i += 2) {
        if (std::string(argv[i]) == "--code") app.network_code = argv[i+1];
        if (std::string(argv[i]) == "--service") network.link.url = argv[i+1];
        if (std::string(argv[i]) == "--profile") network.profile = argv[i+1];
#ifdef PHOENIX_WORKSHOP
        if (std::string(argv[i]) == "--mode" && std::string(argv[i+1]) == "gm") app.screen = phoenix::Screen::gm;
#endif
    }
    if (!app.network_code.empty()) {
        auto separator = app.network_code.find('/');
        network.link.room = app.network_code.substr(0,separator);
        if (separator != std::string::npos) network.server = app.network_code.substr(separator+1);
    }
    try {
#ifdef PHOENIX_SERVER
        network.link.self = network.server;
        app.session = phoenix::make_network_host(network);
#elif defined(PHOENIX_WORKSHOP)
        if (app.screen == phoenix::Screen::gm) { network.inspector = true; app.session = phoenix::make_network_client(network); }
#else
        app.screen = phoenix::Screen::client_lobby;
        app.session = phoenix::make_network_client(network);
#endif
    } catch (const std::exception& error) { app.status = error.what(); }
    phoenix::open_window();
#ifdef PHOENIX_WORKSHOP
    phoenix::begin_tools();
#endif
    emscripten_set_main_loop(frame, 0, true);
}
