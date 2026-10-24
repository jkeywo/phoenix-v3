module;
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <set>
#include <stdexcept>
#include <nlohmann/json.hpp>
module phoenix.content;
namespace phoenix
{
namespace
{
void bridge_require(bool valid, const std::string &path, const std::string &reason)
{
	if (!valid)
		throw std::runtime_error(path + ": " + reason);
}
std::vector<BridgeChoice> choices(const Json &rows, const std::string &path)
{
	bridge_require(rows.is_array() && rows.size() <= 64, path, "expected at most 64 choices");
	std::vector<BridgeChoice> result;
	std::set<std::string> ids;
	for (const auto &row : rows)
	{
		BridgeChoice choice{row.at("id"), row.at("label"), row.value("description", std::string{})};
		bridge_require(
		    !choice.id.empty() && choice.id.size() <= 128 &&
		        choice.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") ==
		            std::string::npos &&
		        ids.insert(choice.id).second && !choice.label.empty(),
		    path, "invalid or duplicate choice ID/label");
		result.push_back(std::move(choice));
	}
	return result;
}
template <typename T>
std::size_t initial_choice(const std::vector<T> &rows, const Json &source, const std::string &field,
                           const std::string &path)
{
	if (rows.empty())
	{
		bridge_require(!source.contains(field), path, "default requires choices");
		return 0;
	}
	const auto id = source.value(field, rows.front().id);
	for (std::size_t i = 0; i < rows.size(); ++i)
		if (rows[i].id == id)
			return i;
	throw std::runtime_error(path + ": unresolved " + field);
}
} // namespace
ConsoleBridgeDefinition compile_console_bridge(const Json &source, const std::string &path)
{
	ConsoleBridgeDefinition result;
	result.stances = choices(source.value("stances", Json::array()), path + ".stances");
	result.initial = initial_choice(result.stances, source, "default_stance", path);
	result.command = source.value("command", false);
	result.auto_hide = source.value("hide_without_ai", false);
	result.viewscreen = source.value("viewscreen", false);
	result.objectives = source.value("objectives", false);
	bridge_require(!result.auto_hide || result.command, path, "hide_without_ai requires Command");
	if (result.viewscreen)
	{
		bridge_require(source.value("human_seeking", result.command), path, "Viewscreen Console must be human-seeking");
		bridge_require(source.contains("compact") && source.at("compact").is_object(), path,
		               "Viewscreen Console requires compact HUD");
		result.compact = source.at("compact");
		bridge_require(result.compact.contains("widgets") && result.compact.at("widgets").is_array(), path,
		               "compact HUD requires widgets");
		std::set<std::string> ids;
		for (const auto &widget : result.compact.at("widgets"))
		{
			const auto id = widget.at("id").get<std::string>();
			const auto kind = widget.at("kind").get<std::string>();
			const auto binding = widget.at("binding").get<std::string>();
			const auto bindings = widget_bindings();
			bridge_require(ids.insert(id).second && !id.empty() &&
			                   std::any_of(bindings.begin(), bindings.end(),
			                               [&](const auto &b)
			                               {
				                               return b.kind == kind && b.binding == binding;
			                               }),
			               path, "invalid compact widget identity or binding");
			for (const auto *key : {"x", "y", "width", "height"})
				bridge_require(widget.at(key).is_number_integer() && widget.at(key) >= 0 && widget.at(key) <= 1000,
				               path, "compact widget bounds");
			bridge_require(widget.at("width") > 0 && widget.at("height") > 0 &&
			                   widget.at("x").get<int>() + widget.at("width").get<int>() <= 1000 &&
			                   widget.at("y").get<int>() + widget.at("height").get<int>() <= 1000,
			               path, "compact widget exceeds layout");
			bridge_require(!widget.at("label").get<std::string>().empty(), path, "compact widget requires label");
			const auto &full = source.at("widgets");
			const auto original = std::find_if(full.begin(), full.end(),
			                                   [&](const auto &value)
			                                   {
				                                   return value.at("id") == id;
			                                   });
			bridge_require(original != full.end(), path, "compact widget must reference a full-layout widget");
			for (const auto *field : {"kind", "binding", "controller", "channel", "system"})
				bridge_require(original->value(field, Json(nullptr)) == widget.value(field, Json(nullptr)), path,
				               "compact widget cannot change its control or observation binding");
		}
	}
	return result;
}
BridgeDefinition compile_bridge(const Catalog &catalog, const std::string &entity)
{
	BridgeDefinition result;
	const auto &source = catalog.entities.at(entity);
	if (!source.contains("bridge"))
		return result;
	const auto &bridge = source.at("bridge");
	const auto rows = bridge.value("conditions", Json::array());
	const auto basic = choices(rows, "entities." + entity + ".bridge.conditions");
	for (std::size_t i = 0; i < basic.size(); ++i)
	{
		AlertCondition condition;
		static_cast<BridgeChoice &>(condition) = basic[i];
		condition.colour = rows[i].value("colour", std::string("#000000"));
		condition.glow = rows[i].value("glow", false);
		condition.siren = rows[i].value("siren", std::string{});
		condition.music = rows[i].value("music", std::string{});
		condition.music_gain = rows[i].value("music_gain", 1.f);
		bridge_require(std::isfinite(condition.music_gain) && condition.music_gain >= 0 && condition.music_gain <= 1,
		               entity, "condition music_gain must be 0..1");
		bridge_require(condition.colour.size() == 7 && condition.colour.front() == '#' &&
		                   condition.colour.substr(1).find_first_not_of("0123456789abcdefABCDEF") == std::string::npos,
		               entity, "condition colour must be #RRGGBB");
		for (const auto &asset : {condition.siren, condition.music})
		{
			bridge_require(asset.empty() || catalog.assets.contains(asset), entity,
			               "unresolved condition audio " + asset);
			if (!asset.empty())
				verify_audio_asset(catalog, asset, entity + ".bridge.conditions");
		}
		result.conditions.push_back(std::move(condition));
	}
	result.initial = initial_choice(result.conditions, bridge, "initial", entity);
	result.alert_console = bridge.value("alert_console", std::string{});
	result.sidebar_default = bridge.value("sidebar_default", std::string{});
	const auto consoles = resolve_consoles(catalog, entity);
	for (const auto &id : {result.alert_console, result.sidebar_default})
		if (!id.empty())
			bridge_require(std::any_of(consoles.begin(), consoles.end(),
			                           [&](const auto &c)
			                           {
				                           return c.id == id;
			                           }),
			               entity, "unresolved bridge Console " + id);
	for (const auto &console : consoles)
		if (console.id == result.sidebar_default)
			bridge_require(
			    compile_console_bridge(catalog.consoles.at(console.definition), console.definition).viewscreen, entity,
			    "default sidebar Console must support Viewscreen");
	bridge_require(result.conditions.empty() == result.alert_console.empty(), entity,
	               "conditions require an alert-owning Console");
	return result;
}
} // namespace phoenix
