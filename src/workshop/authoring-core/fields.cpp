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
	const auto path = Json::json_pointer(
	    field.value("path", (Json::json_pointer("") / field.at("key").get<std::string>()).to_string()));
	auto erase = [&]
	{
		auto parent = path;
		const auto key = parent.back();
		parent.pop_back();
		if (object.contains(parent))
			object.at(parent).erase(key);
	};
	const auto type = field.at("type").get<std::string>();
	if ((type == "integer" && !value.is_number_integer()) || (type == "number" && !value.is_number()) ||
	    (type == "array" && !value.is_array()) || (type == "boolean" && !value.is_boolean()) ||
	    (type == "string" && !value.is_string()))
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
			erase();
			return;
		}
		throw std::runtime_error("Value is not a supported choice for " + field.at("key").get<std::string>());
	}
	auto clamp = [&](const Json &input, const std::string &item_type) -> Json
	{
		if (!input.is_number() || (item_type == "integer" && !input.is_number_integer()))
			throw std::runtime_error("Expected numeric control value");
		if (!std::isfinite(input.get<double>()))
			throw std::runtime_error("Expected finite control value");
		if (item_type == "integer")
			(void)checked_integer(input, std::numeric_limits<std::int64_t>::min(),
			                      std::numeric_limits<std::int64_t>::max(), field.at("key"));
		if (!field.contains("minimum"))
			return input;
		return item_type == "integer"
		           ? Json(std::clamp(input.get<std::int64_t>(), field.at("minimum").get<std::int64_t>(),
		                             field.at("maximum").get<std::int64_t>()))
		           : Json(std::clamp(input.get<double>(), field.at("minimum").get<double>(),
		                             field.at("maximum").get<double>()));
	};
	auto result = value;
	if (type == "array")
	{
		if (value.size() != field.at("length").get<std::size_t>())
			throw std::runtime_error("Wrong control vector length");
		for (auto &item : result)
			item = clamp(item, field.value("items", std::string("number")));
	}
	else if (type == "integer" || type == "number")
		result = clamp(value, type);
	if (field.contains("erase_value") && result == field.at("erase_value"))
	{
		erase();
		return;
	}
	object[path] = result;
}
} // namespace phoenix
