module;
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>
module phoenix.workshop.authoring_core;
import phoenix.content.operations;
import phoenix.content.assets;
import phoenix.content.image_codec;
namespace phoenix
{
void workshop_publish(Json &owner, const Json &candidate)
{
	if (owner == candidate)
		return;
	if (owner.is_object() && candidate.is_object())
	{
		for (auto it = owner.begin(); it != owner.end();)
			if (!candidate.contains(it.key()))
				it = owner.erase(it);
			else
				++it;
		for (const auto &[key, value] : candidate.items())
		{
			if (owner.contains(key))
				workshop_publish(owner.at(key), value);
			else
				owner[key] = value;
		}
	}
	else if (owner.is_array() && candidate.is_array() && owner.size() == candidate.size())
		for (std::size_t i = 0; i < owner.size(); ++i)
			workshop_publish(owner[i], candidate[i]);
	else
		owner = candidate;
}

namespace
{
std::string component(const std::string &key)
{
	return (Json::json_pointer("") / key).to_string();
}
void erase_at(Json &owner, const Json::json_pointer &path)
{
	auto parent = path;
	const auto key = parent.back();
	parent.pop_back();
	if (owner.contains(parent))
		owner.at(parent).erase(key);
}
bool presence(const std::string &change)
{
	return change == "seed" || change == "optional-asset" || change == "grid" || change == "grid-plane" ||
	       change.starts_with("region-") || change == "long-range" || change == "widget-controller" ||
	       change == "theme-slice" || change == "procedural";
}
} // namespace
Json workshop_colour_channels(const std::string &text)
{
	if ((text.size() != 7 && text.size() != 9) || text.front() != '#')
		return Json();
	Json result = Json::array();
	auto nibble = [](char c) -> int
	{
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		if (c >= 'A' && c <= 'F')
			return c - 'A' + 10;
		return -1;
	};
	for (std::size_t i = 1; i < text.size(); i += 2)
	{
		const int a = nibble(text[i]), b = nibble(text[i + 1]);
		if (a < 0 || b < 0)
			return Json();
		result.push_back((a * 16 + b) / 255.0);
	}
	if (result.size() == 3)
		result.push_back(1.0);
	return result;
}
std::string workshop_colour_text(const Json &channels)
{
	if (!channels.is_array() || channels.size() != 4)
		throw std::runtime_error("Expected four colour channels");
	static constexpr char hex[] = "0123456789abcdef";
	std::string result = "#";
	for (const auto &channel : channels)
	{
		if (!channel.is_number() || !std::isfinite(channel.get<double>()))
			throw std::runtime_error("Expected finite colour channels");
		const auto n = static_cast<unsigned>(std::round(std::clamp(channel.get<double>(), 0.0, 1.0) * 255));
		result += hex[n / 16];
		result += hex[n % 16];
	}
	return result;
}
Json workshop_control(const std::string &id, const Json &owner)
{
	return workshop_control(id, owner, Json::object());
}
Json workshop_set_control(Json &owner, const std::string &id, const Json &value)
{
	return workshop_set_control(owner, id, value, Json::object());
}
Json workshop_custom_action(Json &owner, const std::string &id)
{
	return workshop_custom_action(owner, id, Json::object());
}
Json workshop_control(const std::string &id, const Json &owner, const Json &context)
{
	auto spec = workshop_field(id);
	spec["id"] = id;
	if (!context.is_object())
		throw std::runtime_error("Expected control context object");
	const auto schema = spec.value("context", Json{{"required", Json::array()}, {"properties", Json::object()}});
	for (const auto &key : schema.at("required"))
		if (!context.contains(key.get<std::string>()))
			throw std::runtime_error("Missing control context: " + key.get<std::string>());
	for (const auto &[key, value] : context.items())
	{
		if (!schema.at("properties").contains(key))
			throw std::runtime_error("Unknown control context: " + key);
		const auto type = schema.at("properties").at(key).at("type");
		if ((type == "string" && !value.is_string()) || (type == "object" && !value.is_object()) ||
		    (type == "array" && !value.is_array()) || (type == "integer" && !value.is_number_integer()))
			throw std::runtime_error("Wrong control context type: " + key);
		if (type == "integer")
			(void)checked_unsigned(value, 0, std::numeric_limits<std::int64_t>::max(), key);
	}
	const auto key = spec.at("key").get<std::string>();
	spec["path"] = component(key);
	if (spec.contains("default_from"))
		spec["default"] = owner.value(spec.at("default_from").get<std::string>(), spec.at("default"));
	const auto provider = spec.value("provider", std::string{});
	if (provider == "resource-limit")
		spec["default"] = resource_limit;
	if (provider == "inherited-default")
		spec["default"] = context.at("resolved").value(key, spec.at("default"));
	if (provider == "grid-default")
		spec["default"] = reference_grid_defaults().at(key);
	if (provider == "dust-default")
	{
		spec["default"] = owner.value(key, dust_defaults().at(key));
		const auto platform = context.value("platform", std::string{});
		if (!platform.empty() && platform != "native" && platform != "web")
			throw std::runtime_error("Unknown dust platform");
		spec["path"] = platform.empty() ? component(key) : component(platform) + component(key);
	}
	if (provider == "dust-texture")
	{
		const auto platform = context.value("platform", std::string{});
		if (!platform.empty() && platform != "native" && platform != "web")
			throw std::runtime_error("Unknown dust platform");
		const auto index = checked_unsigned(context.at("index"), 0, 2, "texture index");
		const auto defaults = owner.value("textures", dust_defaults().at("textures"));
		spec["container_path"] = (platform.empty() ? std::string{} : component(platform)) + "/textures";
		spec["container_default"] = defaults;
		spec["path"] = spec.at("container_path").get<std::string>() + "/" + std::to_string(index);
		spec["default"] = defaults.at(index);
	}
	if (provider == "frame-index")
		spec["default"] = context.value("frame", 0) * 45.0;
	if (provider == "frame-marker")
	{
		const auto rect = owner.at("rect");
		spec["path"] = "/attachments" + component(context.at("attachment")) + component(key);
		spec["maximum"] = std::max(0.0, rect.at(key == "x" ? 2 : 3).get<double>());
	}
	if (provider == "theme-slot")
		spec["path"] = component(key) + component(context.at("slot"));
	if (provider == "theme-token")
	{
		spec["path"] = "/tokens" + component(context.at("token"));
		const auto &token = owner.at(Json::json_pointer(spec.at("path").get<std::string>()));
		if (token.is_number())
		{
			spec["type"] = "number";
			spec["default"] = 0;
			spec["widget"] = "drag";
			spec["step"] = .5;
			spec["minimum"] = 0;
			spec["maximum"] = 128;
		}
	}
	if (provider == "property")
	{
		const auto definition = context.at("definition");
		const auto kind = definition.value("kind", std::string("quantity"));
		spec["path"] = component(context.at("field"));
		spec["key"] = context.at("field");
		spec["type"] = kind == "flag" ? "boolean" : kind == "enum" ? "string" : "integer";
		spec["default"] = kind == "flag" ? Json(false) : kind == "enum" ? Json("") : Json(0);
		if (definition.contains("default") && (kind == "flag"   ? definition.at("default").is_boolean()
		                                       : kind == "enum" ? definition.at("default").is_string()
		                                                        : definition.at("default").is_number_integer()))
			spec["default"] = definition.at("default");
		if (kind == "enum")
		{
			spec["choices"] = Json::array();
			for (const auto &[id, value] : definition.value("choices", Json::object()).items())
				spec["choices"].push_back(id);
			if (!spec.at("choices").empty() && !definition.contains("default"))
				spec["default"] = spec.at("choices")[0];
		}
	}
	if (provider == "binding")
	{
		spec["path"] = component(context.at("field"));
		spec["choices"] = context.value("allowed", Json::array());
		if (spec.at("choices").empty())
			spec["choices"] = Json::array(
			    {"integer", "boolean", "text", "reference", "parameter", "observation", "intent", "memory", "elapsed"});
	}
	if (provider == "operating-actions")
	{
		spec["choices"] = Json::array();
		for (const auto &action : operating_actions)
			spec["choices"].push_back(action.name);
	}
	if (provider == "operating-argument")
	{
		const auto *contract = operating_action(owner.value("action", std::string{}));
		if (!contract)
			throw std::runtime_error("Unknown operating action");
		const auto fields = operating_arguments(contract->action);
		if (fields.empty())
			throw std::runtime_error("Operating action has no arguments");
		const auto index = checked_unsigned(context.at("index"), 0, fields.size() - 1, "argument index");
		const auto &argument = fields[index];
		spec["path"] = "/arguments/" + std::to_string(index);
		spec["default"] = std::max<std::int64_t>(0, argument.minimum);
		spec["minimum"] = argument.minimum;
		spec["maximum"] = argument.maximum;
		spec["label"] = argument.name;
	}
	if (provider == "record-subject")
	{
		const auto type = context.value("tree_context", std::string("system"));
		spec["choices"] = type == "scenario"     ? Json::array({"self", "entity"})
		                  : type == "controller" ? Json::array({"self", "latest", "entity"})
		                                         : Json::array({"self", "selected_contact", "entity"});
	}
	if (provider == "actor")
		spec["path"] = component(context.at("field"));
	if (provider == "root")
	{
		spec["path"] = "";
		spec["type"] = owner.is_string()         ? "string"
		               : owner.is_boolean()      ? "boolean"
		               : owner.is_number_float() ? "number"
		                                         : "integer";
		spec["default"] = owner;
	}
	if (provider == "data-placement")
		spec["path"] = "/data_placement" + component(context.at("star")) + component(context.at("item"));
	const auto path = Json::json_pointer(spec.at("path").get<std::string>());
	const auto change = spec.value("change", std::string{});
	spec["value"] = owner.contains(path) ? owner.at(path) : spec.at("default");
	if (presence(change))
		spec["value"] = owner.contains(path) && !owner.at(path).is_null();
	if (change == "billboard")
		spec["value"] = owner.value("mode", std::string("camera")) == "eight";
	if (change == "sidecar-billboard")
		spec["value"] =
		    owner.value("billboard_mode",
		                owner.value("sprite_sheet", Json::object()).value("frames", Json::array()).size() == 8
		                    ? std::string("eight")
		                    : std::string("camera")) == "eight";
	if (provider == "binding")
	{
		const auto current = owner.contains(path) ? owner.at(path) : Json(0);
		std::string mode = "integer";
		if (current.is_boolean())
			mode = "boolean";
		else if (current.is_string())
			mode = "text";
		else if (current.is_object())
		{
			for (const char *source : {"argument", "parameter", "observation", "intent", "memory", "elapsed"})
				if (current.contains(source))
					mode = source;
			if (current.contains("type"))
				mode = "reference";
		}
		spec["value"] = mode;
	}
	if (provider == "property")
	{
		const auto &current = spec.at("value");
		const auto type = spec.at("type");
		spec["mismatch"] = (type == "boolean" && !current.is_boolean()) || (type == "string" && !current.is_string()) ||
		                   (type == "integer" && !current.is_number_integer());
	}
	if (provider == "record-subject")
	{
		const auto current = owner.value("subject", owner.value("subject_parameter", Json("self")));
		spec["value"] = current.is_string() ? current : Json("entity");
	}
	if (provider == "actor")
	{
		const auto current = owner.contains(path) ? owner.at(path) : Json::object();
		spec["value"] = current.contains("slot") ? "slot" : current.contains("entity") ? "entity" : "handle";
	}
	const auto type = spec.at("type");
	const auto &input = spec.at("value");
	bool mismatch = (type == "integer" && !input.is_number_integer()) || (type == "number" && !input.is_number()) ||
	                (type == "boolean" && !input.is_boolean()) ||
	                (type == "string" && !input.is_string() && !input.is_object());
	if (type == "array")
	{
		mismatch = !input.is_array() || input.size() != spec.at("length").get<std::size_t>();
		if (!mismatch)
			for (const auto &item : input)
				mismatch = mismatch || !item.is_number() ||
				           (spec.value("items", std::string{}) == "integer" && !item.is_number_integer());
	}
	spec["mismatch"] = spec.value("mismatch", false) || mismatch;
	return spec;
}
Json workshop_set_control(Json &owner, const std::string &id, const Json &value, const Json &context)
{
	const auto spec = workshop_control(id, owner, context);
	auto candidate = owner;
	if (spec.contains("container_path"))
	{
		const auto container = Json::json_pointer(spec.at("container_path").get<std::string>());
		if (!candidate.contains(container))
			candidate[container] = spec.at("container_default");
	}
	workshop_set_field(candidate, spec, value);
	const auto path = Json::json_pointer(spec.at("path").get<std::string>());
	const auto change = spec.value("change", std::string{});
	if (presence(change) && !value.get<bool>())
		erase_at(candidate, path);
	else if (change == "seed")
		candidate[path] = workshop_seed(spec.at("seed"));
	else if (change == "optional-asset")
		candidate[path] = "";
	else if (change == "grid")
		candidate[path] = reference_grid_defaults();
	else if (change == "grid-plane")
		candidate[path] = -10;
	else if (change == "region-display")
		candidate[path] = {{"name", owner.value("id", "Region")},
		                   {"navigation", true},
		                   {"radar", true},
		                   {"colour", {.5, .7, .9}},
		                   {"tags", {"contact"}}};
	else if (change == "region-nebula" || change == "region-asteroids")
		candidate[path] = workshop_seed("gameplay-editors.scenarios." + spec.at("key").get<std::string>());
	else if (change == "long-range")
	{
		const auto range = checked_integer(owner.value("range_mm", Json(100000)), 0,
		                                   std::numeric_limits<std::int64_t>::max() / 3, "range_mm");
		candidate[path] = range * 3;
	}
	else if (change == "procedural")
	{
		candidate.erase("model");
		candidate["star"] = workshop_seed("workflow.appearance.star");
	}
	if (change == "procedural" && !value.get<bool>())
	{
		candidate.erase("star");
		candidate["model"] = workshop_seed("workflow.appearance.model");
	}
	if (change == "billboard")
	{
		candidate["mode"] = value.get<bool>() ? "eight" : "camera";
		const auto images = owner.value("images", Json::array());
		candidate["images"] = Json::array();
		for (int i = 0; i < (value.get<bool>() ? 8 : 1); ++i)
			candidate["images"].push_back(images.empty() ? Json("") : images[0]);
	}
	if (change == "sidecar-billboard")
		candidate["billboard_mode"] = value.get<bool>() ? "eight" : "camera";
	if (change == "collision")
	{
		if (value == "box")
		{
			candidate.erase("radius_mm");
			candidate["half_width_mm"] = 2000;
			candidate["half_length_mm"] = 4000;
		}
		else
		{
			candidate.erase("half_width_mm");
			candidate.erase("half_length_mm");
			candidate["radius_mm"] = 2000;
		}
	}
	if (change == "theme-slice" && value.get<bool>())
		candidate[path] = {0, 0, 0, 0};
	if (change == "widget-controller")
	{
		if (value.get<bool>())
		{
			candidate.erase("system");
			candidate["controller"] = "";
			candidate["channel"] = "";
		}
		else
		{
			candidate.erase("controller");
			candidate.erase("channel");
			candidate["binding"] =
			    candidate.at("kind") == "throttle" ? "engine.desired_speed" : "steering.desired_heading";
		}
	}
	if (change == "binding-source")
	{
		const auto mode = value.get<std::string>();
		candidate[path] = mode == "integer"     ? Json(0)
		                  : mode == "boolean"   ? Json(false)
		                  : mode == "text"      ? Json("")
		                  : mode == "reference" ? Json{{"type", "entity"}, {"id", 1}}
		                  : mode == "elapsed"   ? Json{{"elapsed", true}}
		                                        : Json{{mode, ""}};
	}
	if (change == "controller-policy")
		(void)workshop_custom_action(candidate, "controller.policy");
	if (change == "operating-action")
		workshop_controller_arguments(candidate);
	if (change == "record-subject")
	{
		candidate.erase("subject_parameter");
		candidate["subject"] = value == "entity" ? Json(1) : value;
	}
	if (change == "actor")
		candidate[path] = value == "entity" ? Json{{"entity", 1}} : Json{{value.get<std::string>(), ""}};
	if (change == "effect-kind")
		candidate = {{"kind", value}, {"target", owner.value("target", std::string("protected"))}};
	if (change == "effect-scope" && candidate.value("kind", std::string{}) == "remove" && value == "owner")
	{
		candidate["target"] = "owner";
		for (const char *field : {"grant", "effect", "equipment", "officer", "all_sources", "role", "delta_permille"})
			candidate.erase(field);
	}
	workshop_publish(owner, candidate);
	return owner.contains(path) ? owner.at(path) : Json();
}
void workshop_controller_arguments(Json &command)
{
	if (const auto *contract = operating_action(command.value("action", std::string{})))
	{
		const auto arguments = operating_arguments(contract->action);
		if (arguments.empty())
			command.erase("arguments");
		else if (!command.contains("arguments") || command.at("arguments").size() != arguments.size())
		{
			command["arguments"] = Json::array();
			for (const auto &field : arguments)
				command["arguments"].push_back(std::max<std::int64_t>(0, field.minimum));
		}
	}
}
void workshop_sprite_apply(WorkshopDocument &document, Json &render, const Json &sidecar, const Json &images,
                           bool engine_effect)
{
	auto catalog = document.libraries();
	const auto prefix = document.manifest().at("id").get<std::string>() + ":";
	for (const auto &[path, bytes] : document.pack().files)
		if (path.starts_with("assets/"))
			catalog.assets[prefix + path] = bytes;
	apply_sprite_sidecar(render, sidecar, images.get<std::map<std::string, std::string>>(), catalog, engine_effect);
	document.definitions()["schema"] = 3;
}
Json workshop_asset_choices(const WorkshopDocument &document, bool skybox)
{
	Json result = Json::array();
	auto add = [&](const std::string &id, const Bytes &bytes)
	{
		if (!id.ends_with(".png") || id.starts_with("console-assets:assets/radar/"))
			return;
		if (skybox)
		{
			static std::mutex mutex;
			static std::map<std::string, bool> cache;
			const auto hash = sha256(bytes);
			bool valid = false;
			{
				std::lock_guard lock(mutex);
				if (cache.contains(hash))
					valid = cache.at(hash);
				else
				{
					try
					{
						const auto image = decode_asset_image(bytes);
						valid = image.width == image.height * 2 || image.height == image.width * 6;
					}
					catch (const std::exception &)
					{
					}
					if (cache.size() >= 4096)
						cache.clear();
					cache[hash] = valid;
				}
			}
			if (!valid)
				return;
		}

		result.push_back(id);
	};
	const auto prefix = document.pack().manifest.at("id").get<std::string>() + ":";
	for (const auto &[path, bytes] : document.pack().files)
		if (path.starts_with("assets/"))
			add(prefix + path, bytes);
	for (const auto &[id, bytes] : document.libraries().assets)
		add(id, bytes);
	return result;
}
} // namespace phoenix
