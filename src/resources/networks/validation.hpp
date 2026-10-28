#pragma once
inline ResourceAmount amount(const Json &object, const char *key, ResourceAmount fallback = 0,
                             ResourceAmount maximum = resource_limit)
{
	return json_integer(object, key, fallback, 0, maximum, "resources");
}
inline bool flag(const Json &object, const char *key, bool fallback = false)
{
	if (!object.contains(key))
		return fallback;
	if (!object.at(key).is_boolean())
		throw std::runtime_error(std::string("resources.") + key + ": expected boolean");
	return object.at(key).get<bool>();
}
