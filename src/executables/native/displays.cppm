module;
#include <string>
#include <vector>

export module phoenix.native.displays;
import phoenix.application;

export namespace phoenix
{
// Native executable composition: binds OS/window operations to application display
// assignments. Lower platform services remain independent of Application.
class NativeDisplayAdapter
{
public:
	NativeDisplayAdapter(Application &, unsigned frame_limit, bool vsync, bool uncapped);
	~NativeDisplayAdapter();
	NativeDisplayAdapter(const NativeDisplayAdapter &) = delete;
	NativeDisplayAdapter &operator=(const NativeDisplayAdapter &) = delete;
	void configure(bool span_displays, const std::string &display_layout);
	void poll(bool enabled);
	void pace();

private:
	Application &app;
	unsigned frame_limit;
	bool vsync, uncapped;
	bool lobby_span = false, device_console_mode = false;
	std::string layout_identity;
	void refresh_device_routes();
	double next_display_refresh = 0;
	double next_device_refresh = 0;
	std::vector<Application::NativeDisplay> device_displays;
	static std::vector<Application::NativeDisplay> enumerate_displays();
	void apply();
	void reset();
};
} // namespace phoenix
