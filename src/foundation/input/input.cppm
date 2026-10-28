module;
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

export module phoenix.input;

export namespace phoenix
{
using ConsoleId = std::uint64_t;
struct ConsoleRegion
{
	ConsoleId console;
	float x, y, width, height;
	bool operator==(const ConsoleRegion &) const = default;
};
struct ConsoleInput
{
	ConsoleId console;
	std::string control;
	float value;
	std::string device;
	bool cancelled = false;
};
struct PointerInput
{
	ConsoleId console;
	std::uint64_t contact;
	float x, y;
	bool released;
};
struct FlightAxes
{
	int yaw = 0, thrust = 0;
	bool operator==(const FlightAxes &) const = default;
};
// A held source cannot inherit control after interruption or another source's release.
class ManualFlightInput
{
public:
	void sample(const std::string &source, FlightAxes axes);
	void cancel();
	void cancel_source(const std::string &source);
	FlightAxes axes() const
	{
		return axes_;
	}

private:
	struct Source
	{
		FlightAxes axes;
		bool blocked = false;
	};
	std::map<std::string, Source> sources_;
	std::string owner_;
	FlightAxes axes_;
};
class InputRouter
{
public:
	// Regions are window coordinates, and can describe any monitor arrangement.
	// Invalid replacement leaves the current layout/captures unchanged.
	bool set_regions(std::vector<ConsoleRegion> regions);
	std::optional<PointerInput> pointer_down(std::uint64_t contact, float x, float y);
	std::optional<PointerInput> pointer_move(std::uint64_t contact, float x, float y);
	std::optional<PointerInput> pointer_up(std::uint64_t contact, float x, float y);
	void assign_gamepad(const std::string &device, std::optional<ConsoleId> console);
	void gamepad(const std::string &device, const std::string &control, float value);
	void disconnect_gamepad(const std::string &device);
	bool select_keyboard(ConsoleId console);
	std::optional<ConsoleId> keyboard_console() const
	{
		return keyboard_;
	}
	std::vector<ConsoleInput> drain_gamepad_inputs();

private:
	bool contains(ConsoleId console) const;
	std::optional<PointerInput> pointer(std::uint64_t contact, float x, float y, bool release);
	void neutralise(const std::string &device);
	std::vector<ConsoleRegion> regions_;
	std::map<std::uint64_t, ConsoleId> captures_;
	std::map<std::string, ConsoleId> gamepads_;
	std::map<std::string, std::map<std::string, float>> flight_start_;
	std::map<std::string, bool> flight_ready_;
	std::map<std::string, std::map<std::string, float>> held_;
	std::optional<ConsoleId> keyboard_;
	std::vector<ConsoleInput> pending_;
};
} // namespace phoenix
