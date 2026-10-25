// Members of ConsoleSurface: local drag feedback never changes flight authority.
struct ThrottlePreview
{
	double fraction = 0, updated_at = 0;
	std::optional<std::int64_t> sent;
};
std::map<std::string, ThrottlePreview> throttle_previews_;
double throttle_zero(const std::string &key) const
{
	const auto binding = model_.widget_system(key);
	const auto minimum = model_.system_minimum(binding), maximum = model_.system_maximum(binding);
	return maximum > minimum ? std::clamp(-double(minimum) / double(maximum - minimum), 0.0, 1.0) : 0;
}
double throttle_detent(const std::string &key, double fraction) const
{
	const auto zero = throttle_zero(key);
	return std::abs(fraction - zero) <= .03 ? zero : std::clamp(fraction, 0.0, 1.0);
}
double throttle_fraction(const std::string &key) const
{
	if (const auto preview = throttle_previews_.find(key); preview != throttle_previews_.end())
		return preview->second.fraction;
	const auto binding = model_.widget_system(key);
	const auto minimum = model_.system_minimum(binding), maximum = model_.system_maximum(binding);
	return maximum > minimum
	           ? std::clamp(double(model_.system_request(binding) - minimum) / double(maximum - minimum), 0.0, 1.0)
	           : 0;
}
std::int64_t throttle_request(const std::string &key) const
{
	const auto binding = model_.widget_system(key);
	return std::llround(model_.system_minimum(binding) +
	                    (model_.system_maximum(binding) - model_.system_minimum(binding)) * throttle_fraction(key));
}
void reconcile_throttles()
{
	std::erase_if(throttle_previews_,
	              [&](const auto &entry)
	              {
		              const auto &[key, preview] = entry;
		              if (std::any_of(throttle_contacts_.begin(), throttle_contacts_.end(),
		                              [&](const auto &contact)
		                              {
			                              return contact.second == key;
		                              }))
			              return false;
		              const auto binding = model_.widget_system(key);
		              const auto expected = throttle_request(key);
		              if (const auto view = model_.observation())
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
		              return GetTime() - preview.updated_at > .75;
	              });
}
bool all_stop(const std::string &key)
{
	if (settings_open_ || systems_open_ || model_.manual_active() || !app_ || !game_active(*app_) || !model_.layout())
		return false;
	for (const auto &widget : model_.layout()->at("widgets"))
		if (widget.at("id") == key && widget.at("kind") == "throttle")
		{
			const auto binding = model_.widget_system(key);
			if (model_.system_minimum(binding) > 0 || model_.system_maximum(binding) < 0)
				return false;
			if (!model_.set_speed_fraction(throttle_zero(key), binding))
				return false;
			std::erase_if(throttle_contacts_,
			              [&](const auto &contact)
			              {
				              return contact.second == key;
			              });
			throttle_previews_[key] = {throttle_zero(key), GetTime(), 0};
			app_->accessible_status = localise("All stop");
			return true;
		}
	return false;
}
