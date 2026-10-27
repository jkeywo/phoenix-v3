#pragma once
namespace phoenix::test
{
// Synthetic fixtures author the same explicit native process owners as packs.
// This helper is test construction only; runtime never synthesises a process.
inline void materialise_fixture_processes(Json &authored)
{
	std::map<std::string, std::string> kinds;
	for (const auto &effect : authored.value("effects", Json::array()))
		if (effect.value("kind", std::string{}) == "process")
			kinds.emplace(effect.at("id").get<std::string>(), effect.value("native", std::string("effects")));
	std::set<std::string> defaults;
	const auto remove_generated = [&](Json &owner)
	{
		if (!owner.contains("processes") || !owner["processes"].is_object())
			return;
		auto &bindings = owner["processes"];
		for (auto binding = bindings.begin(); binding != bindings.end();)
		{
			const auto definition = binding.value().value("process", std::string{});
			const bool generated = definition.starts_with("fixture.process.") &&
			                       binding.key().starts_with("fixture-" + definition.substr(16));
			if (generated)
				binding = bindings.erase(binding);
			else
				++binding;
		}
	};
	const auto bind = [&](Json &owner, const std::string &family, const std::string &native,
	                      bool explicit_native = false, bool multiple = false)
	{
		if (native.empty())
			return;
		const auto bindings = owner.value("processes", Json::object());
		for (const auto &[slot, binding] : bindings.items())
		{
			const auto type = kinds.find(binding.value("process", std::string{}));
			if (type != kinds.end() && type->second == family &&
			    (!binding.contains("native_id") || binding.at("native_id") == native))
				return;
		}
		const auto definition = "fixture.process." + family;
		auto slot = "fixture-" + family;
		if (family == "processor" || multiple)
			slot += "-" + (native.size() <= 90 ? native : sha256(json_bytes(native)).substr(0, 32));
		if (owner.contains("processes") && owner["processes"].contains(slot))
			throw std::runtime_error("Synthetic process slot conflict: " + slot);
		owner["processes"][slot] = {{"process", definition}};
		if (explicit_native)
			owner["processes"][slot]["native_id"] = native;
		defaults.insert(family);
	};
	const auto container = [&](Json &entity, bool equipment_template = false)
	{
		remove_generated(entity);
		if (entity.contains("personnel") && entity["personnel"].is_object())
			for (const auto &facility : entity["personnel"].value("facilities", Json::array()))
				bind(entity, "recovery", facility.at("id").get<std::string>(), true, true);
		if (!entity.contains("systems") || !entity["systems"].is_object())
			return;
		auto &systems = entity["systems"];
		for (auto &system : systems)
			remove_generated(system);
		for (auto &[id, system] : systems.items())
		{
			const auto kind = system.value("kind", id);
			if (kind == "generator")
				bind(system, "generation", system.value("generator", std::string{}));
			if (kind == "coolant")
				bind(system, "cooling", system.value("cooling_link", std::string{}));
			if (kind == "engine" || kind == "steering" || kind == "lateral" || kind == "boost" || kind == "impulse")
				bind(system, "propulsion", id);
			if (system.contains("processor"))
				bind(system, "processor", id);
			if (kind == "team" || kind == "repair_team")
			{
				const auto capabilities = system.value("capabilities", std::set<std::string>{"repair"});
				if (capabilities.contains("repair"))
					bind(system, "repair", id);
				if (capabilities.contains("medical"))
					bind(system, "medical", id);
			}
			if (kind == "ftl")
				bind(system, "ftl", id);
			if (kind == "shuttle_bay")
			{
				if (!system.value("power_store", std::string{}).empty() && system.value("charge_per_second", 0) > 0)
					bind(system, "servicing_energy", id);
				const auto ammunition = system.value("ammunition", Json::array());
				for (std::size_t i = 0; i < ammunition.size(); ++i)
					if (ammunition.at(i).value("rounds_per_second", 0) > 0)
						bind(system, "servicing_ammunition",
						     (equipment_template ? std::string{} : id + ".") + "ammo-" + std::to_string(i), true, true);
			}
		}
		std::map<std::string, std::size_t> processor_owners;
		for (const auto &processor : entity.value("processors", Json::array()))
			++processor_owners[processor.value("equipment", std::string{})];
		for (const auto &processor : entity.value("processors", Json::array()))
		{
			const auto equipment = processor.value("equipment", std::string{});
			if (systems.contains(equipment))
				bind(systems[equipment], "processor", processor.at("id").get<std::string>(),
				     processor_owners[equipment] > 1);
		}
	};
	for (const auto *kind : {"entities", "assemblies"})
		if (authored.contains(kind))
			for (auto &row : authored[kind])
				container(row);
	if (authored.contains("equipment"))
		for (auto &equipment : authored["equipment"])
		{
			Json wrapper{{"systems", {{"placement", equipment}}}};
			container(wrapper, true);
			equipment = std::move(wrapper["systems"]["placement"]);
		}
	if (!defaults.empty() && !authored.contains("effects"))
		authored["effects"] = Json::array();
	for (const auto &family : defaults)
	{
		const auto id = "fixture.process." + family;
		if (kinds.contains(id))
		{
			if (kinds.at(id) != family)
				throw std::runtime_error("Synthetic native process definition conflict: " + id);
			continue;
		}
		for (const auto &effect : authored["effects"])
			if (effect.at("id") == id)
				throw std::runtime_error("Synthetic process definition identity conflict: " + id);
		authored["effects"].push_back(
		    {{"id", id}, {"kind", "process"}, {"label", "Synthetic " + family}, {"native", family}});
	}
}
} // namespace phoenix::test
