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
void GameplayEditors::State::destruction_fields(Json &d)
{
	strings(d, "payloads");
	optional_string(d, "visual");
	optional_string(d, "sound");
	edit_registered(d, "gameplay-editors.combat.chance_permille");
	edit_registered(d, "gameplay-editors.combat.damage");
	edit_registered(d, "gameplay-editors.combat.radius_mm");
	edit_registered(d, "gameplay-editors.combat.shield_bypass_permille");
	edit_registered(d, "gameplay-editors.combat.growth_ticks");
	edit_registered(d, "gameplay-editors.combat.fade_ticks");
	edit_registered(d, "gameplay-editors.combat.size_mm");
	edit_registered(d, "gameplay-editors.combat.affects_missiles");
}

void GameplayEditors::State::projectile_fields(Json &p)
{
	strings(p, "payloads");
	edit_registered(p, "gameplay-editors.combat.visual");
	edit_shared_group(p, "gameplay-editors.combat.group.speed_mm_s");
	edit_registered(p, "gameplay-editors.combat.radius_mm.2");
	edit_registered(p, "gameplay-editors.combat.resistance_per_m");
	edit_registered(p, "gameplay-editors.combat.turn_mdeg_s");
	edit_registered(p, "gameplay-editors.combat.seeker_range_mm");
	edit_registered(p, "gameplay-editors.combat.tracer");
	edit_registered(p, "gameplay-editors.combat.guidance");
	optional_string(p, "trail");
	if (p.contains("destruction"))
	{
		ScopedId scope("projectile-destruction");
		destruction_fields(p["destruction"]);
		if (button("Remove destruction effect"))
			p.erase("destruction");
	}
	else if (button("Add destruction effect"))
		p["destruction"] = workshop_seed("gameplay-editors.combat.destruction");
}

void GameplayEditors::State::combat(WorkshopDocument &document)
{
	auto *ship = select_entity(document);
	if (!ship)
		return;
	edit_registered(*ship, "gameplay-editors.combat.durability");
	rule_bindings_fields(*ship, document);
	owner_properties_fields(*ship, document);
	edit_registered(*ship, "gameplay-editors.combat.half_height_mm");
	if (ship->contains("destruction"))
	{
		ScopedId scope("entity-destruction");
		destruction_fields((*ship)["destruction"]);
		if (button("Remove entity destruction effect"))
			ship->erase("destruction");
	}
	else if (button("Add entity destruction effect"))
		(*ship)["destruction"] = workshop_seed("gameplay-editors.combat.destruction.2");
	if (!ship->contains("combat"))
	{
		if (button("Add compartment combat layout"))
			(*ship)["combat"] = workshop_seed("gameplay-editors.combat.combat");
		return;
	}
	auto &c = (*ship)["combat"];
	edit_registered(c, "gameplay-editors.combat.repair_console");
	optional_string(c, "access_controller");
	edit_registered(c, "gameplay-editors.combat.collision_scale");
	edit_registered(c, "gameplay-editors.combat.collision_threshold_mm_s");
	records(
	    c, "decks", workshop_record("gameplay-editors.combat.decks"),
	    [&](Json &deck)
	    {
		    edit_registered(deck, "gameplay-editors.combat.id");
		    edit_registered(deck, "gameplay-editors.combat.weight");
		    records(deck, "compartments", workshop_record("gameplay-editors.combat.compartments"),
		            [&](Json &room)
		            {
			            edit_registered(room, "gameplay-editors.combat.id");
			            owner_properties_fields(room, document);
			            edit_shared_group(room, "gameplay-editors.combat.group.x_mm");
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
	records(c, "connections", workshop_record("gameplay-editors.combat.connections"),
	        [&](Json &edge)
	        {
		        edit_registered(edge, "gameplay-editors.combat.from");
		        edit_registered(edge, "gameplay-editors.combat.to");
		        edit_registered(edge, "gameplay-editors.combat.travel_ticks");
	        });
	if (!c.contains("destruction"))
		c["destruction"] = workshop_seed("gameplay-editors.combat.destruction.2");
	edit_registered(c, "gameplay-editors.combat.half_height_mm");
	auto &d = c["destruction"];
	destruction_fields(d);
	edit_registered(d, "gameplay-editors.combat.visual");
	edit_registered(d, "gameplay-editors.combat.sound");
	edit_registered(d, "gameplay-editors.combat.growth_ticks.2");
	edit_registered(d, "gameplay-editors.combat.fade_ticks.2");
	edit_registered(d, "gameplay-editors.combat.size_mm.2");
}
} // namespace phoenix
