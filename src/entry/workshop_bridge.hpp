#pragma once
// Include after imports phoenix.tools and phoenix.workshop_test in native.cpp.
namespace phoenix
{
inline void pump_native_workshop_test(Application &app, double elapsed_seconds)
{
	static GameplayTestHost test;
	static double milliseconds = 0;
	static std::string last_status;
	static std::uint32_t feedback_elapsed = 0;
	bool requested = false;
	if (auto pending = take_gameplay_test_request())
	{
		WorkshopTestRequest request;
		request.pack = std::move(pending->pack);
		request.dependencies = std::move(pending->dependencies);
		request.mission = std::move(pending->mission);
		request.slot = std::move(pending->slot);
		request.ship = std::move(pending->ship);
		switch (pending->action)
		{
		case GameplayTestAction::Start:
			request.action = WorkshopTestAction::Start;
			break;
		case GameplayTestAction::Pause:
			request.action = WorkshopTestAction::Pause;
			break;
		case GameplayTestAction::Step:
			request.action = WorkshopTestAction::Step;
			break;
		case GameplayTestAction::Restart:
			request.action = WorkshopTestAction::Restart;
			break;
		case GameplayTestAction::Stop:
			request.action = WorkshopTestAction::Stop;
			break;
		}
		test.submit(std::move(request));
		requested = true;
	}
	milliseconds += std::clamp(elapsed_seconds, 0.0, 0.25) * 1000;
	const auto elapsed = static_cast<std::uint32_t>(milliseconds);
	milliseconds -= elapsed;
	test.advance(elapsed);
	feedback_elapsed += elapsed;
	if (!requested && feedback_elapsed < 100)
		return;
	feedback_elapsed %= 100;
	const auto feedback = test.feedback();
	const auto status = feedback.value("status", std::string{});
	if (status != last_status)
	{
		app.status = status;
		last_status = status;
	}
	set_gameplay_test_feedback(feedback);
}
} // namespace phoenix
