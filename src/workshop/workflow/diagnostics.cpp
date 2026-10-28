module;
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <raylib.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

module phoenix.workshop.workflow;
import phoenix.content.image_codec;
import phoenix.content.ui;
import phoenix.workshop.map;
import phoenix.workshop.layout;
import phoenix.workshop.asset_preview;
import phoenix.workshop.material_preview;
import phoenix.workshop.gameplay;
import phoenix.ui.preview;

namespace phoenix
{
#include "state.hpp"
bool WorkshopWorkspace::State::probe_workshop_layout(unsigned stage)
{
	auto &consoles = document.definitions()["consoles"];
	if (consoles.empty())
		return false;
	this->console = 0;
	selection.kind = "Console";
	if (stage == 0)
	{
		consoles[0]["layout"] = convert_console_layout(consoles[0]);
		layout_editor.preview().width = 1280;
		layout_editor.preview().height = 720;
		layout_editor.focus();
	}
	if (stage == 1)
	{
		auto &layout = consoles[0]["layout"];
		layout["root"]["gap"] = 20;
		document.verify();
		const auto bytes = document.export_pack();
		document.load_pack(bytes);
		this->console = 0;
		selection.kind = "Console";
		if (document.definitions().at("consoles").at(0).at("layout").at("root").at("gap") != 20)
			return false;
	}
	if (stage == 2)
	{
		layout_editor.preview().width = 390;
		layout_editor.preview().height = 844;
	}
	if (stage == 3)
	{
		layout_editor.preview().text_scale = 150;
		auto &console = document.definitions()["consoles"][0];
		console["theme_overrides"] = {{"tokens", {{"panel", "#16303b"}, {"text", "#ffffff"}}}};
		AssetImage image{12, 12, Bytes(12 * 12 * 4, 255)};
		for (int y = 0; y < 12; ++y)
			for (int x = 0; x < 12; ++x)
			{
				const bool edge = x == 0 || y == 0 || x == 11 || y == 11;
				const int pixel = (y * 12 + x) * 4;
				image.rgba[pixel] = edge ? 180 : 20;
				image.rgba[pixel + 1] = edge ? 140 : 44;
				image.rgba[pixel + 2] = edge ? 60 : 55;
			}
		console["theme_overrides"]["assets"] = {
		    {"button", document.import_image("layout-probe.png", source_asset_image(image))}};
		console["theme_overrides"]["slices"] = {{"button", {3, 3, 3, 3}}};
	}
	if (stage >= 4)
	{
		if (!layout_editor.error().empty())
			throw std::runtime_error(layout_editor.error());
		document.verify();
		layout_editor.preview().text_scale = 100;
		auto &console = document.definitions()["consoles"][0];
		if (stage == 4)
		{
			console["theme_overrides"].erase("assets");
			console["theme_overrides"].erase("slices");
			console["widgets"] = Json::array({{{"id", "conversations"},
			                                   {"kind", "conversations"},
			                                   {"label", "Conversations"},
			                                   {"binding", "ship.conversations"},
			                                   {"x", 0},
			                                   {"y", 0},
			                                   {"width", 1000},
			                                   {"height", 1000}}});
			console["layout"] = {
			    {"schema", 1},
			    {"root",
			     {{"id", "conversation"},
			      {"kind", "row"},
			      {"children",
			       Json::array(
			           {{{"id", "directory"},
			             {"kind", "component"},
			             {"component", "conversation-list"},
			             {"min_width", 200}},
			            {{"id", "detail"},
			             {"kind", "column"},
			             {"children",
			              Json::array(
			                  {{{"id", "header"}, {"kind", "component"}, {"component", "conversation-header"}},
			                   {{"id", "history"},
			                    {"kind", "component"},
			                    {"component", "conversation-history"},
			                    {"min_height", 180}},
			                   {{"id", "replies"}, {"kind", "component"}, {"component", "conversation-replies"}},
			                   {{"id", "compose"},
			                    {"kind", "component"},
			                    {"component", "conversation-compose"}}})}}})}}}};
			layout_editor.preview().sample_state = 2;
			layout_editor.preview().width = 1280;
			layout_editor.preview().height = 720;
		}
		if (stage == 5)
		{
			console["widgets"] = Json::array();
			console["layout"] = {
			    {"schema", 1},
			    {"root",
			     {{"id", "engineering"},
			      {"kind", "row"},
			      {"children",
			       Json::array(
			           {{{"id", "cards"}, {"kind", "component"}, {"component", "engineering"}, {"min_width", 300}},
			            {{"id", "readouts"},
			             {"kind", "component"},
			             {"component", "readouts"},
			             {"min_width", 300}}})}}}};
			layout_editor.preview().sample_state = 1;
		}
		if (stage == 6)
		{
			console["widgets"] = Json::array({{{"id", "map"},
			                                   {"kind", "navigation"},
			                                   {"label", "Navigation"},
			                                   {"binding", "ship.navigation"},
			                                   {"x", 0},
			                                   {"y", 0},
			                                   {"width", 1000},
			                                   {"height", 1000}}});
			console["layout"] = {{"schema", 1},
			                     {"root", {{"id", "navigation"}, {"kind", "component"}, {"component", "navigation"}}}};
			layout_editor.preview().sample_state = 0;
		}
		if (stage == 7)
		{
			console["widgets"] = Json::array();
			console["layout"] = {{"schema", 1},
			                     {"root",
			                      {{"id", "wrap"},
			                       {"kind", "wrap"},
			                       {"children", Json::array({{{"id", "cards"},
			                                                  {"kind", "component"},
			                                                  {"component", "engineering"},
			                                                  {"width", 300},
			                                                  {"height", 180}},
			                                                 {{"id", "readouts"},
			                                                  {"kind", "component"},
			                                                  {"component", "readouts"},
			                                                  {"width", 300},
			                                                  {"height", 180}},
			                                                 {{"id", "messages"},
			                                                  {"kind", "component"},
			                                                  {"component", "messages"},
			                                                  {"width", 300},
			                                                  {"height", 180}}})}}}};
			layout_editor.preview().width = 700;
			layout_editor.preview().sample_state = 1;
		}
		if (stage == 8)
		{
			const auto &geometry = console_preview_geometry();
			if (geometry.placements.size() != 3 || geometry.placements[2].bounds.y <= geometry.placements[0].bounds.y)
				return false;
			console["widgets"] = Json::array({{{"id", "radar"},
			                                   {"kind", "radar"},
			                                   {"label", "Radar"},
			                                   {"binding", "radar.contacts"},
			                                   {"x", 0},
			                                   {"y", 0},
			                                   {"width", 1000},
			                                   {"height", 1000}}});
			console["layout"] = {{"schema", 1},
			                     {"root",
			                      {{"id", "cutout"},
			                       {"kind", "cutout"},
			                       {"min_width", 1000},
			                       {"min_height", 480},
			                       {"children", Json::array({{{"id", "radar"},
			                                                  {"kind", "component"},
			                                                  {"component", "radar"},
			                                                  {"region", "centre"},
			                                                  {"aspect", 1}},
			                                                 {{"id", "messages"},
			                                                  {"kind", "component"},
			                                                  {"component", "messages"},
			                                                  {"region", "top-left"}}})}}}};
			layout_editor.preview().width = 1280;
		}
		if (stage == 9)
		{
			const auto &geometry = console_preview_geometry();
			const auto &radar = geometry.placements.at(0).bounds;
			if (radar.width <= 0 || std::abs(radar.width - radar.height) > 1)
				return false;
			layout_editor.preview().width = 390;
			layout_editor.preview().height = 844;
		}
		if (stage == 10)
		{
			const auto &geometry = console_preview_geometry();
			if (!geometry.compact || geometry.pages.size() != 2)
				return false;
			const auto &tab = geometry.pages.back();
			layout_editor.preview().pages[geometry.page_groups.at(tab.id)] = tab.id;
		}
		if (stage == 11)
		{
			const auto &geometry = console_preview_geometry();
			if (geometry.placements.at(0).visible || !geometry.placements.at(1).visible)
				return false;
			layout_editor.preview().width = 1280;
			layout_editor.preview().height = 720;
		}
		if (stage == 12)
		{
			layout_editor.preview().width = 390;
			layout_editor.preview().height = 844;
		}
		if (stage == 13 && (console_preview_geometry().placements.at(0).visible ||
		                    !console_preview_geometry().placements.at(1).visible))
			return false;
	}

	if (stage == 14)
	{
		auto &console = document.definitions()["consoles"][0];
		console["widgets"] = Json::array();
		console["layout"] = {
		    {"schema", 1},
		    {"root",
		     {{"id", "station"},
		      {"kind", "tabs"},
		      {"children",
		       Json::array({{{"id", "power"}, {"kind", "component"}, {"component", "power"}, {"label", "Power"}},
		                    {{"id", "repair"}, {"kind", "component"}, {"component", "repair"}, {"label", "Repair"}},
		                    {{"id", "operations"},
		                     {"kind", "component"},
		                     {"component", "operations"},
		                     {"label", "Operations"}}})}}}};
		layout_editor.preview().width = 1280;
		layout_editor.preview().height = 720;
		layout_editor.preview().pages.clear();
		layout_editor.preview().sample_state = 1;
		layout_editor.preview().sample_stars = 1;
		const auto exported = document.export_pack();
		document.load_pack(exported);
		this->console = 0;
		selection.kind = "Console";
	}
	if (stage == 15)
	{
		const auto &geometry = console_preview_geometry();
		if (geometry.pages.size() != 2 || geometry.placements.size() != 2 || !geometry.placements[0].visible ||
		    geometry.placements[0].bounds.width < 500)
			return false;
		layout_editor.preview().pages["station"] = "station.repair";
	}
	if (stage == 16)
	{
		const auto &geometry = console_preview_geometry();
		if (geometry.placements[0].visible || !geometry.placements[1].visible ||
		    geometry.placements[1].bounds.height < 200)
			return false;
		layout_editor.preview().sample_state = 0;
	}
	if (stage == 17 && !console_preview_geometry().placements.empty())
		return false;
	if (stage == 18)
	{
		auto &console = document.definitions()["consoles"][0];
		console["header"] = console_ui_header_template();
		console["header"]["root"]["gap"] = 20;
		layout_editor.preview().header = true;
		layout_editor.preview().sample_stars = 3;
		document.verify();
		const auto exported = document.export_pack();
		document.load_pack(exported);
		this->console = 0;
		selection.kind = "Console";
	}
	if (stage == 19)
	{
		const auto &geometry = console_preview_geometry();
		if (geometry.placements.size() != 3 || geometry.containers.empty())
			return false;
		for (const auto &p : geometry.placements)
			if (!p.visible || p.bounds.width < 44 || p.bounds.height < 44)
				return false;
		auto &console = document.definitions()["consoles"][0];
		console["header"]["root"]["gap"] = 28;
		const auto exported = document.export_pack();
		document.load_pack(exported);
		this->console = 0;
		selection.kind = "Console";
		if (document.definitions()["consoles"][0]["header"]["root"]["gap"] != 28)
			return false;
		layout_editor.preview().header = false;
		layout_editor.preview().sample_state = 1;
		document.definitions()["consoles"][0]["layout"] = {
		    {"schema", 1},
		    {"root",
		     {{"id", "tasks"},
		      {"kind", "row"},
		      {"children", Json::array({{{"id", "power"}, {"kind", "component"}, {"component", "power"}},
		                                {{"id", "health"}, {"kind", "component"}, {"component", "compartment-health"}},
		                                {{"id", "teams"}, {"kind", "component"}, {"component", "team-roster"}}})}}}};
		layout_editor.preview().pages.clear();
	}
	if (stage == 20)
	{
		const auto &geometry = console_preview_geometry();
		if (geometry.placements.size() != 3)
			return false;
		for (const auto &p : geometry.placements)
			if (!p.visible || p.bounds.width < 100)
				return false;
		layout_editor.preview().sample_stars = 1;
	}

	return true;
}

bool WorkshopWorkspace::State::probe_workshop_controllers(unsigned stage)
{
	if (stage == 0)
	{
		if (document.manifest().at("id") != "test-core")
			return false;
		auto &channel = document.definitions()
		                    .at("entities")
		                    .at(0)
		                    .at("control_layout")
		                    .at("controllers")
		                    .at(0)
		                    .at("commands")
		                    .at(0);
		channel["initial"] = 3000;
		history.checkpoint(document);
		document.verify();
		const auto bytes = document.export_pack();
		document.load_pack(bytes);
		selection.definition = 0;
		selection.kind = "Entity";
		status = "Controller smoke: edited initial command, verified, exported and reopened.";
	}
	return gameplay.probe_test(document, stage);
}
} // namespace phoenix
