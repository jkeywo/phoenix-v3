module;
#include <utility>
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <nlohmann/json.hpp>
module phoenix.content.effects;
import phoenix.content.catalog;
import phoenix.content.effects.types;

namespace phoenix
{
namespace
{
[[noreturn]] void property_error(const std::string &path, const std::string &why)
{
	throw std::runtime_error(path + ": " + why);
}
std::int64_t property_integer(const Json &v, const std::string &path, std::int64_t low, std::int64_t high)
{
	if (!v.is_number_integer() || v < low || v > high)
		property_error(path, "integer outside supported bounds");
	return v.get<std::int64_t>();
}
} // namespace

SharedPropertyCatalog compile_property_catalog(const Catalog &catalog)
{
	PropertyCatalog result;
	if (catalog.properties.size() > 4096)
		property_error("properties", "too many definitions");
	for (const auto &[id, v] : catalog.properties)
	{
		const auto path = "properties." + id;
		if (!valid_identifier(id) || known_modifier_property(id) || !v.is_object())
			property_error(path, "invalid or reserved native property identity");
		for (const auto &[field, unused] : v.items())
			if (field != "id" && field != "extends" && field != "kind" && field != "label" && field != "unit" &&
			    field != "minimum" && field != "maximum" && field != "default" && field != "display_scale" &&
			    field != "choices")
				property_error(path + "." + field, "unsupported field");
		CustomPropertyDefinition d;
		d.id = id;
		d.label = v.value("label", id);
		d.unit = v.value("unit", std::string{});
		if (d.label.empty() || d.label.size() > 128 || d.unit.size() > 128)
			property_error(path, "invalid label/unit");
		const auto kind = v.value("kind", std::string{});
		if (kind == "quantity")
			d.kind = CustomPropertyKind::Quantity;
		else if (kind == "attribute")
			d.kind = CustomPropertyKind::Attribute;
		else if (kind == "flag")
			d.kind = CustomPropertyKind::Flag;
		else if (kind == "enum")
			d.kind = CustomPropertyKind::Enum;
		else
			property_error(path, "unknown property kind");
		const bool numeric = d.kind == CustomPropertyKind::Quantity || d.kind == CustomPropertyKind::Attribute;
		if (numeric)
		{
			d.minimum = property_integer(v.value("minimum", Json(0)), path + ".minimum",
			                             d.kind == CustomPropertyKind::Quantity ? 0 : -resource_limit, resource_limit);
			d.maximum = property_integer(v.value("maximum", Json(resource_limit)), path + ".maximum", d.minimum,
			                             resource_limit);
			d.display_scale =
			    property_integer(v.value("display_scale", Json(1)), path + ".display_scale", 1, 1000000000);
			if (v.contains("choices"))
				property_error(path, "numeric property cannot declare enum choices");
		}
		else if (!d.unit.empty() || v.contains("minimum") || v.contains("maximum") || v.contains("display_scale"))
			property_error(path, "only numeric properties declare units/bounds/display scale");
		if (d.kind == CustomPropertyKind::Enum)
		{
			const auto choices = v.value("choices", Json::object());
			if (!choices.is_object() || choices.empty() || choices.size() > 128)
				property_error(path, "enum requires 1..128 named choices");
			for (const auto &[key, label] : choices.items())
			{
				if (!valid_identifier(key) || !label.is_string() || label.get<std::string>().empty() ||
				    label.get<std::string>().size() > 128)
					property_error(path, "invalid enum choice");
				d.choices.emplace(key, label.get<std::string>());
			}
		}
		else if (v.contains("choices"))
			property_error(path, "choices require enum property");
		if (!v.contains("default"))
			property_error(path, "property requires an explicit default");
		d.initial = read_custom_property_value(d, v.at("default"), path + ".default");
		result.emplace(id, std::move(d));
	}
	return std::make_shared<const PropertyCatalog>(std::move(result));
}
std::map<std::string, CustomPropertyValue> compile_property_bindings(const PropertyCatalog &catalog, const Json &owner,
                                                                     const std::string &path)
{
	std::map<std::string, CustomPropertyValue> result;
	if (!owner.contains("properties"))
		return result;
	const auto &values = owner.at("properties");
	if (!values.is_object() || values.size() > 256)
		property_error(path + ".properties", "expected at most 256 named properties");
	for (const auto &[id, v] : values.items())
	{
		const auto found = catalog.find(id);
		if (found == catalog.end())
			property_error(path + ".properties." + id, "unknown property");
		if (v.is_object() && v.empty())
			result.emplace(id, found->second.initial);
		else
			result.emplace(id, read_custom_property_value(found->second, v, path + ".properties." + id));
	}
	return result;
}
void verify_property_bindings(const Catalog &catalog, SharedPropertyCatalog compiled)
{
	const auto definitions = compiled ? std::move(compiled) : compile_property_catalog(catalog);
	const auto check = [&](const Json &owner, const std::string &path)
	{
		(void)compile_property_bindings(*definitions, owner, path);
	};
	for (const auto &[id, e] : catalog.equipment)
		check(e, "equipment." + id);
	for (const auto &[id, e] : catalog.entities)
	{
		const auto path = "entities." + id;
		check(e, path);
		if (e.contains("systems"))
			for (const auto &[key, s] : e.at("systems").items())
				check(s, path + ".systems." + key);
		if (e.contains("combat") && e.at("combat").contains("decks"))
			for (const auto &deck : e.at("combat").at("decks"))
				for (const auto &c : deck.at("compartments"))
					check(c, path + ".combat.decks.compartments");
		if (e.contains("personnel") && e.at("personnel").contains("officers"))
			for (const auto &o : e.at("personnel").at("officers"))
				check(o, path + ".personnel.officers");
	}
}
} // namespace phoenix
