#pragma once
#include <phoenix/application.hpp>
namespace phoenix {
void open_window();
bool close_requested();
void draw_screen(Application& app);
void close_window();
void begin_tools();
void draw_workshop(Application& app);
void end_tools();
void open_workshop_pack(const std::string& path);
}
