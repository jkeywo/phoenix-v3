module;
#include <set>
#include <cstdint>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <stdexcept>
module phoenix.content.effects.types;
namespace phoenix
{
namespace
{
[[noreturn]] void property_error(const std::string &path, const std::string &why)
{
	throw std::runtime_error(path + ": " + why);
}
std::int64_t property_integer(const Json &value, const std::string &path, std::int64_t low, std::int64_t high)
{
	if (!value.is_number_integer() || value < low || value > high)
		property_error(path, "integer outside supported bounds");
	return value.get<std::int64_t>();
}
[[noreturn]] void process_fail(const std::string &path, const std::string &why)
{
	throw std::runtime_error(path + ": " + why);
}
} // namespace
CustomPropertyValue read_custom_property_value(const CustomPropertyDefinition &d, const Json &value,
                                               const std::string &path)
{
	if (d.kind == CustomPropertyKind::Quantity || d.kind == CustomPropertyKind::Attribute)
		return property_integer(value, path, d.minimum, d.maximum);
	if (d.kind == CustomPropertyKind::Flag)
	{
		if (!value.is_boolean())
			property_error(path, "expected Boolean");
		return value.get<bool>();
	}
	if (!value.is_string() || !d.choices.contains(value.get<std::string>()))
		property_error(path, "expected enum identifier in declared domain");
	return value.get<std::string>();
}
Json custom_property_json(const CustomPropertyValue &v)
{
	return std::visit(
	    [](const auto &item)
	    {
		    return Json(item);
	    },
	    v);
}
std::string resolve_process_native_binding(const ProcessDefinition &definition, const ProcessNativeBindings &bindings,
                                           const std::string &requested_id)
{
	if (definition.native == NativeProcessKind::Effects)
	{
		if (!requested_id.empty())
			process_fail(definition.id, "effect-only process omits native_id");
		return {};
	}
	const auto found = bindings.find(definition.native);
	if (found == bindings.end() || found->second.empty())
		process_fail(definition.id, "native process requires matching fitted machinery");
	if (!requested_id.empty())
	{
		if (found->second.contains(requested_id))
			return requested_id;
		if (definition.native == NativeProcessKind::ServicingAmmunition && requested_id.starts_with("ammo-"))
		{
			std::string match;
			for (const auto &candidate : found->second)
				if (candidate.ends_with("." + requested_id))
				{
					if (!match.empty())
						process_fail(definition.id, "ambiguous ammunition connection");
					match = candidate;
				}
			if (!match.empty())
				return match;
		}
		process_fail(definition.id, "native_id does not belong to the owning equipment/mechanism");
	}
	if (found->second.size() != 1)
		process_fail(definition.id, "ambiguous native machinery binding; set native_id");
	return *found->second.begin();
}
} // namespace phoenix
