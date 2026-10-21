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
struct ConsoleMessageView
{
	std::string id, sender, text, portrait;
	Bytes avatar;
	std::string accessible_text() const
	{
		return sender + ": " + text;
	}
};
std::vector<ConsoleMessageView> console_messages(const Json &board, bool public_only = false);
std::vector<std::pair<std::string, std::string>> public_board_readouts(const Json &, std::uint64_t now);
enum class SystemCondition
{
	Operational,
	Off,
	Damaged,
	CompartmentDisabled,
	Overheated,
	Destroyed
};
struct ConsoleSystemStatus
{
	std::string system, semantic_label, compact_label;
	SystemCondition condition = SystemCondition::Operational;
	bool attention() const
	{
		return condition != SystemCondition::Operational;
	}
};
// One instance per routed Console. No renderer, device or authoritative world.
class ConsoleModel
{
public:
	void refresh(const std::shared_ptr<SessionPort> &session, std::uint64_t route = 1);
	void reset();
	bool act(const std::string &action);
	bool fire_arcs_visible() const
	{
		return fire_arcs_;
	}
	std::int64_t selected_compartment() const
	{
		return selected_compartment_;
	}
	bool command(const std::string &action, std::int64_t value, const std::string &system = {});
	bool manual_available() const;
	bool manual_active() const;
	bool set_manual(bool enabled);
	bool set_manual_axes(int yaw, int thrust);
	bool hold_control(const std::string &action);
	bool flight_button(const std::string &action, std::int64_t value, bool pressed);
	bool release_control(const std::string &action);
	void release_controls();
	bool set_speed_fraction(double fraction, const std::string &system = {});
	bool set_heading_degrees(double degrees, const std::string &system = {});
	bool on_screen(const std::string &widget, const std::vector<std::int64_t> &camera, bool update = false);
	bool is_on_screen(const std::string &widget) const;
	bool set_waypoint(std::int64_t x, std::int64_t y, bool active = true);
	ShipCameraState camera() const;
	Json objectives() const;
	Json personnel() const;
	bool personnel_action(const std::string &action, const std::string &officer, const std::string &duty = {});
	Json conversations() const;
	bool conversation_action(const std::string &action, std::uint64_t conversation = 0, const std::string &text = {},
	                         std::uint64_t prompt = 0);
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
	std::uint64_t layout_revision() const
	{
		return layout_revision_;
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
	std::vector<ConsoleMessageView> messages() const;
	std::string system_details() const;
	std::vector<ConsoleSystemStatus> system_statuses() const;
	const std::string &status_system() const
	{
		return status_system_;
	}

private:
	std::string selected_console_, status_system_;
	std::int64_t combat_target_ = 0, selected_compartment_ = 0;
	bool fire_arcs_ = true;
	std::vector<std::pair<std::string, std::string>> available_consoles_;
	const Json *controller_section(const std::string &binding) const;
	const Json *controller_channel(const std::string &binding) const;
	std::string manual_binding(const std::string &action, const std::string &widget_action) const;
	std::string default_binding(const std::string &action) const;
	bool submit(OperatingCommand command);
	std::weak_ptr<SessionPort> owner_;
	std::uint64_t generation_ = 0, route_ = 1;
	std::vector<std::pair<std::string, std::string>> packs_;
	std::optional<Observation> observation_;
	std::optional<std::uint64_t> pending_at_;
	std::int64_t speed_ = 0, heading_ = 0, maximum_ = 0;
	Json layout_;
	std::uint64_t layout_revision_ = 0;
	std::string announcement_;
	std::map<std::pair<std::string, std::string>, std::int64_t> pending_requests_;
	std::map<std::string, OperatingCommand> held_controls_;
	std::uint64_t notification_sequence_ = 0;
	bool notifications_initialised_ = false;
};
} // namespace phoenix
