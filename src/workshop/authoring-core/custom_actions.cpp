module;
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>
module phoenix.workshop.authoring_core;
namespace phoenix
{
namespace
{
struct Action
{
	const char *id;
	Json arguments;
	std::function<void(Json &, const Json &)> apply;
};
Json args(std::initializer_list<std::pair<const char *, const char *>> required)
{
	Json names = Json::array(), properties = Json::object();
	for (const auto &[name, type] : required)
	{
		names.push_back(name);
		properties[name] = std::string(type) == "any" ? Json::object() : Json{{"type", type}};
	}
	return {{"type", "object"}, {"required", names}, {"properties", properties}};
}
std::string fresh(const Json &object, const std::string &prefix)
{
	unsigned serial = 1;
	std::string name;
	do
	{
		name = prefix + std::to_string(serial++);
	} while (object.contains(name));
	return name;
}
const std::vector<Action> &actions()
{
	static const std::vector<Action> all = []
	{
		std::vector<Action> result;
		result.push_back({"dust.reset", args({{"platform", "string"}}), [](Json &candidate, const Json &a)
		                  {
			                  const auto platform = a.at("platform").get<std::string>();
			                  if (platform != "native" && platform != "web")
				                  throw std::runtime_error("Unknown dust platform");
			                  candidate[platform] = Json::object();
		                  }});

		result.push_back({"object.rename", args({{"from", "string"}, {"to", "string"}}),
		                  [](Json &candidate, const Json &a)
		                  {
			                  const auto from = a.at("from").get<std::string>(), to = a.at("to").get<std::string>();
			                  if (!from.empty() && !to.empty() && candidate.contains(from) && !candidate.contains(to))
			                  {
				                  candidate[to] = candidate.at(from);
				                  candidate.erase(from);
			                  }
		                  }});
		result.push_back({"object.add", args({{"prefix", "string"}, {"type", "string"}}),
		                  [](Json &candidate, const Json &a)
		                  {
			                  Json value;
			                  (void)workshop_custom_action(value, "value.type", {{"type", a.at("type")}});
			                  candidate[fresh(candidate, a.at("prefix"))] = value;
		                  }});
		result.push_back({"effect.operations", args({}), [](Json &candidate, const Json &)
		                  {
			                  candidate.erase("contributions");
			                  candidate.erase("duration_ticks");
		                  }});
		result.push_back({"effect.contributions", args({}), [](Json &candidate, const Json &)
		                  {
			                  candidate.erase("operations");
			                  candidate.erase("payer");
			                  candidate.erase("energy");
		                  }});
		result.push_back({"effect.contribution.add", args({{"modifier", "boolean"}}), [](Json &candidate, const Json &a)
		                  {
			                  auto &rows = candidate["contributions"];
			                  if (!rows.is_object())
				                  rows = Json::object();
			                  std::string name = "arc";
			                  unsigned index = 1;
			                  while (rows.contains(name))
				                  name = "arc" + std::to_string(index++);
			                  rows[name] = a.at("modifier").get<bool>()
			                                   ? Json{{"property", "recharge"}, {"delta_permille", 100}}
			                                   : Json{{"capacity", 100}};
		                  }});

		result.push_back({"field.ensure", args({{"field", "string"}, {"type", "string"}}),
		                  [](Json &candidate, const Json &a)
		                  {
			                  const auto key = a.at("field").get<std::string>();
			                  if (!candidate.contains(key))
			                  {
				                  Json value;
				                  (void)workshop_custom_action(value, "value.type", {{"type", a.at("type")}});
				                  candidate[key] = value;
			                  }
		                  }});

		result.push_back({"controller.roles", args({{"definition", "object"}}), [](Json &candidate, const Json &a)
		                  {
			                  auto &equipment = candidate["equipment"];
			                  if (!equipment.is_object())
				                  equipment = Json::object();
			                  const auto roles = a.at("definition").value("equipment", Json::object());
			                  for (auto it = equipment.begin(); it != equipment.end();)
				                  if (!roles.contains(it.key()))
					                  it = equipment.erase(it);
				                  else
					                  ++it;
			                  if (!candidate.value("star_levels", Json()).is_object())
				                  candidate["star_levels"] = Json::object();
		                  }});

		result.push_back(
		    {"property.initial", args({{"definition", "object"}}), [](Json &candidate, const Json &a)
		     {
			     const auto d = a.at("definition");
			     const auto kind = d.value("kind", std::string("quantity"));
			     candidate = d.value("default", kind == "flag" ? Json(false) : kind == "enum" ? Json("") : Json(0));
		     }});
		result.push_back({"property.default", args({}), [](Json &candidate, const Json &)
		                  {
			                  candidate = Json::object();
		                  }});
		result.push_back(
		    {"field.choice",
		     args({{"field", "string"}, {"value", "string"}, {"choices", "array"}, {"optional", "boolean"}}),
		     [](Json &candidate, const Json &a)
		     {
			     Json spec{{"key", a.at("field")},
			               {"type", "string"},
			               {"default", ""},
			               {"choices", a.at("choices")},
			               {"optional", a.at("optional")}};
			     workshop_set_field(candidate, spec, a.at("value"));
		     }});
		result.push_back(
		    {"layout.rect.gesture",
		     args({{"rect", "object"}, {"dx", "number"}, {"dy", "number"}, {"resize", "boolean"}, {"snap", "boolean"}}),
		     [](Json &candidate, const Json &arguments)
		     {
			     auto rect = arguments.at("rect");
			     const auto x = checked_integer(rect.at("x"), 0, 999, "rect.x"),
			                y = checked_integer(rect.at("y"), 0, 999, "rect.y");
			     const auto width = checked_integer(rect.at("width"), 1, 1000 - x, "rect.width"),
			                height = checked_integer(rect.at("height"), 1, 1000 - y, "rect.height");
			     const auto dx = arguments.at("dx").get<double>(), dy = arguments.at("dy").get<double>();
			     if (!std::isfinite(dx) || !std::isfinite(dy))
				     throw std::runtime_error("Expected finite gesture delta");
			     const double step = arguments.at("snap").get<bool>() ? 10 : 1;
			     auto change = [&](double value, double delta, double low, double high)
			     {
				     return static_cast<std::int64_t>(std::clamp(std::round((value + delta) / step) * step, low, high));
			     };
			     if (arguments.at("resize").get<bool>())
			     {
				     rect["width"] = change(width, dx, 1, 1000 - x);
				     rect["height"] = change(height, dy, 1, 1000 - y);
			     }
			     else
			     {
				     rect["x"] = change(x, dx, 0, 1000 - width);
				     rect["y"] = change(y, dy, 0, 1000 - height);
			     }
			     candidate["rect"] = rect;
		     }});
		result.push_back({"list.member", args({{"field", "string"}, {"value", "any"}, {"enabled", "boolean"}}),
		                  [](Json &candidate, const Json &arguments)
		                  {
			                  const auto key = arguments.at("field").get<std::string>();
			                  const auto value = arguments.at("value");
			                  const auto enabled = arguments.at("enabled").get<bool>();
			                  if (!candidate.contains(key))
				                  candidate[key] = Json::array();
			                  auto &rows = candidate.at(key);
			                  if (!rows.is_array())
				                  throw std::runtime_error("Membership requires an array");
			                  auto found = std::find(rows.begin(), rows.end(), value);
			                  if (enabled && found == rows.end())
				                  rows.push_back(value);
			                  else if (!enabled && found != rows.end())
				                  rows.erase(found);
		                  }});
		result.push_back({"array.move", args({{"from", "integer"}, {"to", "integer"}}),
		                  [](Json &candidate, const Json &arguments)
		                  {
			                  if (!candidate.is_array() || candidate.empty())
				                  throw std::runtime_error("Move requires a nonempty array");
			                  const auto from = checked_unsigned(arguments.at("from"), 0, candidate.size() - 1, "from"),
			                             to = checked_unsigned(arguments.at("to"), 0, candidate.size() - 1, "to");
			                  auto value = candidate.at(from);
			                  candidate.erase(candidate.begin() + from);
			                  candidate.insert(candidate.begin() + to, value);
		                  }});
		result.push_back({"value.type", args({{"type", "string"}}), [](Json &candidate, const Json &arguments)
		                  {
			                  const auto type = arguments.at("type").get<std::string>();
			                  if (type != "integer" && type != "boolean" && type != "text" && type != "object" &&
			                      type != "array" && type != "null")
				                  throw std::runtime_error("Unknown structured value type");
			                  candidate = type == "integer"   ? Json(0)
			                              : type == "boolean" ? Json(false)
			                              : type == "text"    ? Json("")
			                              : type == "object"  ? Json::object()
			                              : type == "array"   ? Json::array()
			                                                  : Json();
		                  }});
		result.push_back({"sprite.entries", args({}), [](Json &candidate, const Json &arguments)
		                  {
			                  candidate["entries"] =
			                      Json::array({Json{{"name", "default"}, {"frames", candidate.at("frames")}}});
			                  candidate.erase("frames");
		                  }});
		result.push_back({"sprite.entry.duplicate", args({}), [](Json &candidate, const Json &arguments)
		                  {
			                  auto &entries = candidate.at("entries");
			                  if (entries.empty())
				                  throw std::runtime_error("No sprite entry to duplicate");
			                  auto entry = entries.back();
			                  unsigned serial = static_cast<unsigned>(entries.size() + 1);
			                  std::string name;
			                  do
			                  {
				                  name = "entry-" + std::to_string(serial++);
			                  } while (std::any_of(entries.begin(), entries.end(),
			                                       [&](const Json &row)
			                                       {
				                                       return row.at("name") == name;
			                                       }));
			                  entry["name"] = name;
			                  entries.push_back(entry);
		                  }});
		result.push_back({"sprite.attachment.add", args({}), [](Json &candidate, const Json &arguments)
		                  {
			                  const std::string action = "sprite.attachment.add";
			                  if (action == "sprite.attachment.add" || action == "model.attachment.add")
			                  {
				                  auto &attachments = candidate["attachments"];
				                  if (attachments.is_null())
					                  attachments = Json::object();
				                  unsigned serial = 1;
				                  std::string name;
				                  do
				                  {
					                  name = "engine_" + std::to_string(serial++);
				                  } while (attachments.contains(name));
				                  if (action == "sprite.attachment.add")
				                  {
					                  const auto pivot = candidate.at("pivot");
					                  attachments[name] = {
					                      {"x", pivot[0]}, {"y", pivot[1]}, {"layer", 0}, {"depth_offset", 0}};
				                  }
				                  else
					                  attachments[name] = {{"position", {0, 0, 0}}, {"direction", {0, 0, 1}}};
			                  }
		                  }});
		result.push_back({"model.attachment.add", args({}), [](Json &candidate, const Json &arguments)
		                  {
			                  const std::string action = "model.attachment.add";
			                  if (action == "sprite.attachment.add" || action == "model.attachment.add")
			                  {
				                  auto &attachments = candidate["attachments"];
				                  if (attachments.is_null())
					                  attachments = Json::object();
				                  unsigned serial = 1;
				                  std::string name;
				                  do
				                  {
					                  name = "engine_" + std::to_string(serial++);
				                  } while (attachments.contains(name));
				                  if (action == "sprite.attachment.add")
				                  {
					                  const auto pivot = candidate.at("pivot");
					                  attachments[name] = {
					                      {"x", pivot[0]}, {"y", pivot[1]}, {"layer", 0}, {"depth_offset", 0}};
				                  }
				                  else
					                  attachments[name] = {{"position", {0, 0, 0}}, {"direction", {0, 0, 1}}};
			                  }
		                  }});
		result.push_back({"radar.icon", args({{"value", "any"}}), [](Json &candidate, const Json &arguments)
		                  {
			                  const auto value = arguments.at("value");
			                  candidate.erase("radar_icon");
			                  if (value.is_null())
				                  candidate.erase("radar_sprite");
			                  else
				                  candidate["radar_sprite"] = value;
		                  }});
		result.push_back({"controller.policy", args({}), [](Json &candidate, const Json &arguments)
		                  {
			                  if (!candidate.value("policy", std::string{}).empty())
			                  {
				                  candidate.erase("tree");
				                  candidate["fallback"] = "hold";
			                  }
		                  }});
		result.push_back({"controller.tree", args({}), [](Json &candidate, const Json &arguments)
		                  {
			                  if (candidate.contains("tree") && (candidate.at("tree").is_object() ||
			                                                     !candidate.at("tree").get<std::string>().empty()))
				                  candidate.erase("fallback");
			                  else
			                  {
				                  candidate.erase("tree");
				                  candidate["fallback"] = "hold";
			                  }
		                  }});
		result.push_back({"controller.arguments", args({}), [](Json &candidate, const Json &arguments)
		                  {
			                  workshop_controller_arguments(candidate);
		                  }});
		result.push_back({"audio.add", args({{"field", "string"}, {"shield", "boolean"}}),
		                  [](Json &candidate, const Json &arguments)
		                  {
			                  candidate[arguments.at("field").get<std::string>()] = {
			                      {"asset", ""}, {"gain", arguments.at("shield").get<bool>() ? .06 : 1.0}};
		                  }});
		result.push_back({"gameplay.panel", args({}), [](Json &candidate, const Json &arguments)
		                  {
			                  candidate["gameplay_panel"] = {{"x", 0.0}, {"y", 0.0}, {"width", 1.0}, {"height", 1.0}};
		                  }});
		result.push_back({"process.declare", args({}), [](Json &candidate, const Json &arguments)
		                  {
			                  auto &rows = candidate["processes"];
			                  if (rows.is_null())
				                  rows = Json::object();
			                  rows[fresh(rows, "process")] = {{"process", ""}, {"requested", true}};
		                  }});
		result.push_back({"ability.endpoint", args({}), [](Json &candidate, const Json &arguments)
		                  {
			                  candidate[fresh(candidate, "ability")] = {{"source", "equipment"}, {"owner", ""},
			                                                            {"ability", ""},         {"console", ""},
			                                                            {"controller", ""},      {"enabled", ""}};
		                  }});
		result.push_back({"tree.parameter.add", args({{"name", "string"}}), [](Json &candidate, const Json &arguments)
		                  {
			                  const auto name = arguments.at("name").get<std::string>();
			                  if (name.empty())
				                  throw std::runtime_error("Parameter name cannot be empty");
			                  auto &rows = candidate["parameters"];
			                  if (rows.is_null())
				                  rows = Json::object();
			                  if (!rows.contains(name))
				                  rows[name] = {{"type", "integer"}, {"default", 0}};
		                  }});
		result.push_back({"tree.binding.add", args({{"field", "string"}, {"name", "string"}}),
		                  [](Json &candidate, const Json &arguments)
		                  {
			                  const auto name = arguments.at("name").get<std::string>();
			                  if (name.empty())
				                  throw std::runtime_error("Binding name cannot be empty");
			                  auto &rows = candidate[arguments.at("field").get<std::string>()];
			                  if (rows.is_null())
				                  rows = Json::object();
			                  if (!rows.contains(name))
				                  rows[name] = 0;
		                  }});
		result.push_back({"assembly.system.add", args({{"name", "string"}}), [](Json &candidate, const Json &a)
		                  {
			                  candidate["systems"][a.at("name").get<std::string>()] = {{"template", ""}};
		                  }});
		result.push_back({"service.add", args({{"name", "string"}, {"direction", "string"}}),
		                  [](Json &candidate, const Json &a)
		                  {
			                  const auto direction = a.at("direction").get<std::string>();
			                  if (direction != "input" && direction != "output")
				                  throw std::runtime_error("Unsupported service direction");
			                  const auto name = a.at("name").get<std::string>();
			                  if (!candidate.contains("services"))
				                  candidate["services"] = Json::object();
			                  if (!candidate.at("services").contains(name))
			                  {
				                  candidate["services"][name] = {{"kind", "power"}, {"direction", direction}};
				                  if (direction == "output")
					                  candidate["services"][name]["target"] = "";
			                  }
		                  }});
		result.push_back({"system.add", args({{"name", "string"}}), [](Json &candidate, const Json &a)
		                  {
			                  const auto name = a.at("name").get<std::string>();
			                  if (!candidate.value("systems", Json::object()).contains(name))
				                  (void)workshop_ship_add(candidate, {{"kind", "System"}, {"id", name}});
		                  }});
		result.push_back(
		    {"process.endpoint", args({}), [](Json &candidate, const Json &)
		     {
			     candidate[fresh(candidate, "process")] = {
			         {"source", "equipment"}, {"owner", ""}, {"process", ""}, {"controller", ""}, {"enabled", ""}};
		     }});
		result.push_back({"console.compact", args({}), [](Json &candidate, const Json &)
		                  {
			                  candidate["compact"] = {{"widgets", candidate.value("widgets", Json::array())}};
		                  }});
		result.push_back(
		    {"actor.bind", args({{"field", "string"}, {"slots", "array"}}), [](Json &candidate, const Json &a)
		     {
			     const auto slots = a.at("slots");
			     candidate[a.at("field").get<std::string>()] = {{"slot", slots.empty() ? Json("") : slots[0]}};
		     }});
		result.push_back({"objective.recipients", args({{"slots", "array"}}), [](Json &candidate, const Json &a)
		                  {
			                  const auto slots = a.at("slots");
			                  candidate["recipients"] = {{"ship", {{"slot", slots.empty() ? Json("") : slots[0]}}},
			                                             {"consoles", Json::array()}};
		                  }});
		result.push_back({"property.modifier", args({{"definition", "object"}}), [](Json &candidate, const Json &a)
		                  {
			                  const auto kind = a.at("definition").value("kind", std::string{});
			                  if (kind == "flag" || kind == "enum")
				                  candidate.erase("delta_permille");
			                  else
			                  {
				                  candidate.erase("priority");
				                  candidate.erase("value");
			                  }
		                  }});
		result.push_back({"effect.cost", args({}), [](Json &candidate, const Json &)
		                  {
			                  if (!candidate.value("property", std::string{}).empty())
			                  {
				                  candidate.erase("store");
				                  candidate.erase("resource");
			                  }
		                  }});
		result.push_back({"telemetry.field", args({{"name", "string"}}), [](Json &candidate, const Json &a)
		                  {
			                  candidate["fields"][a.at("name").get<std::string>()] = "requested";
		                  }});
		result.push_back({"control.default", args({{"control", "string"}}), [](Json &candidate, const Json &a)
		                  {
			                  const auto spec = workshop_control(a.at("control"), candidate);
			                  (void)workshop_set_control(candidate, a.at("control"), spec.at("default"));
		                  }});
		return result;
	}();
	return all;
}
} // namespace
Json workshop_custom_actions()
{
	Json result = Json::array();
	for (const auto &action : actions())
		result.push_back({{"id", action.id}, {"arguments", action.arguments}});
	return result;
}
Json workshop_custom_action(Json &owner, const std::string &id, const Json &arguments)
{
	for (const auto &action : actions())
		if (id == action.id)
		{
			if (!arguments.is_object())
				throw std::runtime_error("Expected action argument object");
			for (const auto &key : action.arguments.at("required"))
				if (!arguments.contains(key.get<std::string>()))
					throw std::runtime_error("Missing authoring argument: " + key.get<std::string>());
			for (const auto &[key, value] : arguments.items())
			{
				if (!action.arguments.at("properties").contains(key))
					throw std::runtime_error("Unknown authoring argument: " + key);
				const auto &spec = action.arguments.at("properties").at(key);
				if (!spec.contains("type"))
					continue;
				const auto type = spec.at("type");
				if ((type == "string" && !value.is_string()) || (type == "object" && !value.is_object()) ||
				    (type == "array" && !value.is_array()) || (type == "boolean" && !value.is_boolean()) ||
				    (type == "integer" && !value.is_number_integer()) || (type == "number" && !value.is_number()))
					throw std::runtime_error("Invalid authoring argument: " + key);
			}
			auto candidate = owner;
			action.apply(candidate, arguments);
			workshop_publish(owner, candidate);
			return owner;
		}
	throw std::runtime_error("Unknown authoring action: " + id);
}
} // namespace phoenix
