module;
#include <span>
#include <string_view>
export module phoenix.ui.choice_specs;
export import phoenix.contracts;
export namespace phoenix
{
struct ConsoleChoiceOption
{
	std::string_view label, visual;
	float left, top, width, height;
};
enum class ConsoleChoiceField
{
	CameraMode,
	CameraDirection
};
struct ConsoleChoiceSpec
{
	std::string_view kind, artwork, housing_top, housing_height;
	float aspect;
	ConsoleChoiceField field;
	std::span<const ConsoleChoiceOption> options;
	int selected(ShipCameraState camera) const;
};
// Presentation descriptions only. ConsoleModel and host admission own commands.
const ConsoleChoiceSpec *console_choice_spec(std::string_view kind);
} // namespace phoenix
