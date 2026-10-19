#pragma once
inline ResourceAmount amount(const Json &object, const char *key, ResourceAmount fallback = 0,
                             ResourceAmount maximum = resource_limit)
{
	if (!object.contains(key))
		return fallback;
	const auto &value = object.at(key);
	if (!value.is_number_integer() || value.is_boolean() || value < 0 || value > maximum)
		throw std::runtime_error(std::string("resources.") + key +
		                         ": expected finite nonnegative integer <= " + std::to_string(maximum));
	return value.get<ResourceAmount>();
}
inline bool flag(const Json &object, const char *key, bool fallback = false)
{
	if (!object.contains(key))
		return fallback;
	if (!object.at(key).is_boolean())
		throw std::runtime_error(std::string("resources.") + key + ": expected boolean");
	return object.at(key).get<bool>();
}
