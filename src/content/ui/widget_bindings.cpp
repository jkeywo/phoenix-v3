module;
#include <set>
#include <cstdint>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <utility>

module phoenix.content.ui;

namespace phoenix
{
std::span<const WidgetBinding> widget_bindings()
{
	static constexpr WidgetBinding bindings[] = {{"conversations", "ship.conversations"},
	                                             {"radar", "radar.contacts"},
	                                             {"throttle", "engine.desired_speed"},
	                                             {"heading", "steering.desired_heading"},
	                                             {"camera_mode", "ship.camera.mode"},
	                                             {"camera_direction", "ship.camera.direction"},
	                                             {"status", "ship.status"},
	                                             {"objectives", "ship.objectives"},
	                                             {"navigation", "ship.navigation"}};
	return bindings;
}
} // namespace phoenix
