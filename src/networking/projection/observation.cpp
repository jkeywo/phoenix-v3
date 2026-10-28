module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module phoenix.networking.projection;
import phoenix.knowledge.read_models;
import phoenix.content.controllers;

namespace phoenix::network
{
namespace
{
BodyView read_body(const Json &value)
{
	return projection::read_body(value, projection::BodyReadPolicy::NetworkCompatible);
}
} // namespace
using projection::body_json;
Json observation_json(const Observation &observation)
{
	Json additional = Json::array();
	for (const auto &console : observation.additional_consoles)
		additional.push_back(observation_json(console));
	Json bearings = Json::array();
	for (const auto &b : observation.bearing_contacts)
		bearings.push_back(projection::bearing_json(b));
	Json contacts = Json::array();
	for (const auto &contact : observation.contacts)
		contacts.push_back(body_json(contact));
	Json systems = Json::array();
	for (const auto &system : observation.systems)
	{
		const auto profile = sensor_reach_json(system);
		systems.push_back({{"id", system.id},
		                   {"kind", std::string(operating_system_name(system.kind))},
		                   {"requested", system.requested},
		                   {"maximum", system.maximum},
		                   {"enabled", system.enabled},
		                   {"controlled", system.controlled},
		                   {"telemetry", system.telemetry},
		                   {"status", system.status},
		                   {"effective_range", system.effective_range},
		                   {"radial_ranges", system.radial_ranges},
		                   {"reach_profile", profile},
		                   {"reach_uncertainty_mm", system.reach_uncertainty_mm},
		                   {"reach_limited", system.reach_limited},
		                   {"shows", system.shows},
		                   {"contact_ids", system.contact_ids}});
	}
	return {{"own", body_json(observation.own)},
	        {"contacts", contacts},
	        {"bearing_contacts", bearings},
	        {"observed_tick", observation.observed_tick},
	        {"requested_speed", observation.requested_speed},
	        {"requested_heading", observation.requested_heading},
	        {"engine_enabled", observation.engine_enabled},
	        {"radar_enabled", observation.radar_enabled},
	        {"console", observation.console},
	        {"controls", observation.controls},
	        {"feeds", observation.feeds},
	        {"systems", systems},
	        {"gameplay", bounded_gameplay(observation.gameplay)},
	        {"additional_consoles", additional},
	        {"regions", observation.regions}};
}
Observation read_observation(const Json &value, const Catalog &catalog)
{
	Observation result{read_body(value.at("own")), {}};
	const auto additional = value.value("additional_consoles", Json::array());
	if (!additional.is_array() || additional.size() > 31)
		throw std::runtime_error("Invalid additional Console count");
	for (const auto &entry : additional)
	{
		if (!entry.value("additional_consoles", Json::array()).empty())
			throw std::runtime_error("Nested additional Consoles are unsupported");
		auto console = read_observation(entry, catalog);
		if (console.own.id != result.own.id)
			throw std::runtime_error("Additional Console belongs to another ship");
		result.additional_consoles.push_back(std::move(console));
	}
	if (!catalog.entities.contains(result.own.definition) || !value.at("contacts").is_array() ||
	    value.at("contacts").size() > 4096)
		throw std::runtime_error("Projected observation does not match verified content");
	result.bearing_contacts = projection::read_bearings(value.value("bearing_contacts", Json::array()));
	for (const auto &contact : value.at("contacts"))
	{
		auto body = read_body(contact);
		// Contact definitions carry observed public class labels, not private catalog identifiers.
		result.contacts.push_back(std::move(body));
	}
	if (value.contains("observed_tick") &&
	    (!value.at("observed_tick").is_number_integer() || value.at("observed_tick") < 0 ||
	     value.at("observed_tick") > ~std::uint64_t{0}))
		throw std::runtime_error("Invalid observation tick");
	result.observed_tick = value.value("observed_tick", std::uint64_t{0});
	for (const auto &b : result.bearing_contacts)
		if (b.observed_tick > result.observed_tick || std::any_of(result.contacts.begin(), result.contacts.end(),
		                                                          [&](const auto &c)
		                                                          {
			                                                          return c.id == b.id;
		                                                          }))
			throw std::runtime_error("Invalid bearing observation");
	result.requested_speed = value.at("requested_speed");
	result.requested_heading = value.at("requested_heading");
	result.engine_enabled = value.at("engine_enabled");
	result.radar_enabled = value.at("radar_enabled");
	result.console = value.value("console", std::string{"main"});
	const auto controls = value.value("controls", 7), feeds = value.value("feeds", 7);
	if (controls < 0 || controls > static_cast<std::int64_t>((system_bit(OperatingSystem::Psychic) << 1) - 1) ||
	    feeds < 0 || feeds > static_cast<std::int64_t>((system_bit(OperatingSystem::Psychic) << 1) - 1) ||
	    (controls & feeds) != controls || result.console.size() > 128)
		throw std::runtime_error("Invalid projected Console permissions");
	result.controls = static_cast<SystemMask>(controls);
	result.feeds = static_cast<SystemMask>(feeds);
	const auto specs = resolve_systems(catalog, result.own.definition);
	const auto seats = resolve_consoles(catalog, result.own.definition);
	const auto seat = std::find_if(seats.begin(), seats.end(),
	                               [&](const auto &entry)
	                               {
		                               return entry.id == result.console;
	                               });
	if (seat == seats.end() || seat->controls != result.controls || seat->feeds != result.feeds)
		throw std::runtime_error(
		    "Projected Console permissions disagree with verified content: " + result.console + " received " +
		    std::to_string(result.controls) + "/" + std::to_string(result.feeds) +
		    (seat == seats.end() ? " (Console absent)"
		                         : " expected " + std::to_string(seat->controls) + "/" + std::to_string(seat->feeds)));
	const auto &projected = value.at("systems");
	if (!projected.is_array() || projected.size() != seat->feed_systems.size())
		throw std::runtime_error("Projected System roster mismatch");
	std::vector<std::string> seen;
	for (const auto &entry : projected)
	{
		const auto id = entry.at("id").get<std::string>();
		const auto spec = std::find_if(specs.begin(), specs.end(),
		                               [&](const auto &item)
		                               {
			                               return item.id == id;
		                               });
		if (spec == specs.end() ||
		    std::find(seat->feed_systems.begin(), seat->feed_systems.end(), id) == seat->feed_systems.end() ||
		    std::find(seen.begin(), seen.end(), id) != seen.end() ||
		    entry.at("kind") != std::string(operating_system_name(spec->kind)))
			throw std::runtime_error("Unpermitted projected System");
		seen.push_back(id);
		const bool controlled = std::find(seat->controlled_systems.begin(), seat->controlled_systems.end(), id) !=
		                        seat->controlled_systems.end();
		SystemView state{
		    id, spec->kind, entry.at("requested"), entry.at("maximum"), entry.at("enabled"), entry.at("controlled")};
		const auto maximum = spec->kind == OperatingSystem::Steering ? 359999 : spec->maximum;
		if ((state.controlled && !controlled) || state.maximum != maximum || state.requested < -maximum ||
		    (spec->kind != OperatingSystem::Radar &&
		     (state.requested > 1000000000000LL || state.requested < -1000000000)) ||
		    (spec->kind == OperatingSystem::Radar && state.requested != 0))
			throw std::runtime_error("Invalid projected System state");
		state.telemetry = entry.value("telemetry", std::map<std::string, std::int64_t>{});
		state.status = entry.value("status", std::string{});
		state.effective_range =
		    entry.value("effective_range", state.kind == OperatingSystem::Radar && state.enabled ? state.maximum : 0LL);
		state.shows = entry.value("shows", std::vector<std::string>{"contact"});
		state.contact_ids = entry.value("contact_ids", std::vector<EntityId>{});
		if (!entry.contains("contact_ids"))
			for (const auto &body : result.contacts)
				state.contact_ids.push_back(body.id);
		if (state.shows.size() > 32 ||
		    state.contact_ids.size() > result.contacts.size() + result.bearing_contacts.size())
			throw std::runtime_error("Invalid radar tag projection");
		for (const auto &tag : state.shows)
			if (tag.empty() || tag.size() > 128)
				throw std::runtime_error("Invalid radar tag");
		for (const auto id : state.contact_ids)
			if (std::none_of(result.contacts.begin(), result.contacts.end(),
			                 [&](const auto &b)
			                 {
				                 return b.id == id;
			                 }) &&
			    std::none_of(result.bearing_contacts.begin(), result.bearing_contacts.end(),
			                 [&](const auto &b)
			                 {
				                 return b.id == id;
			                 }))
				throw std::runtime_error("Unknown radar contact");
		state.radial_ranges = entry.value("radial_ranges", std::vector<std::int64_t>{});
		read_sensor_reach(state, entry.value("reach_profile", Json::array()));
		state.reach_uncertainty_mm = entry.value("reach_uncertainty_mm", 0LL);
		state.reach_limited = entry.value("reach_limited", false);
		if (state.reach_uncertainty_mm < 0 || state.reach_uncertainty_mm > state.effective_range)
			throw std::runtime_error("Invalid sensor uncertainty");
		// The host applies officer bonuses; nominal authored range is not an effective-range cap.
		const auto range_limit = state.kind == OperatingSystem::Radar ? projection::sensor_range_limit : state.maximum;
		if (state.effective_range < 0 || state.effective_range > range_limit ||
		    (!state.radial_ranges.empty() &&
		     (state.kind != OperatingSystem::Radar || state.radial_ranges.size() != 128)) ||
		    std::any_of(state.radial_ranges.begin(), state.radial_ranges.end(),
		                [&](auto n)
		                {
			                return n < 0 || n > state.effective_range;
		                }))
			throw std::runtime_error("Invalid projected sensor envelope");
		if (state.telemetry.size() > 128 || state.status.size() > 256)
			throw std::runtime_error("Invalid projected telemetry bounds");
		result.systems.push_back(std::move(state));
	}
	if (result.requested_speed < -1000000000 || result.requested_speed > 1000000000 || result.requested_heading < 0 ||
	    result.requested_heading >= 360000)
		throw std::runtime_error("Invalid projected operating request");
	result.regions = value.value("regions", Json::array());
	if (!result.regions.is_array() || result.regions.size() > 256)
		throw std::runtime_error("Invalid region marker count");
	for (const auto &r : result.regions)
	{
		if (!r.is_object() || r.at("name").get<std::string>().size() > 128 || !r.at("navigation").is_boolean() ||
		    !r.at("radar").is_boolean() ||
		    (r.at("shape") != "circle" && r.at("shape") != "rectangle" && r.at("shape") != "annulus"))
			throw std::runtime_error("Invalid region marker");
		for (const auto *key : {"x", "y", "radius", "inner_radius", "half_width", "half_height"})
			if (!r.at(key).is_number_integer() || r.at(key) < -1000000000 || r.at(key) > 1000000000)
				throw std::runtime_error("Invalid region marker dimensions");
		if (!r.at("colour").is_array() || r.at("colour").size() != 3)
			throw std::runtime_error("Invalid region colour");
		for (const auto &c : r.at("colour"))
			if (!c.is_number() || c < 0 || c > 1)
				throw std::runtime_error("Invalid region colour");
		if (!r.at("tags").is_array() || r.at("tags").size() > 32)
			throw std::runtime_error("Invalid region tags");
		for (const auto &tag : r.at("tags"))
			if (!tag.is_string() || tag.get<std::string>().size() > 128)
				throw std::runtime_error("Invalid region tag");
	}
	result.gameplay = value.value("gameplay", Json::object());
	if (const auto controls = result.gameplay.find("controls");
	    controls != result.gameplay.end() && controls->is_array())
		for (const auto &control : *controls)
		{
			ability_projection::validate_control(control);
			process_projection::validate_control(control);
		}
	if (result.gameplay.contains("ftl"))
		result.gameplay["ftl"] = ftl_projection::board(result.gameplay.at("ftl"));
	if (result.gameplay.contains("mission_report"))
		result.gameplay["mission_report"] = ftl_projection::report(result.gameplay.at("mission_report"));
	if (result.gameplay.contains("psychic"))
		result.gameplay["psychic"] = psychic_projection::board(result.gameplay.at("psychic"));
	if (result.gameplay.contains("processes"))
		result.gameplay["processes"] = process_projection::board(result.gameplay.at("processes"));
	if (result.gameplay.contains("abilities"))
		result.gameplay["abilities"] = ability_projection::board(result.gameplay.at("abilities"));
	if (result.gameplay.contains("rules"))
		result.gameplay["rules"] = rules_projection::board(result.gameplay.at("rules"));
	if (result.gameplay.contains("sensors"))
		result.gameplay["sensors"] = sensor_projection::board(result.gameplay.at("sensors"));
	if (result.gameplay.contains("directed"))
		result.gameplay["directed"] = directed_projection::board(result.gameplay.at("directed"));
	if (result.gameplay.contains("away_missions"))
		result.gameplay["away_missions"] = away_projection::board(result.gameplay.at("away_missions"));
	if (result.gameplay.contains("docking"))
		result.gameplay["docking"] = docking_projection::board(result.gameplay.at("docking"));
	if (result.gameplay.contains("logistics"))
		result.gameplay["logistics"] = logistics_projection::board(result.gameplay.at("logistics"));
	if (result.gameplay.contains("craft"))
		result.gameplay["craft"] = craft_projection::board(result.gameplay.at("craft"));
	if (result.gameplay.contains("personnel"))
		result.gameplay["personnel"] = personnel_projection::board(result.gameplay.at("personnel"));
	if (result.gameplay.contains("teams"))
		result.gameplay["teams"] = team_projection::board(result.gameplay.at("teams"));
	if (result.gameplay.contains("conversations"))
		result.gameplay["conversations"] = conversation_projection::board(result.gameplay.at("conversations"));
	// StateSource/StateTransfer already authenticate and bound the complete
	// snapshot to 4 MiB. Permitted records and history may exceed one packet;
	// applying a smaller sub-envelope limit would reject valid chunked snapshots.
	if (!result.gameplay.is_object() || result.gameplay.dump().size() > 4 * 1024 * 1024)
		throw std::runtime_error("Invalid knowledge projection bounds");
	return result;
}
std::uint64_t counter(const Json &value)
{
	if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 0))
		throw std::runtime_error("Invalid session counter");
	auto result = value.get<std::uint64_t>();
	if (result > 9007199254740991ULL)
		throw std::runtime_error("Session counter exceeds supported bounds");
	return result;
}
} // namespace phoenix::network
