module;
#include <string>
#include <utility>
#include <vector>

export module phoenix.application.displays;
export import phoenix.application;
export import phoenix.application.preferences;

export namespace phoenix
{
void remember_station_display(Application &app, const std::string &console, int monitor);
bool update_native_displays(Application &app, std::vector<Application::NativeDisplay> displays);
void restore_station_displays(Application &app);
} // namespace phoenix
