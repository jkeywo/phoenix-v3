module;
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

export module phoenix.console;
export import phoenix.session.api;

export namespace phoenix
{
std::vector<std::pair<std::string, std::string>> public_board_readouts(const Json &, std::uint64_t now);
// One instance per routed Console. No renderer, device or authoritative world.
class ConsoleModel
{
public:
	void refresh(const std::shared_ptr<SessionPort> &session, std::uint64_t route = 1);
	void reset();
	bool act(const std::string &action);
	bool command(const std::string &action, std::int64_t value, const std::string &system = {});
	bool hold_control(const std::string &action);
	bool release_control(const std::string &action);
	void release_controls();
	bool set_speed_fraction(double fraction, const std::string &system = {});
	bool set_heading_degrees(double degrees, const std::string &system = {});
	bool on_screen(const std::string &widget, const std::vector<std::int64_t> &camera, bool update = false);
	bool is_on_screen(const std::string &widget) const;
	bool set_waypoint(std::int64_t x, std::int64_t y, bool active = true);
	ShipCameraState camera() const;
	Json objectives() const;
	Json waypoint() const;
	std::string widget_system(const std::string &widget) const;
	std::int64_t system_maximum(const std::string &system) const;
	std::int64_t system_minimum(const std::string &system) const;
	std::int64_t system_request(const std::string &system, const std::string &action = {}) const;
	std::int64_t maximum_speed() const
	{
		return maximum_;
	}
	const std::optional<Observation> &observation() const
	{
		return observation_;
	}
	const Json *layout() const
	{
		return layout_.is_null() ? nullptr : &layout_;
	}
	std::int64_t requested_speed() const
	{
		return speed_;
	}
	std::int64_t requested_heading() const
	{
		return heading_;
	}
	const std::string &announcement() const
	{
		return announcement_;
	}
	std::string contacts_text() const;
	std::vector<std::pair<std::string, std::string>> gameplay_controls() const;
	std::vector<std::pair<std::string, std::string>> gameplay_readouts() const;

private:
	std::string selected_console_;
	std::vector<std::pair<std::string, std::string>> available_consoles_;
	const Json *controller_section(const std::string &binding) const;
	const Json *controller_channel(const std::string &binding) const;
	std::string default_binding(const std::string &action) const;
	bool submit(OperatingCommand command);
	std::weak_ptr<SessionPort> owner_;
	std::uint64_t generation_ = 0, route_ = 1;
	std::vector<std::pair<std::string, std::string>> packs_;
	std::optional<Observation> observation_;
	std::optional<std::uint64_t> pending_at_;
	std::int64_t speed_ = 0, heading_ = 0, maximum_ = 0;
	Json layout_;
	std::string announcement_;
	std::map<std::pair<std::string, std::string>, std::int64_t> pending_requests_;
	std::map<std::string, OperatingCommand> held_controls_;
	std::uint64_t notification_sequence_ = 0;
	bool notifications_initialised_ = false;
};
} // namespace phoenix
