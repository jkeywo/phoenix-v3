#pragma once
#include <phoenix/application.hpp>
#include <RmlUi/Core.h>
namespace phoenix
{
void prepare_game_screen(Application &app, Rml::ElementDocument &document);
void draw_game_scene(const Application &app);
void close_game_scene();
} // namespace phoenix
