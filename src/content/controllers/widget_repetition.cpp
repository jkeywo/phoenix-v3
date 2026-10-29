module;
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.content.controllers;

namespace phoenix
{
// Authoring expansion only: execution uses ordinary referenced state trees and
// their existing independent continuations. A repeated widget remains one owner.
void expand_control_widget_repetition(Catalog &catalog)
{
	for (auto &[entity_id, entity] : catalog.entities)
	{
		if (!entity.contains("control_layout"))
			continue;
		for (auto &instance : entity["control_layout"]["controllers"])
		{
			if (!instance.contains("widget"))
				continue;
			const auto original = instance.at("widget").get<std::string>();
			const auto source = catalog.control_widgets.at(original);
			if (!source.contains("repeat"))
				continue;
			(void)bind_control_widget(compile_control_widget(catalog, original), instance);
			const auto fail = [&](bool valid, const std::string &reason)
			{
				if (!valid)
					throw std::runtime_error("control_widgets." + original + ": " + reason);
			};
			const auto role = source.at("repeat").get<std::string>();
			fail(source.at("equipment").contains(role) && source.at("equipment").at(role).value("multiple", false),
			     "repeat must name a multiple equipment role");
			fail(!source.contains("tree") && source.contains("member_policy") && source.at("member_policy").is_object(),
			     "repeated widget needs an inline member_policy instead of tree");
			const auto attached = instance.at("equipment").at(role);
			fail(attached.is_array() && !attached.empty() && attached.size() <= 64,
			     "repeat needs 1..64 attached members");
			fail(attached.size() * source.at("commands").size() <= 256, "expanded widget exceeds 256 commands");
			const auto identity =
			    "phoenix.widget." + sha256(json_bytes(Json::array({source, instance.at("equipment")}))).substr(0, 32);
			Json definition = source;
			definition["id"] = identity;
			definition.erase("repeat");
			definition.erase("member_policy");
			definition["equipment"].erase(role);
			definition["commands"] = Json::array();
			definition["data"] = Json::object();
			definition["operations"] = source.value("operations", Json::object());
			definition["part_bindings"] = source.value("part_bindings", Json::object());
			Json bindings = instance.at("equipment");
			bindings.erase(role);
			std::map<std::string, std::vector<std::string>> channels, data;
			Json tree{{"id", identity + ".policy"},
			          {"context", "controller"},
			          {"initial", "operate"},
			          {"states", Json::array({{{"id", "operate"}, {"running", Json::array()}}})},
			          {"command_bindings", Json::array()},
			          {"system_bindings", Json::array()}};
			for (std::size_t index = 0; index < attached.size(); ++index)
			{
				const auto prefix = "m" + std::to_string(index) + ".";
				const auto member_role = "m" + std::to_string(index) + "_" + role;
				auto specification = source.at("equipment").at(role);
				specification["multiple"] = false;
				definition["equipment"][member_role] = specification;
				bindings[member_role] = attached[index];
				std::map<std::string, std::string> names;
				for (const auto &command : source.at("commands"))
					names[command.at("id")] = prefix + command.at("id").get<std::string>();
				std::map<std::string, std::string> observations;
				for (const auto &binding : source.at("member_policy").value("system_bindings", Json::array()))
					observations[binding.at("name")] = prefix + binding.at("name").get<std::string>();
				std::function<Json(const Json &)> rebind = [&](const Json &value) -> Json
				{
					if (value.is_string())
					{
						const auto text = value.get<std::string>();
						if (text == "$" + role)
							return "$" + member_role;
						if (text.starts_with("write.") && names.contains(text.substr(6)))
							return "write." + names.at(text.substr(6));
						for (const auto &[channel, expanded] : names)
							if (text == "command." + channel || text.starts_with("command." + channel + "."))
								return "command." + expanded + text.substr(8 + channel.size());
						for (const auto *kind : {"system", "weapon"})
							if (text.starts_with(std::string(kind) + "." + role + "."))
								return std::string(kind) + "." + member_role +
								       text.substr(std::string(kind).size() + role.size() + 1);
						return value;
					}
					if (!value.is_structured())
						return value;
					Json result = value.is_array() ? Json::array() : Json::object();
					for (auto item = value.begin(); item != value.end(); ++item)
						if (value.is_array())
							result.push_back(rebind(item.value()));
						else if (item.key() == "channel" && item.value().is_string() && names.contains(item.value()))
							result[item.key()] = names.at(item.value());
						else if (item.key() == "observation" && item.value().is_string() &&
						         observations.contains(item.value()))
							result[item.key()] = observations.at(item.value());
						else
							result[item.key()] = rebind(item.value());
					return result;
				};
				for (auto command : source.at("commands"))
				{
					const auto name = command.at("id").get<std::string>();
					command = rebind(command);
					command["id"] = names.at(name);
					channels[name].push_back(names.at(name));
					definition["commands"].push_back(command);
				}
				const auto member_data = source.value("data", Json::object());
				for (const auto &[name, datum] : member_data.items())
				{
					const auto expanded = prefix + name;
					data[name].push_back(expanded);
					definition["data"][expanded] = rebind(datum);
					definition["data"][expanded]["member"] = attached[index];
				}
				auto policy = rebind(source.at("member_policy"));
				fail(!policy.contains("action_bindings") && !policy.contains("command_reads"),
				     "member_policy cannot bind cross-controller actions or command reads");
				policy["id"] = identity + "." + std::to_string(index);
				policy["context"] = "controller";
				policy["system_bindings"] = policy.value("system_bindings", Json::array());
				for (auto &binding : policy["system_bindings"])
				{
					binding["name"] = observations.at(binding.at("name").get<std::string>());
					const auto equipment = binding.at("system").get<std::string>();
					if (equipment.starts_with("$"))
						binding["system"] = bindings.at(equipment.substr(1));
				}
				for (const auto *field : {"command_bindings", "system_bindings"})
					for (const auto &binding : policy.value(field, Json::array()))
						tree[field].push_back(binding);
				tree["states"][0]["running"].push_back({{"call", policy.at("id")}});
				catalog.trees[policy.at("id").get<std::string>()] = policy;
			}
			const auto expand = [&](const Json &members, const auto &mapping)
			{
				Json result = Json::array();
				for (const auto &member : members)
					if (const auto found = mapping.find(member.template get<std::string>()); found != mapping.end())
						for (const auto &name : found->second)
							result.push_back(name);
					else
						result.push_back(member);
				return result;
			};
			for (auto &level : definition["levels"])
			{
				level["human"] = expand(level.at("human"), channels);
				for (const auto *field : {"data", "replaces"})
					if (level.contains(field))
					{
						auto expanded = Json::object();
						const auto &mapping = std::string(field) == "data" ? data : channels;
						for (const auto &[name, value] : level.at(field).items())
							if (mapping.contains(name))
								for (const auto &member : mapping.at(name))
								{
									if (std::string(field) == "data")
										expanded[member] = value;
									else
									{
										auto replacements = expand(value, channels);
										const auto prefix = member.substr(0, member.find('.') + 1);
										std::erase_if(replacements.get_ref<Json::array_t &>(),
										              [&](const auto &replacement)
										              {
											              return !replacement.template get<std::string>().starts_with(
											                  prefix);
										              });
										expanded[member] = replacements;
									}
								}
							else
								expanded[name] = value;
						level[field] = expanded;
					}
			}
			for (auto &part : definition["part_bindings"])
			{
				for (const auto *field : {"commands", "ranges", "data"})
					if (part.contains(field))
						part[field] = expand(part.at(field), std::string(field) == "data" ? data : channels);
				if (part.contains("values"))
				{
					Json values = Json::object();
					for (const auto &[name, value] : part.at("values").items())
						for (const auto &member : channels.at(name))
							values[member] = value;
					part["values"] = values;
				}
			}
			for (auto &subjects : definition["operations"])
			{
				Json expanded = Json::array();
				for (const auto &subject : subjects)
					if (subject == "$" + role)
						for (std::size_t index = 0; index < attached.size(); ++index)
							expanded.push_back("$m" + std::to_string(index) + "_" + role);
					else
						expanded.push_back(subject);
				subjects = expanded;
			}
			if (instance.contains("data_placement"))
				for (auto &star : instance["data_placement"])
				{
					Json expanded = Json::object();
					for (const auto &[name, value] : star.items())
						for (const auto &member : data.at(name))
							expanded[member] = value;
					star = expanded;
				}
			definition["tree"] = tree.at("id");
			catalog.trees[tree.at("id").get<std::string>()] = tree;
			catalog.control_widgets[identity] = definition;
			instance["widget"] = identity;
			instance["equipment"] = bindings;
		}
	}
}
} // namespace phoenix
