module;
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
module phoenix.content.effects.types;
namespace phoenix
{
const std::vector<ModifierPropertyDescriptor> &modifier_properties()
{
	static const std::vector<ModifierPropertyDescriptor> properties{
	    {PropertyKey::propulsion,
	     "propulsion",
	     "mm_per_second_squared",
	     {OperatingSystem::Engine, OperatingSystem::Lateral, OperatingSystem::Boost, OperatingSystem::Impulse}},
	    {PropertyKey::speed,
	     "speed",
	     "mm_per_second",
	     {OperatingSystem::Engine, OperatingSystem::Boost, OperatingSystem::Impulse}},
	    {PropertyKey::turn, "turn", "mdeg_per_second", {OperatingSystem::Steering, OperatingSystem::Engine}, 360000},
	    {PropertyKey::sensors, "sensors", "mm", {OperatingSystem::Radar}},
	    {PropertyKey::cycling, "cycling", "relative_permille", {OperatingSystem::Weapon}},
	    {PropertyKey::traverse, "traverse", "mdeg_per_second", {OperatingSystem::Turret}, 360000},
	    {PropertyKey::reload, "reload", "relative_permille", {OperatingSystem::Magazine}},
	    {PropertyKey::processing,
	     "processing",
	     "work_per_second",
	     {OperatingSystem::Engine,    OperatingSystem::Steering,     OperatingSystem::Radar,
	      OperatingSystem::Generator, OperatingSystem::Storage,      OperatingSystem::Distribution,
	      OperatingSystem::Coolant,   OperatingSystem::Lateral,      OperatingSystem::Boost,
	      OperatingSystem::Impulse,   OperatingSystem::Weapon,       OperatingSystem::Turret,
	      OperatingSystem::Magazine,  OperatingSystem::Team,         OperatingSystem::Shield,
	      OperatingSystem::Comms,     OperatingSystem::Displacement, OperatingSystem::Dispatch,
	      OperatingSystem::Ftl,       OperatingSystem::ShuttleBay,   OperatingSystem::Psychic}},
	    {PropertyKey::recharge, "recharge", "milli_strength_per_second", {OperatingSystem::Shield}},
	    {PropertyKey::off_recharge, "off_recharge", "milli_strength_per_second", {OperatingSystem::Shield}},
	    {PropertyKey::sustain, "sustain", "energy_per_second", {OperatingSystem::Shield}},
	    {PropertyKey::discharge, "discharge", "strength_per_second", {OperatingSystem::Shield}, 1000000},
	    {PropertyKey::protection, "protection", "relative_permille", {OperatingSystem::Shield}, 1000},
	    {PropertyKey::repair, "repair", "native_work_units", {OperatingSystem::Team}, 1000000000000000000LL},
	    {PropertyKey::medical, "medical", "relative_permille", {OperatingSystem::Team}},
	    {PropertyKey::security, "security", "relative_permille", {OperatingSystem::Team}},
	    {PropertyKey::comms, "comms", "mm", {OperatingSystem::Comms}},
	    {PropertyKey::generation, "generation", "energy_per_second", {OperatingSystem::Generator}},
	    {PropertyKey::cooling, "cooling", "heat_per_second", {OperatingSystem::Coolant}}};
	return properties;
}
PropertyKey property_key(std::string_view name)
{
	for (const auto &descriptor : modifier_properties())
		if (descriptor.id == name)
			return descriptor.key;
	throw std::runtime_error("Unknown native property");
}
const ModifierPropertyDescriptor &modifier_property(PropertyKey key)
{
	static const auto indexed = []
	{
		std::array<const ModifierPropertyDescriptor *, static_cast<std::size_t>(PropertyKey::count)> result{};
		for (const auto &descriptor : modifier_properties())
		{
			auto &slot = result.at(static_cast<std::size_t>(descriptor.key));
			if (slot)
				throw std::logic_error("Duplicate native property key");
			slot = &descriptor;
		}
		if (std::any_of(result.begin(), result.end(),
		                [](auto value)
		                {
			                return value == nullptr;
		                }))
			throw std::logic_error("Incomplete native property catalogue");
		return result;
	}();
	return *indexed.at(static_cast<std::size_t>(key));
}
bool known_modifier_property(std::string_view property)
{
	return std::any_of(modifier_properties().begin(), modifier_properties().end(),
	                   [&](const auto &d)
	                   {
		                   return d.id == property;
	                   });
}
bool property_compatible(std::string_view property, OperatingSystem kind)
{
	for (const auto &d : modifier_properties())
		if (d.id == property)
			return std::find(d.owners.begin(), d.owners.end(), kind) != d.owners.end();
	return false;
}
std::string modifier_property_role(std::string_view property)
{
	return std::string(property);
}
} // namespace phoenix
