module;
#include <RmlUi/Core.h>
#include <RmlUi/Core/StringUtilities.h>
#include <raylib.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
export module phoenix.ui.navigation_panel;
export import phoenix.ui.surface_host;
export import phoenix.console;
export import phoenix.devices;
export import phoenix.graphics.geometry;
export import phoenix.graphics.assets;
export namespace phoenix
{
struct NavigationBrowsing
{
	NavigationCamera camera;
	double x = 0, y = 0;
};
struct NavigationPointerResult
{
	bool handled, changed;
};
struct NavigationFrame
{
	ConsoleDocument &dom;
	Rml::Context &context;
	ConsolePanelOutput output;
	ConsoleRegion region;
	std::string prefix;
	bool pregame;
	std::string &accessible_status;
	std::function<std::string(const std::string &)> asset, share_markup;
	std::string id(const std::string &name) const
	{
		return prefix + name;
	}
};
class NavigationPanel
{
public:
	void prepare(ConsoleModel &, NavigationFrame);
	bool action(ConsoleModel &, ConsoleDocument &, const std::string &prefix, const std::function<void()> &return_home,
	            const std::string &action);
	NavigationPointerResult pointer(ConsoleModel &, std::uint64_t, float x, float y, DeviceEventKind);
	bool key(int key, ConsoleDocument &, const std::string &prefix);
	void wheel(float x, float y, float delta);
	void draw(const ConsoleModel &, TextureCache &, const Catalog &, const Json &tokens, bool settings_open, float ox,
	          float oy);
	NavigationBrowsing browsing() const;
	void restore_browsing(const NavigationBrowsing &);
	std::vector<std::int64_t> display_camera() const;
	void layout_area(Rectangle area);
	void hide();
	void cancel();
	void coordinate(bool x, double value);
	bool visible() const;
	bool selected() const;
	double selected_x() const;
	Rectangle area() const;
	bool captures(std::uint64_t contact) const;
	const std::string &outcome() const;

private:
	NavigationCamera navigation_camera_;
	Rectangle navigation_area_{};
	bool navigation_visible_ = false;
	std::string navigation_markup_, mission_outcome_;
	std::map<std::uint64_t, Vector2> navigation_contacts_, navigation_starts_;
	bool navigation_dragged_ = false, navigation_selected_ = false;
	EntityId navigation_selected_contact_ = 0;
	std::vector<Rectangle> navigation_buttons_;
	double navigation_x_ = 0, navigation_y_ = 0;
};
} // namespace phoenix
