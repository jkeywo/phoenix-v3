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
import phoenix.content.controllers;
import phoenix.content.distribution;
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
			throw std::runtime_error(
			    "Usage: phoenix_mission_smoke pack-directory "
			    "[delivery-courier|delivery-cruiser|preview-courier|preview-cruiser|comms-preview-courier|comms-"
			    "preview-cruiser|checkpoint-courier|checkpoint-"
			    "cruiser|seize-"
			    "courier|seize-cruiser|checkpoint-seize-courier|checkpoint-seize-cruiser|checkpoint-takeover-courier|"
			    "checkpoint-takeover-cruiser|checkpoint-recapture-cruiser] [checkpoint]");
		const std::string mode = argc > 2 ? argv[2] : "delivery-courier";
		if (mode != "delivery-courier" && mode != "delivery-cruiser" && mode != "preview-courier" &&
		    mode != "preview-cruiser" && mode != "comms-preview-courier" && mode != "comms-preview-cruiser" &&
		    mode != "checkpoint-cruiser" && mode != "checkpoint-courier" && mode != "seize-courier" &&
		    mode != "seize-cruiser" && mode != "checkpoint-seize-courier" && mode != "checkpoint-seize-cruiser" &&
		    mode != "checkpoint-takeover-cruiser" && mode != "checkpoint-takeover-courier" &&
		    mode != "checkpoint-recapture-cruiser")
			throw std::runtime_error("Choose a delivery, checkpoint, preview or seizure walkthrough mode.");
		return rescue_opening_smoke(argv[1], mode.find("cruiser") != std::string::npos, opened, argc > 3 ? argv[3] : "",
		                            mode.starts_with("preview-") || mode.starts_with("comms-preview-"),
		                            !mode.starts_with("checkpoint-"), mode.find("seize-") != std::string::npos,
		                            mode.find("takeover-") != std::string::npos ||
		                                mode.find("recapture-") != std::string::npos,
		                            mode.find("recapture-") != std::string::npos, mode.starts_with("comms-preview-"));
	}
	catch (const std::exception &e)
	{
		if (opened)
			close_window();
		std::cerr << "FAIL " << e.what() << std::endl;
		return 1;
	}
}
