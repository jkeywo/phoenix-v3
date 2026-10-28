#pragma once
#include <cstdint>
#include <string>
#include "native_menu.hpp"

// Native executable manual journeys. Import the application/navigation and
// presentation contracts before including this harness.
enum class NativeSmokeResult
{
	Next,
	Repeat,
	Stop,
	Failed
};
struct NativeSmoke
{
	NativeMenuSmoke menu;
	bool ai_config = false, ai_config_opened = false;
	unsigned ai_config_ready_frames = 0;
	bool qr = false;
	bool join = false, join_submitted = false, claim_submitted = false;
	unsigned join_ready_frames = 0;
	int frames = 0;
	bool gui = false, tools_presentation = false;
	bool helm = false, readout = false, controller_report = false;
	bool engineering = false, personnel = false;
	bool navigation = false;
	bool on_screen = false;
	int bridge_width = 0, bridge_height = 0;
	std::uint64_t helm_tick = 0;
	bool workshop = false, controller_workshop = false;
	double started = 0;
	void prepare(phoenix::Application &app);
	NativeSmokeResult frame(phoenix::Application &app);
};
