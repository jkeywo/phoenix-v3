module;
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <phoenix_workshop_fields.hpp>
module phoenix.workshop.authoring_core;

namespace phoenix
{
const Json &workshop_fields()
{
	static const Json fields =
	    Json::parse(phoenix_workshop_field_bytes, phoenix_workshop_field_bytes + sizeof(phoenix_workshop_field_bytes));
	return fields;
}
const Json &workshop_field(const std::string &id)
{
	return workshop_fields().at(id);
}
void workshop_set_field(Json &object, const Json &field, const Json &value)
{
	const auto type = field.at("type").get<std::string>();
	if ((type == "integer" && !value.is_number_integer()) || (type == "number" && !value.is_number()) ||
	    (type == "boolean" && !value.is_boolean()) || (type == "string" && !value.is_string()))
		throw std::runtime_error(field.at("key").get<std::string>() + ": expected " + type);
	if (value.is_number_float() && !std::isfinite(value.get<double>()))
		throw std::runtime_error("Expected a finite number");
	if (type == "integer")
		(void)checked_integer(value, std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max(),
		                      field.at("key"));
	if (field.contains("choices") &&
	    std::find(field.at("choices").begin(), field.at("choices").end(), value) == field.at("choices").end())
	{
		if (field.value("optional", false) && value == "")
		{
			object.erase(field.at("key").get<std::string>());
			return;
		}
		throw std::runtime_error("Value is not a supported choice for " + field.at("key").get<std::string>());
	}
	object[field.at("key").get<std::string>()] = value;
}
} // namespace phoenix
