module;
#include <cstdint>
#include <string>
#include <vector>

export module phoenix.devices;
export namespace phoenix
{
enum class DeviceEventKind
{
	Down,
	Move,
	Up,
	Cancel,
	Gamepad,
	Added,
	Removed
};
struct DeviceEvent
{
	DeviceEventKind kind;
	std::uint64_t contact = 0;
	float x = 0, y = 0;
	std::string device, control;
	float value = 0;
	std::string name, serial, path, model;
};
void open_devices(void *window);
void close_devices(void *window);
std::vector<DeviceEvent> poll_devices();
} // namespace phoenix
