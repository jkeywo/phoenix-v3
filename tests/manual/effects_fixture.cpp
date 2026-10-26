// Explicit synthetic scaling input generator. Not a CTest or timing gate.
#include "../support/combat_fixture.hpp"
#include <iostream>
#include <string>
using namespace phoenix;
using namespace phoenix::test;
int main(int argc, char **argv)
{
	try
	{
		if (argc != 4)
			throw std::runtime_error("Usage: phoenix_effects_fixture modifier|periodic count output.zip");
		const std::string kind = argv[1];
		const auto count = std::stoul(argv[2]);
		if ((kind != "modifier" && kind != "periodic") || count < 1 || count > 8192)
			throw std::runtime_error("Choose modifier|periodic and 1..8192 applications");
		auto source = combat_fixture();
		auto observer = controller_session_fixture()["entities"][0];
		observer["id"] = "test.profile-ship";
		source["entities"].push_back(observer);
		for (auto &compartment : source["entities"][0]["combat"]["decks"][0]["compartments"])
			compartment["hp"] = 1000000;
		for (auto &controller : source["entities"][0]["control_layout"]["controllers"])
			if (controller.at("id") == "team-a" || controller.at("id") == "team-b")
				controller["commands"][0]["initial"] = 0;
		auto &map = source["maps"][0];
		map["ship_slots"] = Json::array({{{"id", "test.profile"},
		                                  {"x_mm", 1000000},
		                                  {"y_mm", 0},
		                                  {"heading_mdeg", 0},
		                                  {"allowed_ships", {"test.profile-ship"}}}});
		map["scenario"] = {{"tree", "test.effects-profile"}};
		if (kind == "modifier")
			source["effects"].push_back(
			    {{"id", "test.profile.effect"},
			     {"kind", "modifier"},
			     {"contributions", {{"range", {{"property", "sensors"}, {"delta_permille", 1}}}}}});
		else
		{
			source["effects"].push_back({{"id", "test.profile.effect"},
			                             {"kind", "periodic"},
			                             {"duration_ticks", 10000},
			                             {"period_ticks", 1},
			                             {"effect", "test.profile.damage"}});
			source["effects"].push_back(
			    {{"id", "test.profile.damage"},
			     {"kind", "operations"},
			     {"operations", Json::array({{{"kind", "damage"}, {"target", "protected"}, {"damage", 1}}})}});
		}
		Json tasks = Json::array();
		for (unsigned first = 0; first < count; first += 32)
		{
			const auto group = "test.profile.group" + std::to_string(first / 32);
			Json operations = Json::array();
			for (unsigned i = first; i < std::min<unsigned>(first + 32, count); ++i)
			{
				Json op{{"kind", "apply"},
				        {"target", "protected"},
				        {"effect", "test.profile.effect"},
				        {"grant", "application" + std::to_string(i)}};
				if (kind == "modifier")
					op["equipment"] = "radar";
				operations.push_back(op);
			}
			source["effects"].push_back({{"id", group}, {"kind", "operations"}, {"operations", operations}});
			tasks.push_back({{"action", "apply_effect_group"},
			                 {"arguments",
			                  Json::array({{{"type", "entity"}, {"id", 1}}, {{"type", "entity"}, {"id", 1}}, group})}});
		}
		source["trees"].push_back({{"id", "test.effects-profile"},
		                           {"context", "scenario"},
		                           {"initial", "start"},
		                           {"states", Json::array({{{"id", "start"}, {"entry", tasks}}})}});
		write_file(argv[3], archive(source));
		std::cout << "Synthetic " << kind << " fixture: " << count << " applications\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
