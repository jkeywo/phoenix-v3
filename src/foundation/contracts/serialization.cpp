module;
#include <nlohmann/json.hpp>

module phoenix.contracts;

namespace phoenix
{
void to_json(Json &value, const ConsoleAddress &address)
{
	value["ship"] = address.ship;
	value["console"] = address.console;
}
void from_json(const Json &value, ConsoleAddress &address)
{
	value.at("ship").get_to(address.ship);
	value.at("console").get_to(address.console);
}
} // namespace phoenix
