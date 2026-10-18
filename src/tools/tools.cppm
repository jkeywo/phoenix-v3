module;
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

export module phoenix.tools;
export import phoenix.presentation;

export namespace phoenix
{
void begin_tools();
void draw_workshop(Application &app);
void draw_gm_region(Application &app);
void end_tools();
void open_workshop_pack(const std::string &path);
} // namespace phoenix
