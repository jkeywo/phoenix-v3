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
		ship["personnel"] = {{"departments", Json::array()},  {"duties", Json::array()},
		                     {"officers", Json::array()},     {"traits", Json::array()},
		                     {"compartments", Json::array()}, {"connections", Json::array()}};
	if (!ship.contains("personnel"))
		return;
	auto &p = ship["personnel"];
	if (!p.contains("casualties"))
		p["casualties"] = Json::object();
	{
		ScopedTree policy("Casualty policy");
		if (policy)
		{
			auto &c = p["casualties"];
			integer(c, "significant_damage", 20);
			integer(c, "heavy_below_permille", 300);
			boolean(c, "lethal", false);
			integer(c, "lethal_damage", 80);
			integer(c, "stun_permille", 150);
			integer(c, "minor_permille", 100);
			integer(c, "severe_permille", 30);
			integer(c, "killed_permille", 10);
		}
	}
	records(p, "facilities", {{"id", "sickbay"}, {"compartment", ""}, {"capacity", 1}, {"recovery_ticks", 1200}},
	        [&](Json &f)
	        {
		        string(f, "id");
		        string(f, "compartment");
		        integer(f, "capacity", 1);
		        integer(f, "recovery_ticks", 1200);
	        });
	records(p, "departments", {{"id", "department"}, {"label", "Department"}, {"station", ""}},
	        [&](Json &d)
	        {
		        string(d, "id");
		        string(d, "label");
		        string(d, "station");
	        });
	strings(p, "compartments");
	records(p, "connections", {{"from", ""}, {"to", ""}, {"ticks", 40}},
	        [&](Json &c)
	        {
		        string(c, "from");
		        string(c, "to");
		        integer(c, "ticks", 40);
	        });
	records(p, "traits", {{"id", "trait"}, {"label", "Expertise"}, {"rules", Json::array()}},
	        [&](Json &t)
	        {
		        string(t, "id");
		        string(t, "label");
		        rule_bindings_fields(t, document);
	        });
	records(p, "duties",
	        {{"id", "duty"},
	         {"label", "Duty"},
	         {"department", ""},
	         {"controller", ""},
	         {"compartment", ""},
	         {"bindings", Json::object()}},
	        [&](Json &d)
	        {
		        string(d, "id");
		        string(d, "label");
		        choice(d, "department", ids(p["departments"]));
		        optional_string(d, "controller");
		        optional_string(d, "console");
		        optional_string(d, "team");
		        optional_string(d, "choir");
		        choice(d, "required_trait", ids(p["traits"]), true);
		        string(d, "compartment");
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
		        if (!old_target.empty() && !new_target.empty() && !effects.contains(new_target) &&
		            effects.contains(old_target))
		        {
			        effects[new_target] = effects.at(old_target);
			        effects.erase(old_target);
		        }
		        if (button("Add target binding"))
			        effects[fresh(effects, "system")] = Json::array();
	        });
	records(p, "officers",
	        {{"id", "officer"},
	         {"name", "New officer"},
	         {"department", ""},
	         {"compartment", ""},
	         {"traits", Json::array()}},
	        [&](Json &o)
	        {
		        string(o, "id");
		        string(o, "name");
		        optional_string(o, "portrait");
		        strings(o, "abilities");
		        rule_bindings_fields(o, document, true);
		        owner_properties_fields(o, document);
		        choice(o, "department", ids(p["departments"]));
		        choice(o, "duty", ids(p["duties"]), true);
		        string(o, "compartment");
		        strings(o, "traits");
		        choice(o, "condition", {"healthy", "minor", "severe"});
	        });
}
} // namespace phoenix
