module;
#include <string>
#include <utility>
#include <vector>

export module phoenix.application.connections;
export import phoenix.application;
export import phoenix.application.preferences;

export namespace phoenix
{
bool ai_configuration_available(const Application &app);
std::vector<std::pair<std::string, std::string>> inference_controls(const Application &app);
std::vector<std::pair<std::string, std::string>> inference_readouts(const Application &app);
} // namespace phoenix
