module;
#include <RmlUi/Core.h>
#include <raylib.h>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
export module phoenix.ui.console_surface;
export import phoenix.content.ui;

export import phoenix.application;
export import phoenix.console;
export import phoenix.ui.rml;
export import phoenix.ui.interaction;
export import phoenix.devices;
export namespace phoenix
{
class ConsoleSurface
{
public:
	explicit ConsoleSurface(ConsoleId route, const std::string &prefix = "console-", bool toolbar = true,
	                        Rml::RenderInterface *renderer = nullptr);
	~ConsoleSurface();
	void preview_text_scale(int scale);
	const UiResolved &authored_geometry() const;
	void preview_pages(const Json &layout);
	Rml::Context &context();
	bool action(const std::string &name);
	bool controller_hold(const std::string &name, bool pressed);
	std::string controller_identity() const;
	bool controller_flight() const;
	Rml::Element *controller_element(const std::string &name);
	void controller_cancel();
	bool controller_back();
	void focus(const std::string &name);
	bool set_value(const std::string &name, const std::string &text);
	void flush_manual();
	void cancel_gamepad(const std::string &device);
	void gamepad(const std::string &device, const std::string &control, float value);
	void manual_frame(bool keyboard_selected, FlightAxes keyboard_axes, bool focused);
	void prepare_manual(Application &app, const ConsoleRegion &region, bool pregame);
	bool pointer(std::uint64_t contact, float x, float y, DeviceEventKind kind);
	void cancel_pointer();
	void update_view();
	bool probe_throttle(unsigned stage);
	bool probe_conversation() const;
	bool probe_tab(const std::string &action);
	bool probe_navigation(unsigned stage);
	bool probe_fit() const;
	bool probe_support_visuals();
	bool probe_readout_scroll();
	bool probe_report();
	bool probe_ready();
	bool probe_on_screen(const std::string &widget);
	bool probe_helm(bool verify = false);
	bool probe_manual(unsigned stage);
	bool probe_engineering(unsigned stage);
	bool preparation_urgent() const;
	void prepare(Application &app, const ConsoleRegion &physical_region, const std::set<std::string> &pads,
	             bool allow_fresh = true);
	bool paint_pending();
	double paint_wait() const;
	double paint_cost() const;
	void render_ui(UiRenderer &renderer, const ConsoleRegion &region, bool allow_paint = true);
	void prepare_loading(const LoadingProgress *loading, const ConsoleRegion &region);
	void prepare_current(Application &app, const ConsoleRegion &physical_region, const std::set<std::string> &pads);
	void draw_profile(const ConsoleRegion &region);
	void draw(const ConsoleRegion &region);
	ConsoleSurface(const ConsoleSurface &) = delete;
	ConsoleSurface &operator=(const ConsoleSurface &) = delete;

private:
	struct State;
	std::unique_ptr<State> state_;
};
} // namespace phoenix
