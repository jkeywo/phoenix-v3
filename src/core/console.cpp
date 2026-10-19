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
	pending_requests_.clear();
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
		pending_requests_.clear();
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
			std::string binding;
			if (!layout_.is_null())
				for (const auto &widget : layout_.at("widgets"))
					if (widget.at("kind") == "throttle")
						binding = widget.value("system", std::string{});
			if (const auto selected = select_system(next->systems, OperatingSystem::Engine, binding))
				for (const auto &system : next->systems)
					if (system.id == *selected)
						maximum_ = system.maximum;
		}
	}
	observation_ = std::move(next);
	if (observation_ && (!pending_at_ || session->ticks() > *pending_at_))
	{
		pending_requests_.clear();
		speed_ = observation_->requested_speed;
		heading_ = observation_->requested_heading;
		if (!layout_.is_null())
			for (const auto &widget : layout_.at("widgets"))
			{
				const auto id = widget.at("id").get<std::string>();
				if (widget.at("kind") == "throttle")
					speed_ = system_request(widget_system(id));
				else if (widget.at("kind") == "heading")
					heading_ = system_request(widget_system(id));
			}
		pending_at_.reset();
		pending_requests_.clear();
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
	const auto separator = id.find('/');
	const auto binding = separator == std::string::npos ? std::string{} : widget_system(id.substr(separator + 1));
	if (control == "game.faster" || control == "game.slower")
	{
		command.action = "speed_mm_s";
		command.system = binding;
		const auto speed = binding.empty() ? speed_ : system_request(binding);
		const auto maximum = binding.empty() ? maximum_ : system_maximum(binding);
		command.value =
		    std::clamp(speed + (control == "game.faster" ? 1 : -1) * std::max<std::int64_t>(1, maximum / 20),
		               std::int64_t{0}, maximum);
	}
	else if (control == "game.left" || control == "game.right")
	{
		command.action = "heading_mdeg";
		command.system = binding;
		command.value =
		    ((binding.empty() ? heading_ : system_request(binding)) + (control == "game.left" ? 5000 : 355000)) %
		    360000;
	}
	else
		return false;
	return submit(std::move(command));
}
std::string ConsoleModel::widget_system(const std::string &id) const
{
	if (!observation_ || layout_.is_null())
		return {};
	for (const auto &widget : layout_.at("widgets"))
		if (widget.at("id") == id)
		{
			const auto kind = widget.at("kind");
			const auto type = kind == "throttle"  ? OperatingSystem::Engine
			                  : kind == "heading" ? OperatingSystem::Steering
			                                      : OperatingSystem::Radar;
			return select_system(observation_->systems, type, widget.value("system", std::string{}), kind != "radar")
			    .value_or("");
		}
	return {};
}
std::int64_t ConsoleModel::system_maximum(const std::string &id) const
{
	if (observation_)
		for (const auto &system : observation_->systems)
			if (system.id == id)
				return system.maximum;
	return 0;
}
std::int64_t ConsoleModel::system_request(const std::string &id) const
{
	if (auto found = pending_requests_.find(id); found != pending_requests_.end())
		return found->second;
	if (observation_)
		for (const auto &system : observation_->systems)
			if (system.id == id)
				return system.requested;
	return 0;
}
bool ConsoleModel::set_speed_fraction(double fraction, const std::string &system)
{
	if (!std::isfinite(fraction) || fraction < 0 || fraction > 1)
		return false;
	auto session = owner_.lock();
	if (!session)
		return false;
	refresh(session, route_);
	return observation_ && submit({observation_->own.id, "speed_mm_s",
	                               static_cast<std::int64_t>(
	                                   std::llround((system.empty() ? maximum_ : system_maximum(system)) * fraction)),
	                               system});
}
bool ConsoleModel::set_heading_degrees(double degrees, const std::string &system)
{
	if (!std::isfinite(degrees) || degrees < 0 || degrees >= 360)
		return false;
	auto session = owner_.lock();
	if (!session)
		return false;
	refresh(session, route_);
	return observation_ && submit({observation_->own.id, "heading_mdeg",
	                               static_cast<std::int64_t>(std::llround(degrees * 1000)) % 360000, system});
}
bool ConsoleModel::submit(OperatingCommand command)
{
	auto session = owner_.lock();
	if (!session || !observation_)
		return false;
	const auto system = operating_action(command.action)->system;
	std::string binding = command.system;
	const auto selected = select_system(observation_->systems, system, binding);
	if (!selected)
		return false;
	command.system = *selected;
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
	pending_requests_[command.system] = command.value;
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
