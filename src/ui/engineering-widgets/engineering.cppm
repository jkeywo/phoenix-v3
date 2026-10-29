module;
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
export module phoenix.ui.engineering_widgets;

export import phoenix.contracts;
export namespace phoenix
{
struct EngineeringCard
{
	std::string id, title, summary;
	std::optional<int> heat, battery, condition;
	std::vector<std::pair<std::string, std::string>> controls;
};
std::string engineering_name(std::string name);
std::string descriptor_action(const std::string &id);
std::vector<std::string> command_consumers(const Observation &view, const Json &descriptor);
std::set<std::string> station_owned_systems(const Observation &view);
struct PowerSetting
{
	std::string id, controller, channel, action, subject;
	std::int64_t minimum = 0, maximum = 0, value = 0;
	bool coolant = false;
	std::string console, component;
};
std::vector<PowerSetting> power_settings(const Observation &view);
const PowerSetting *power_setting_for(const std::vector<PowerSetting> &settings, const Json &row, bool coolant);
std::vector<EngineeringCard> engineering_cards(const Observation &view);
} // namespace phoenix
