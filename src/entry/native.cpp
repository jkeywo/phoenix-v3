#include <phoenix/presentation.hpp>
#include <phoenix/world.hpp>
#include <raylib.h>
#include <cstring>
#include <iostream>
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
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke") == 0) smoke_frames = 6;
        if (std::strcmp(argv[i], "--workshop") == 0) { app.screen = phoenix::Screen::workshop; workshop_smoke = true; }
    }
    phoenix::open_window();
    phoenix::begin_tools();
    while (!phoenix::close_requested()) {
        world.tick();
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
