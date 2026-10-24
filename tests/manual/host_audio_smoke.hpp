// Explicit, real-time device smoke. This captures signal statistics before the
// device's master gain; it is not a human listening judgement.
namespace
{
std::mutex capture_mutex;
struct AudioCapture
{
	unsigned long long samples = 0, nonzero = 0;
	float peak = 0;
};
AudioCapture audio_capture[5];
std::atomic<int> audio_phase{0};
void capture_mix(void *buffer, unsigned frames)
{
	const auto *samples = static_cast<const float *>(buffer);
	const std::lock_guard lock(capture_mutex);
	auto &capture = audio_capture[audio_phase.load()];
	capture.samples += frames * 2;
	for (unsigned i = 0; i < frames * 2; ++i)
	{
		const float value = std::abs(samples[i]);
		capture.peak = std::max(capture.peak, value);
		capture.nonzero += value > .00001f;
	}
}
void run_host_audio_smoke(Application &app, const std::function<void()> &frame)
{
	if (!IsAudioDeviceReady())
		throw std::runtime_error("Audio device unavailable for explicit smoke");
	std::vector<std::uint64_t> routes{1};
	const auto own = app.session->focus_body();
	unsigned next_route = 2;
	for (const auto &seat : app.session->console_seats())
		if (seat.address.ship == own->id && app.session->claim_console(next_route, seat.address))
			routes.push_back(next_route++);
	app.session->tick();
	EntityId target = 0;
	for (const auto &body : app.session->viewscreen())
		if (body.id != own->id && body.definition.find("pirate") != std::string::npos)
		{
			target = body.id;
			break;
		}
	if (!target)
		throw std::runtime_error("Audio smoke pirate target unavailable");
	unsigned weapons = 0;
	Json requests = Json::array();
	std::function<void(std::uint64_t, const Observation &)> dispatch = [&](std::uint64_t route, const Observation &view)
	{
		for (const auto &controller : view.gameplay.value("controllers", Json::array()))
			for (const auto &[key, channel] : controller.at("commands").items())
			{
				const auto action = channel.at("action").get<std::string>();
				const bool weapon = action == "weapon_target";
				if (!weapon && action != "speed_mm_s" && action != "longitudinal_thrust")
					continue;
				OperatingCommand command{own->id,
				                         action,
				                         weapon                   ? static_cast<std::int64_t>(target)
				                         : action == "speed_mm_s" ? 12000
				                                                  : 400,
				                         {},
				                         controller.at("id"),
				                         key,
				                         controller.at("generation")};
				command.console = view.console;
				const bool accepted = app.session->console_command(route, command);
				if (weapon && accepted)
					++weapons;
			}
		OperatingCommand alert{own->id, "alert.set", 4, {}};
		alert.console = view.console;
		app.session->console_command(route, alert);
		requests.push_back({{"route", route},
		                    {"console", view.console},
		                    {"controllers", view.gameplay.value("controllers", Json::array())}});
		for (const auto &additional : view.additional_consoles)
			dispatch(route, additional);
	};
	for (const auto route : routes)
		if (const auto view = app.session->console_observation(route))
			dispatch(route, *view);
	write_file("host-audio-requests.json", json_bytes(requests));
	if (!weapons)
		throw std::runtime_error("No admitted audio-smoke weapon commands");
	app.screen = Screen::viewscreen;
	app.music_volume = app.siren_volume = 0;
	app.sfx_volume = app.master_volume = 100;
	SetTargetFPS(60);
	AttachAudioMixedProcessor(capture_mix);
	for (int phase = 0; phase < 5; ++phase)
	{
		audio_phase = phase;
		app.screen = phase == 2 ? Screen::console : Screen::viewscreen;
		if (phase == 4)
		{
			std::function<void(std::uint64_t, const Observation &)> leave_alert =
			    [&](std::uint64_t route, const Observation &view)
			{
				OperatingCommand command{own->id, "alert.set", 0, {}};
				command.console = view.console;
				app.session->console_command(route, command);
				for (const auto &additional : view.additional_consoles)
					leave_alert(route, additional);
			};
			for (const auto route : routes)
				if (const auto view = app.session->console_observation(route))
					leave_alert(route, *view);
		}
		app.sfx_volume = (phase == 0 || phase == 2) ? 100 : 0;
		app.music_volume = phase == 3 ? 100 : 0;
		const int frames = (phase == 1 || phase == 4) ? 120 : 240;
		for (int i = 0; i < frames; ++i)
		{
			if (i % 3 == 0)
				app.session->tick();
			frame();
		}
		if (phase == 0)
			TakeScreenshot("host-audio-smoke.png");
	}
	DetachAudioMixedProcessor(capture_mix);
	Json evidence{{"admitted_weapons", weapons}, {"phases", Json::array()}};
	evidence["final_alert"] = app.session->public_board().value("alert", Json::object());
	for (const auto &capture : audio_capture)
		evidence["phases"].push_back(
		    {{"samples", capture.samples}, {"nonzero", capture.nonzero}, {"peak", capture.peak}});
	write_file("host-audio-smoke.json", json_bytes(evidence));
	if (!audio_capture[0].nonzero || !audio_capture[2].nonzero || !audio_capture[3].nonzero)
		throw std::runtime_error("Expected live SFX/music output was absent");
	std::cout << evidence.dump(2) << '\n';
}
} // namespace
