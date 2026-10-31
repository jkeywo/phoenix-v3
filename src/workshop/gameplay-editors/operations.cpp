module;
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <set>
#include <string>
#include <utility>
#include <vector>

module phoenix.workshop.gameplay;
import phoenix.workshop.authoring_core;
import phoenix.content.documents;
import phoenix.workshop.authoring;
import phoenix.workshop.tree;
import phoenix.workshop.test_client;

namespace phoenix
{
#include "state.hpp"
void GameplayEditors::State::personnel(Json &ship, WorkshopDocument &document)
{
	ScopedTree panel("Personnel and departments");
	if (!panel)
		return;
	if (!ship.contains("personnel") && button("Add personnel roster"))
		ship["personnel"] = workshop_seed("gameplay-editors.operations.personnel");
	if (!ship.contains("personnel"))
		return;
	auto &p = ship["personnel"];
	if (!p.contains("casualties"))
		p["casualties"] = workshop_seed("gameplay-editors.operations.casualties");
	{
		ScopedTree policy("Casualty policy");
		if (policy)
		{
			auto &c = p["casualties"];
			edit_registered(c, "gameplay-editors.operations.significant_damage");
			edit_registered(c, "gameplay-editors.operations.heavy_below_permille");
			edit_registered(c, "gameplay-editors.operations.lethal");
			edit_registered(c, "gameplay-editors.operations.lethal_damage");
			edit_registered(c, "gameplay-editors.operations.stun_permille");
			edit_registered(c, "gameplay-editors.operations.minor_permille");
			edit_registered(c, "gameplay-editors.operations.severe_permille");
			edit_registered(c, "gameplay-editors.operations.killed_permille");
		}
	}
	records(p, "facilities", workshop_record("gameplay-editors.operations.facilities"),
	        [&](Json &f)
	        {
		        edit_registered(f, "gameplay-editors.operations.id");
		        edit_registered(f, "gameplay-editors.operations.compartment");
		        edit_registered(f, "gameplay-editors.operations.capacity");
		        edit_registered(f, "gameplay-editors.operations.recovery_ticks");
	        });
	records(p, "departments", workshop_record("gameplay-editors.operations.departments"),
	        [&](Json &d)
	        {
		        edit_registered(d, "gameplay-editors.operations.id");
		        edit_registered(d, "gameplay-editors.operations.label");
		        edit_registered(d, "gameplay-editors.operations.station");
	        });
	strings(p, "compartments");
	records(p, "connections", workshop_record("gameplay-editors.operations.connections"),
	        [&](Json &c)
	        {
		        edit_registered(c, "gameplay-editors.operations.from");
		        edit_registered(c, "gameplay-editors.operations.to");
		        edit_registered(c, "gameplay-editors.operations.ticks");
	        });
	records(p, "traits", workshop_record("gameplay-editors.operations.traits"),
	        [&](Json &t)
	        {
		        edit_registered(t, "gameplay-editors.operations.id");
		        edit_registered(t, "gameplay-editors.operations.label");
		        rule_bindings_fields(t, document);
	        });
	records(p, "duties", workshop_record("gameplay-editors.operations.duties"),
	        [&](Json &d)
	        {
		        edit_registered(d, "gameplay-editors.operations.id");
		        edit_registered(d, "gameplay-editors.operations.label");
		        choice(d, "department", ids(p["departments"]));
		        optional_string(d, "controller");
		        optional_string(d, "console");
		        optional_string(d, "team");
		        optional_string(d, "choir");
		        choice(d, "required_trait", ids(p["traits"]), true);
		        edit_registered(d, "gameplay-editors.operations.compartment");
		        auto &effects = d["bindings"];
		        if (effects.is_null())
			        effects = Json::object();
		        std::string old_target, new_target;
		        for (auto it = effects.begin(); it != effects.end();)
		        {
			        ScopedId identity(it.key().c_str());
			        auto target = it.key();
			        if (edit_value(label("System target").c_str(), target))
			        {
				        old_target = it.key();
				        new_target = target;
			        }
			        strings(effects, it.key().c_str());
			        if (button("Remove target binding"))
				        it = effects.erase(it);
			        else
				        ++it;
		        }
		        (void)workshop_custom_action(effects, "object.rename", {{"from", old_target}, {"to", new_target}});
		        if (button("Add target binding"))
			        (void)workshop_custom_action(effects, "object.add", {{"prefix", "system"}, {"type", "array"}});
	        });
	records(p, "officers", workshop_record("gameplay-editors.operations.officers"),
	        [&](Json &o)
	        {
		        edit_registered(o, "gameplay-editors.operations.id");
		        edit_registered(o, "gameplay-editors.operations.name");
		        optional_string(o, "portrait");
		        strings(o, "abilities");
		        rule_bindings_fields(o, document, true);
		        owner_properties_fields(o, document);
		        choice(o, "department", ids(p["departments"]));
		        choice(o, "duty", ids(p["duties"]), true);
		        edit_registered(o, "gameplay-editors.operations.compartment");
		        strings(o, "traits");
		        edit_registered(o, "gameplay-editors.operations.condition");
	        });
}
} // namespace phoenix
