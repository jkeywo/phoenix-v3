// Explicitly invoked graphical rescue journey; never part of automatic ctest.
#include <raylib.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <algorithm>
#include <iostream>
#include <fstream>
#include <cmath>
#include <string>
#include <nlohmann/json.hpp>
import phoenix.application;
import phoenix.presentation;
import phoenix.session.execution;
import phoenix.console;
using namespace phoenix;
#include "rescue_opening.hpp"
int main(int argc, char **argv)
{
	bool opened = false;
	try
	{
		if (argc < 2)
			throw std::runtime_error("Usage: phoenix_mission_smoke pack-directory "
			                         "[delivery-courier|delivery-cruiser|preview-cruiser] [arrival-checkpoint]");
		const std::string mode = argc > 2 ? argv[2] : "delivery-courier";
		if (mode != "delivery-courier" && mode != "delivery-cruiser" && mode != "preview-cruiser")
			throw std::runtime_error("Choose delivery-courier, delivery-cruiser or preview-cruiser.");
		return rescue_opening_smoke(argv[1], mode != "delivery-courier", opened, argc > 3 ? argv[3] : "",
		                            mode == "preview-cruiser");
	}
	catch (const std::exception &e)
	{
		if (opened)
			close_window();
		std::cerr << "FAIL " << e.what() << std::endl;
		return 1;
	}
}
