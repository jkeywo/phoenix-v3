#include <phoenix/presentation.hpp>
#include <phoenix/world.hpp>
#include <phoenix/semantics.hpp>
#include <raylib.h>
#include <cstring>
#include <iostream>
#include <algorithm>
int main(int argc, char** argv) {
    phoenix::World world;
    if (argc > 1 && std::strcmp(argv[1], "--build-check") == 0) {
        world.tick();
        std::cout << "Phoenix native: Flecs tick " << world.ticks() << '\n';
        return world.ticks() == 1 ? 0 : 1;
    }
    phoenix::Application app;
    app.can_host = true;
    app.can_edit = true;
    int smoke_frames = 0;
    bool workshop_smoke = false;
    std::string workshop_pack;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke") == 0) smoke_frames = 6;
        if (std::strcmp(argv[i], "--workshop") == 0) { app.screen = phoenix::Screen::workshop; workshop_smoke = true; }
        if (std::strcmp(argv[i], "--workshop-pack") == 0 && i+1 < argc) { workshop_pack = argv[++i]; app.screen = phoenix::Screen::workshop; workshop_smoke = true; }
    }
    phoenix::open_window();
    if (argc > 1 && std::strcmp(argv[1], "--platform-check") == 0) {
        app.screen = phoenix::Screen::client_lobby;
        bool passed = phoenix::probe_native_accessibility(app);
        phoenix::close_window();
        std::cout << "Native UIA label/invoke queue: " << (passed ? "PASS" : "FAIL") << '\n';
        return passed ? 0 : 1;
    }
    phoenix::begin_tools();
    if (!workshop_pack.empty()) {
        try { phoenix::open_workshop_pack(workshop_pack); }
        catch (const std::exception& error) { std::cerr << error.what() << '\n'; phoenix::end_tools(); phoenix::close_window(); return 1; }
    }
    double accumulated = 0;
    while (!phoenix::close_requested()) {
        accumulated += std::min(static_cast<double>(GetFrameTime()),0.25);
        while (accumulated >= 1.0/phoenix::World::ticks_per_second) { world.tick(); accumulated -= 1.0/phoenix::World::ticks_per_second; }
        BeginDrawing();
        phoenix::draw_screen(app);
        if (app.screen == phoenix::Screen::workshop || app.screen == phoenix::Screen::gm) phoenix::draw_workshop(app);
        EndDrawing();
        if (smoke_frames && --smoke_frames == 0) {
            TakeScreenshot(workshop_smoke ? "workshop-smoke.png" : "home-smoke.png");
            break;
        }
    }
    phoenix::end_tools();
    phoenix::close_window();
}
