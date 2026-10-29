#pragma once
namespace phoenix::test
{
// Concise synthetic builders are expanded to the current production rule schema.
// Production loading and execution never accept these old builder shorthands.
inline void materialise_fixture_rules(Json &authored)
{
	if (!authored.contains("effects"))
		authored["effects"] = Json::array();
	Json generated = Json::array();
	std::size_t sequence = 0;
	const auto rule = [&](Json definition)
	{
		const auto id = "fixture.rule." + std::to_string(++sequence);
		definition["id"] = id;
		definition["kind"] = "rule";
		generated.push_back(std::move(definition));
		return id;
	};
	const auto group = [&](const Json &operations)
	{
		const auto id = "fixture.rule-group." + std::to_string(++sequence);
		auto definition = operations;
		definition["id"] = id;
		definition["kind"] = "operations";
		generated.push_back(std::move(definition));
		return id;
	};
	const auto hit_group = [&](const std::string &reference, const std::string &owner_effect = std::string{})
	{
		for (const auto &candidate : authored["effects"])
			if (candidate.at("id") == reference)
			{
				auto adapted = candidate;
				adapted.erase("id");
				adapted.erase("kind");
				for (auto &operation : adapted["operations"])
				{
					bool removes_owner = false;
					if (!owner_effect.empty() && operation.value("kind", std::string{}) == "remove" &&
					    (operation.value("target", std::string("attacker")) == "protected" ||
					     operation.value("target", std::string("attacker")) == "subject") &&
					    !operation.value("grant", std::string{}).empty() && !operation.contains("effect") &&
					    !operation.contains("all_sources") && !operation.contains("equipment") &&
					    !operation.contains("officer"))
						for (const auto &parent : authored["effects"])
							for (const auto &apply : parent.value("operations", Json::array()))
								removes_owner =
								    removes_owner ||
								    (apply.value("kind", std::string{}) == "apply" &&
								     apply.value("effect", std::string{}) == owner_effect &&
								     apply.value("grant", std::string{}) == operation.at("grant").get<std::string>());
					if (removes_owner)
						operation = {{"kind", "remove"}, {"target", "owner"}, {"scope", "owner"}};
					else if (operation.value("target", std::string("attacker")) == "protected")
						operation["target"] = "subject";
				}
				for (auto &cost : adapted["costs"])
					if (cost.value("payer", std::string("target")) == "protected")
						cost["payer"] = "subject";
				if (adapted["costs"].is_null())
					adapted.erase("costs");
				return group(adapted);
			}
		throw std::runtime_error("fixture native hit group missing: " + reference);
	};
	std::function<void(Json &)> personnel = [&](Json &value)
	{
		if (value.is_object())
		{
			if (value.contains("personnel") && value.at("personnel").is_object())
				for (auto &trait : value["personnel"]["traits"])
					if (trait.contains("effects"))
					{
						trait["rules"] = Json::array();
						std::size_t local = 0;
						for (const auto &effect : trait["effects"])
						{
							const auto effect_group = group({{"operations", Json::array({{{"kind", "apply"},
							                                                              {"target", "target"},
							                                                              {"equipment", "$target"},
							                                                              {"role", "$role"},
							                                                              {"effect", effect},
							                                                              {"grant", "buff"}}})}});
							const auto id =
							    rule({{"trigger", "maintain"}, {"phase", "maintained"}, {"effect", effect_group}});
							trait["rules"].push_back({{"id", "buff-" + std::to_string(local++)}, {"rule", id}});
						}
						trait.erase("effects");
					}
			for (auto &[key, child] : value.items())
				if (key != "effects")
					personnel(child);
		}
		else if (value.is_array())
			for (auto &child : value)
				personnel(child);
	};
	personnel(authored);
	for (auto &effect : authored["effects"])
	{
		const auto kind = effect.value("kind", std::string{});
		if (kind == "shield")
			for (auto &[name, contribution] : effect["contributions"].items())
			{
				if (!contribution.contains("rules"))
					contribution["rules"] = Json::array();
				auto &bindings = contribution["rules"];
				if (contribution.contains("pre_damage_permille"))
				{
					const auto id = rule({{"trigger", "pre_shield_damage"},
					                      {"damage_permille",
					                       {{"value", contribution.at("pre_damage_permille")}, {"unit", "permille"}}}});
					bindings.push_back({{"id", "pre"}, {"rule", id}});
					contribution.erase("pre_damage_permille");
				}
				std::size_t local = 0;
				for (const auto &reaction : contribution.value("reactions", Json::array()))
				{
					const auto effect_group = group(reaction);
					const auto id = rule({{"trigger", "post_shield_damage"}, {"effect", effect_group}});
					bindings.push_back({{"id", "reaction-" + std::to_string(local++)}, {"rule", id}});
				}
				contribution.erase("reactions");
				if (contribution.contains("post_damage"))
				{
					const auto effect_group =
					    group({{"operations", Json::array({{{"kind", "damage"},
					                                        {"target", "attacker"},
					                                        {"damage", contribution.at("post_damage")}}})}});
					const auto id = rule({{"trigger", "post_shield_damage"}, {"effect", effect_group}});
					bindings.push_back({{"id", "post"}, {"rule", id}});
					contribution.erase("post_damage");
				}
			}
		else if (kind == "payload" && effect.contains("bindings"))
		{
			effect["rules"] = Json::array();
			for (const auto &binding : effect["bindings"])
			{
				const auto outcome = binding.at("outcome").get<std::string>();
				Json definition = {{"trigger", "post_" + outcome + "_damage"},
				                   {"effect", hit_group(binding.at("effect").get<std::string>())}};
				if (binding.value("positive_damage", true))
					definition["when"] = {
					    {"op", "gt"},
					    {"args", Json::array({{{"fact", outcome == "shield" ? "event.absorbed" : "event.inflicted"}},
					                          {{"value", 0}, {"unit", "damage"}}})}};
				const auto id = rule(std::move(definition));
				effect["rules"].push_back({{"id", binding.at("id")}, {"rule", id}});
			}
			effect.erase("bindings");
		}
		else if (kind == "periodic" && effect.contains("effect"))
		{
			const auto id = rule(
			    {{"trigger", "tick"},
			     {"phase", "periodic"},
			     {"period_ticks", effect.value("period_ticks", 1)},
			     {"effect", hit_group(effect.at("effect").get<std::string>(), effect.at("id").get<std::string>())}});
			effect["rules"] = Json::array({{{"id", "tick"}, {"rule", id}}});
			effect.erase("effect");
			effect.erase("period_ticks");
		}
	}
	for (auto &definition : generated)
		authored["effects"].push_back(std::move(definition));
}
} // namespace phoenix::test
