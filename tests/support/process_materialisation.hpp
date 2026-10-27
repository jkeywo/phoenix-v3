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
	const auto container = [&](Json &entity)
	{
		if (!entity.contains("systems") || !entity["systems"].is_object())
			return;
		auto &systems = entity["systems"];
		// A fixture may be materialised, edited, then loaded again. Rebuild only
		// helper-owned bindings so removed machinery cannot leave stale owners.
		for (auto &system : systems)
			if (system.contains("processes") && system["processes"].is_object())
			{
				auto &bindings = system["processes"];
				for (auto binding = bindings.begin(); binding != bindings.end();)
				{
					const auto definition = binding.value().value("process", std::string{});
					bool generated = false;
					for (const auto *family : {"generation", "cooling", "propulsion", "processor"})
					{
						const auto slot = std::string("fixture-") + family;
						generated |= definition == std::string("fixture.process.") + family &&
						             (binding.key() == slot ||
						              (std::string(family) == "processor" && binding.key().starts_with(slot + "-")));
					}
					if (generated)
						binding = bindings.erase(binding);
					else
						++binding;
				}
			}
		const auto bind = [&](const std::string &equipment, const std::string &family, const std::string &native,
		                      bool explicit_native = false)
		{
			if (native.empty() || !systems.contains(equipment))
				return;
			auto &system = systems[equipment];
			const auto bindings = system.value("processes", Json::object());
			for (const auto &[slot, binding] : bindings.items())
			{
				const auto type = kinds.find(binding.value("process", std::string{}));
				if (type != kinds.end() && type->second == family &&
				    (!binding.contains("native_id") || binding.at("native_id") == native))
					return;
			}
			const auto definition = "fixture.process." + family;
			auto slot = "fixture-" + family;
			if (family == "processor")
				slot += "-" + (native.size() <= 100 ? native : sha256(json_bytes(native)).substr(0, 32));
			if (system.contains("processes") && system["processes"].contains(slot))
				throw std::runtime_error("Synthetic process slot conflict: " + equipment + "." + slot);
			system["processes"][slot] = {{"process", definition}};
			if (explicit_native)
				system["processes"][slot]["native_id"] = native;
			defaults.insert(family);
		};
		for (const auto &[id, system] : systems.items())
		{
			const auto kind = system.value("kind", id);
			if (kind == "generator")
				bind(id, "generation", system.value("generator", std::string{}));
			if (kind == "coolant")
				bind(id, "cooling", system.value("cooling_link", std::string{}));
			if (kind == "engine" || kind == "steering" || kind == "lateral" || kind == "boost" || kind == "impulse")
				bind(id, "propulsion", id);
			if (system.contains("processor"))
				bind(id, "processor", id);
		}
		std::map<std::string, std::size_t> processor_owners;
		for (const auto &processor : entity.value("processors", Json::array()))
			++processor_owners[processor.value("equipment", std::string{})];
		for (const auto &processor : entity.value("processors", Json::array()))
		{
			const auto equipment = processor.value("equipment", std::string{});
			bind(equipment, "processor", processor.at("id").get<std::string>(), processor_owners[equipment] > 1);
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
			container(wrapper);
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
