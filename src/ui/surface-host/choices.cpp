module;
#include <array>
#include <span>
#include <string_view>
module phoenix.ui.choice_specs;
namespace phoenix
{
namespace
{
constexpr std::array modes{ConsoleChoiceOption{"First person", "1P", 16.f, 25.f, 32.f, 50.f},
                           ConsoleChoiceOption{"Third person", "3P", 51.f, 25.f, 32.f, 50.f}};
constexpr std::array<std::array<float, 2>, 8> centers{
    {{.5f, .19f}, {.73f, .27f}, {.81f, .5f}, {.73f, .73f}, {.5f, .81f}, {.27f, .73f}, {.19f, .5f}, {.27f, .27f}}};
constexpr auto directions = []
{
	std::array<ConsoleChoiceOption, 8> result{};
	constexpr std::array<std::string_view, 8> arrows{"↑", "↗", "→", "↘", "↓", "↙", "←", "↖"};
	for (std::size_t i = 0; i < result.size(); ++i)
		result[i] = {camera_directions[i],         arrows[i], (centers[i][0] - .11f) * 100,
		             (centers[i][1] - .11f) * 100, 22.f,      22.f};
	return result;
}();
const std::array specs{
    ConsoleChoiceSpec{"camera_mode", "camera-mode.png", "8%", "22%", 2.5f, ConsoleChoiceField::CameraMode, modes},
    ConsoleChoiceSpec{"camera_direction", "camera-direction.png", "30%", "45%", 1.f,
                      ConsoleChoiceField::CameraDirection, directions}};
} // namespace
int ConsoleChoiceSpec::selected(ShipCameraState camera) const
{
	return field == ConsoleChoiceField::CameraMode ? camera.mode : camera.direction;
}
const ConsoleChoiceSpec *console_choice_spec(std::string_view kind)
{
	for (const auto &spec : specs)
		if (spec.kind == kind)
			return &spec;
	return nullptr;
}
} // namespace phoenix
