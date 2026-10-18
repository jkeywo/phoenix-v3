module;
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

module phoenix.console;
namespace phoenix
{
void ConsoleModel::reset()
{
	owner_.reset();
	packs_.clear();
	observation_.reset();
	pending_at_.reset();
	speed_ = heading_ = maximum_ = 0;
	layout_ = nullptr;
	announcement_.clear();
}
void ConsoleModel::refresh(const std::shared_ptr<SessionPort> &session, std::uint64_t route)
{
	if (!session)
	{
		reset();
		return;
	}
	const auto &catalog = session->catalog();
	if (owner_.lock() != session || generation_ != session->generation() || packs_ != catalog.ordered_packs ||
	    route_ != route)
	{
		reset();
		owner_ = session;
		generation_ = session->generation();
		route_ = route;
		packs_ = catalog.ordered_packs;
	}
	auto next = session->console_observation(route);
	const bool changed = observation_.has_value() != next.has_value() ||
	                     (next && observation_ &&
	                      (next->own.id != observation_->own.id || next->console != observation_->console ||
	                       next->controls != observation_->controls || next->feeds != observation_->feeds));
	if (changed)
	{
		pending_at_.reset();
		announcement_.clear();
		layout_ = nullptr;
		speed_ = heading_ = maximum_ = 0;
		if (next)
		{
			for (const auto &seat : resolve_consoles(catalog, next->own.definition))
				if (seat.id == next->console || next->console.empty())
				{
					if (catalog.consoles.contains(seat.definition))
						layout_ = catalog.consoles.at(seat.definition);
					break;
				}
			const auto &systems = catalog.entities.at(next->own.definition).at("systems");
			if (systems.contains("engine"))
				maximum_ = systems.at("engine").at("max_speed_mm_s");
		}
	}
	observation_ = std::move(next);
	if (observation_ && (!pending_at_ || session->ticks() > *pending_at_))
	{
		speed_ = observation_->requested_speed;
		heading_ = observation_->requested_heading;
		pending_at_.reset();
	}
}
bool ConsoleModel::act(const std::string &id)
{
	auto session = owner_.lock();
	if (!session)
		return false;
	refresh(session, route_);
	if (!observation_)
		return false;
	const auto control = id.substr(0, id.find('/'));
	if (control == "game.contacts")
	{
		if (!(observation_->feeds & system_bit(OperatingSystem::Radar)))
			return false;
		announcement_ = contacts_text();
		return true;
	}
	OperatingCommand command{observation_->own.id, {}, 0, {}};
	if (control == "game.faster" || control == "game.slower")
	{
		command.action = "speed_mm_s";
		command.value =
		    std::clamp(speed_ + (control == "game.faster" ? 1 : -1) * std::max<std::int64_t>(1, maximum_ / 20),
		               std::int64_t{0}, maximum_);
	}
	else if (control == "game.left" || control == "game.right")
	{
		command.action = "heading_mdeg";
		command.value = (heading_ + (control == "game.left" ? 5000 : 355000)) % 360000;
	}
	else
		return false;
	return submit(std::move(command));
}
bool ConsoleModel::set_speed_fraction(double fraction)
{
	if (!std::isfinite(fraction) || fraction < 0 || fraction > 1)
		return false;
	auto session = owner_.lock();
	if (!session)
		return false;
	refresh(session, route_);
	return observation_ &&
	       submit(
	           {observation_->own.id, "speed_mm_s", static_cast<std::int64_t>(std::llround(maximum_ * fraction)), {}});
}
bool ConsoleModel::set_heading_degrees(double degrees)
{
	if (!std::isfinite(degrees) || degrees < 0 || degrees >= 360)
		return false;
	auto session = owner_.lock();
	if (!session)
		return false;
	refresh(session, route_);
	return observation_ && submit({observation_->own.id,
	                               "heading_mdeg",
	                               static_cast<std::int64_t>(std::llround(degrees * 1000)) % 360000,
	                               {}});
}
bool ConsoleModel::submit(OperatingCommand command)
{
	auto session = owner_.lock();
	if (!session || !observation_)
		return false;
	const auto system = operating_action(command.action)->system;
	if (!(observation_->controls & system_bit(system)))
		return false;
	command.system = operating_system_name(system);
	if (!session->console_command(route_, command))
	{
		announcement_ = "The server refused that control request";
		return false;
	}
	if (system == OperatingSystem::Engine)
		speed_ = command.value;
	else
		heading_ = command.value;
	pending_at_ = session->ticks();
	announcement_ = "Requested speed " + std::to_string(speed_ / 1000.0) + " m/s; heading " +
	                std::to_string(heading_ / 1000) + " degrees.";
	return true;
}
std::string ConsoleModel::contacts_text() const
{
	if (!observation_ || !(observation_->feeds & system_bit(OperatingSystem::Radar)))
		return {};
	std::ostringstream text;
	text << "Radar: " << observation_->contacts.size() << " contacts.";
	for (const auto &contact : observation_->contacts)
	{
		const double x = static_cast<double>(contact.x - observation_->own.x),
		             y = static_cast<double>(contact.y - observation_->own.y);
		const auto bearing = std::fmod(std::atan2(y, x) * 180 / 3.14159265358979323846 + 360, 360.0);
		text << " " << contact.definition << " #" << contact.id << ", " << static_cast<int>(std::hypot(x, y) / 1000)
		     << " m, bearing " << static_cast<int>(bearing) << " degrees.";
	}
	return text.str();
}
} // namespace phoenix
