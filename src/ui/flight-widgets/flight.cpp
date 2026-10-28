module;
#include <cstdint>
#include <map>
#include <string>
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
module phoenix.ui.flight_widgets;

namespace phoenix
{
double ThrottleFeedback::zero(const ConsoleModel &model, const std::string &key) const
{
	const auto binding = model.widget_system(key);
	const auto minimum = model.system_minimum(binding), maximum = model.system_maximum(binding);
	return maximum > minimum ? std::clamp(-double(minimum) / double(maximum - minimum), 0.0, 1.0) : 0;
}
double ThrottleFeedback::detent(const ConsoleModel &model, const std::string &key, double fraction) const
{
	const auto zero = this->zero(model, key);
	return std::abs(fraction - zero) <= .03 ? zero : std::clamp(fraction, 0.0, 1.0);
}
double ThrottleFeedback::fraction(const ConsoleModel &model, const std::string &key) const
{
	if (const auto preview = previews_.find(key); preview != previews_.end())
		return preview->second.fraction;
	const auto binding = model.widget_system(key);
	const auto minimum = model.system_minimum(binding), maximum = model.system_maximum(binding);
	return maximum > minimum
	           ? std::clamp(double(model.system_request(binding) - minimum) / double(maximum - minimum), 0.0, 1.0)
	           : 0;
}
std::int64_t ThrottleFeedback::request(const ConsoleModel &model, const std::string &key) const
{
	const auto binding = model.widget_system(key);
	return std::llround(model.system_minimum(binding) +
	                    (model.system_maximum(binding) - model.system_minimum(binding)) * fraction(model, key));
}
void ThrottleFeedback::reconcile(const ConsoleModel &model, const std::map<std::uint64_t, std::string> &contacts,
                                 double now)
{
	std::erase_if(previews_,
	              [&](const auto &entry)
	              {
		              const auto &[key, preview] = entry;
		              if (std::any_of(contacts.begin(), contacts.end(),
		                              [&](const auto &contact)
		                              {
			                              return contact.second == key;
		                              }))
			              return false;
		              const auto binding = model.widget_system(key);
		              const auto expected = request(model, key);
		              if (const auto view = model.observation())
		              {
			              for (const auto &controller : view->gameplay.value("controllers", Json::array()))
			              {
				              const auto commands = controller.value("commands", Json::object());
				              for (const auto &[channel, command] : commands.items())
					              if (binding ==
					                      "controller/" + controller.at("id").get<std::string>() + "/" + channel &&
					                  command.value("value", std::int64_t{}) == expected)
						              return true;
			              }
			              for (const auto &system : view->systems)
				              if (system.id == binding &&
				                  (system.telemetry.contains("desired_speed") ? system.telemetry.at("desired_speed")
				                                                              : system.requested) == expected)
					              return true;
		              }
		              return now - preview.updated_at > .75;
	              });
}
void ThrottleFeedback::track(const ConsoleModel &model, const std::string &key, double fraction, double now)
{
	auto [preview, inserted] = previews_.try_emplace(key);
	if (inserted)
		preview->second.sent = model.system_request(model.widget_system(key));
	preview->second.fraction = fraction;
	preview->second.updated_at = now;
}
bool ThrottleFeedback::differs(const std::string &key, std::int64_t requested) const
{
	return previews_.at(key).sent != requested;
}
void ThrottleFeedback::sent(const std::string &key, std::int64_t requested)
{
	previews_.at(key).sent = requested;
}
void ThrottleFeedback::settle(const std::string &key, double fraction, double now, std::int64_t requested)
{
	previews_[key] = {fraction, now, requested};
}
void ThrottleFeedback::erase(const std::string &key)
{
	previews_.erase(key);
}
void ThrottleFeedback::clear()
{
	previews_.clear();
}
} // namespace phoenix
