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
void GameplayEditors::State::destruction_fields(Json &d)
{
	strings(d, "payloads");
	optional_string(d, "visual");
	optional_string(d, "sound");
	integer(d, "chance_permille", 1000);
	integer(d, "damage");
	integer(d, "radius_mm");
	integer(d, "shield_bypass_permille");
	integer(d, "growth_ticks", 1);
	integer(d, "fade_ticks", 10);
	integer(d, "size_mm", 10000);
	boolean(d, "affects_missiles", true);
}

void GameplayEditors::State::projectile_fields(Json &p)
{
	strings(p, "payloads");
	string(p, "visual");
	for (const auto *key :
	     {"speed_mm_s", "lifetime_ticks", "penetration", "damage", "damage_per_m", "spread_mdeg",
	      "shield_bypass_permille", "durability", "arc_height_mm", "chaos_mdeg", "blast_radius_mm", "fuse_radius_mm"})
		integer(p, key);
	integer(p, "radius_mm", 100);
	integer(p, "resistance_per_m", 1);
	integer(p, "turn_mdeg_s", 90000);
	integer(p, "seeker_range_mm", 1000000);
	boolean(p, "tracer");
	choice(p, "guidance", {"none", "homing", "chaotic"});
	optional_string(p, "trail");
	if (p.contains("destruction"))
	{
		ScopedId scope("projectile-destruction");
		destruction_fields(p["destruction"]);
		if (button("Remove destruction effect"))
			p.erase("destruction");
	}
	else if (button("Add destruction effect"))
		p["destruction"] = {{"chance_permille", 1000}, {"affects_missiles", false}};
}

void GameplayEditors::State::combat(WorkshopDocument &document)
{
	auto *ship = select_entity(document);
	if (!ship)
		return;
	integer(*ship, "durability");
	rule_bindings_fields(*ship, document);
	owner_properties_fields(*ship, document);
	integer(*ship, "half_height_mm", 5000);
	if (ship->contains("destruction"))
	{
		ScopedId scope("entity-destruction");
		destruction_fields((*ship)["destruction"]);
		if (button("Remove entity destruction effect"))
			ship->erase("destruction");
	}
	else if (button("Add entity destruction effect"))
		(*ship)["destruction"] = Json::object();
	if (!ship->contains("combat"))
	{
		if (button("Add compartment combat layout"))
			(*ship)["combat"] = {
			    {"decks", Json::array()}, {"connections", Json::array()}, {"destruction", Json::object()}};
		return;
	}
	auto &c = (*ship)["combat"];
	string(c, "repair_console");
	optional_string(c, "access_controller");
	integer(c, "collision_scale", 1);
	integer(c, "collision_threshold_mm_s", 1000);
	records(
	    c, "decks", {{"id", "deck"}, {"weight", 1}, {"compartments", Json::array()}},
	    [&](Json &deck)
	    {
		    string(deck, "id");
		    integer(deck, "weight", 1);
		    records(deck, "compartments",
		            {{"id", "room"},
		             {"x_mm", 0},
		             {"y_mm", 0},
		             {"half_width_mm", 1000},
		             {"half_length_mm", 1000},
		             {"hp", 100},
		             {"resistance_per_m", 10},
		             {"recovery_hp", 20}},
		            [&](Json &room)
		            {
			            string(room, "id");
			            owner_properties_fields(room, document);
			            for (const auto *key : {"x_mm", "y_mm", "half_width_mm", "half_length_mm", "hp",
			                                    "resistance_per_m", "recovery_hp"})
				            integer(room, key);
		            });
		    const auto origin = ImGui::GetCursorScreenPos();
		    const float size = 280;
		    auto *draw = ImGui::GetWindowDrawList();
		    std::int64_t extent = 1;
		    for (const auto &room : deck.value("compartments", Json::array()))
			    extent = std::max(
			        {extent,
			         std::abs(room.value("x_mm", std::int64_t{0})) + room.value("half_width_mm", std::int64_t{1}),
			         std::abs(room.value("y_mm", std::int64_t{0})) + room.value("half_length_mm", std::int64_t{1})});
		    const float scale = size / (extent * 2.f);
		    for (const auto &room : deck.value("compartments", Json::array()))
		    {
			    const float x = room.value("x_mm", 0.f), y = room.value("y_mm", 0.f),
			                hw = room.value("half_width_mm", 1.f), hl = room.value("half_length_mm", 1.f);
			    const ImVec2 a{origin.x + size / 2 + (y - hl) * scale, origin.y + size / 2 - (x + hw) * scale},
			        b{origin.x + size / 2 + (y + hl) * scale, origin.y + size / 2 - (x - hw) * scale};
			    draw->AddRectFilled(a, b, IM_COL32(20, 65, 65, 255));
			    draw->AddRect(a, b, IM_COL32(210, 200, 160, 255));
			    draw->AddText(a, IM_COL32(240, 230, 190, 255), room.value("id", std::string{}).c_str());
		    }
		    ImGui::Dummy({size, size});
	    });
	records(c, "connections", {{"from", ""}, {"to", ""}, {"travel_ticks", 20}},
	        [&](Json &edge)
	        {
		        string(edge, "from");
		        string(edge, "to");
		        integer(edge, "travel_ticks", 20);
	        });
	if (!c.contains("destruction"))
		c["destruction"] = Json::object();
	integer(c, "half_height_mm", 5000);
	auto &d = c["destruction"];
	destruction_fields(d);
	string(d, "visual");
	string(d, "sound");
	integer(d, "growth_ticks", 10);
	integer(d, "fade_ticks", 20);
	integer(d, "size_mm", 100000);
}
} // namespace phoenix
