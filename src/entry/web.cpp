#include <phoenix/presentation.hpp>
#include <raylib.h>
#include <emscripten.h>
#ifdef PHOENIX_SERVER
#include <phoenix/world.hpp>
std::unique_ptr<phoenix::World> world;
#endif
phoenix::Application app;
void frame() {
#ifdef PHOENIX_SERVER
    world->tick();
#endif
    BeginDrawing();
    phoenix::draw_screen(app);
#ifdef PHOENIX_WORKSHOP
    if (app.screen == phoenix::Screen::workshop || app.screen == phoenix::Screen::gm) phoenix::draw_workshop(app);
#endif
    EndDrawing();
}
int main(int argc, char** argv) {
#ifdef PHOENIX_SERVER
    world = std::make_unique<phoenix::World>();
#endif
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
#ifdef PHOENIX_WORKSHOP
        if (std::string(argv[i]) == "--mode" && std::string(argv[i+1]) == "gm") app.screen = phoenix::Screen::gm;
#endif
    }
    phoenix::open_window();
#ifdef PHOENIX_WORKSHOP
    phoenix::begin_tools();
#endif
    emscripten_set_main_loop(frame, 0, true);
}
