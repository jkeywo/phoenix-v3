module;
#ifdef __EMSCRIPTEN__
#include "../platform/browser_main.hpp"
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

module phoenix.session.local;

namespace phoenix
{
#if !defined(__EMSCRIPTEN__) || defined(PHOENIX_BROWSER_THREADS)
namespace
{
class FrameSession final : public SessionPort
{
	using Clock = std::chrono::steady_clock;
	struct Frame
	{
		std::shared_ptr<const Catalog> catalog;
		std::uint64_t generation = 0, ticks = 0;
		bool running = false, recovery = false, networked = false;
		std::string loading, phase, status, invitation;
		std::optional<std::pair<std::uint64_t, std::uint64_t>> progress;
		LobbyProjection lobby;
		std::map<std::uint64_t, std::shared_ptr<const Observation>> consoles;
		std::shared_ptr<const Observation> primary;
		std::vector<BodyView> viewscreen;
		std::shared_ptr<const std::vector<SceneBody>> scene;
		Json lighting = Json::object(), presentation = Json::object(), board = Json::object();
		Json inference = Json::object(), inspection, profile = Json::object();
		std::vector<ConsoleSeat> seats;
		std::vector<std::pair<std::string, std::string>> lobby_controls;
		std::optional<BodyView> focus;
	};
	std::shared_ptr<SessionPort> backend_;
	std::shared_ptr<const Frame> latest_;
	mutable std::mutex latest_mutex_;
	std::shared_ptr<const Frame> load_frame() const
	{
		std::lock_guard lock(latest_mutex_);
		return latest_;
	}
	mutable std::shared_ptr<const Frame> reader_;
	std::shared_ptr<const Catalog> catalog_;
	std::deque<std::shared_ptr<const Frame>> retired_;
	void store_frame(std::shared_ptr<const Frame> frame)
	{
		std::shared_ptr<const Frame> previous;
		{
			std::lock_guard lock(latest_mutex_);
			previous = std::exchange(latest_, std::move(frame));
		}
		if (previous)
			retired_.push_back(std::move(previous));
		while (retired_.size() > 8)
			retired_.pop_front();
	}
	std::set<std::uint64_t> routes_{1};
	mutable std::mutex mutex_;
	mutable std::condition_variable wake_;
	mutable std::deque<std::function<void()>> jobs_;
	bool stopping_ = false;
	std::atomic<bool> clock_requested_ = false, scene_requested_ = false, inference_requested_ = false,
	                  inspection_requested_ = false;
	std::atomic<bool> finished_ = false;
	std::thread worker_;
	Clock::time_point tick_deadline_ = Clock::now();
	std::deque<double> tick_times_;
	double maximum_lag_ = 0;
	std::uint64_t completed_ticks_ = 0, accepted_commands_ = 0, refused_commands_ = 0;
	std::string failure_;

	void update_scene(Frame &frame)
	{
		if (!scene_requested_)
			return;
		frame.viewscreen = backend_->viewscreen();
		auto bodies = backend_->scene();
		for (auto &body : bodies)
			if (!body.sound.empty())
				body.audio.push_back({SceneAudioKind::Shot, "explosion", {body.sound, 1}, body.id, body.effect_tick});
		frame.scene = std::make_shared<const std::vector<SceneBody>>(std::move(bodies));
		frame.lighting = backend_->scene_lighting();
		frame.presentation = backend_->scene_presentation();
		frame.focus = backend_->focus_body();
	}
	void publish()
	{
		auto frame = std::make_shared<Frame>();
		if (!catalog_ || catalog_->ordered_packs != backend_->catalog().ordered_packs)
			catalog_ = std::make_shared<const Catalog>(backend_->catalog());
		frame->catalog = catalog_;
		frame->generation = backend_->generation();
		frame->ticks = backend_->ticks();
		frame->running = backend_->running();
		frame->recovery = backend_->recovery_pending();
		frame->networked = backend_->networked();
		frame->loading = backend_->loading_scenario();
		frame->phase = backend_->preparation_phase();
		frame->progress = backend_->content_progress();
		frame->lobby = backend_->lobby();
		// Lobby reservations are claimed inside the backend during launch, rather
		// than through this wrapper's claim_console. Publish those local surfaces
		// too, including reservations made before the worker was attached.
		auto routes = routes_;
		for (const auto &seat : frame->lobby.consoles)
			if (seat.local_screen)
				routes.insert(seat.local_screen);
		for (const auto route : routes)
			frame->consoles.emplace(route, backend_->console_snapshot(route));
		frame->primary = frame->consoles.contains(1) ? frame->consoles.at(1) : nullptr;
		if (!frame->primary)
			if (auto value = backend_->observation())
				frame->primary = std::make_shared<const Observation>(std::move(*value));
		frame->focus = frame->primary ? std::optional(frame->primary->own) : backend_->focus_body();
		frame->board = backend_->public_board();
		frame->seats = backend_->console_seats();
		frame->status = backend_->connection_status();
		frame->invitation = backend_->invitation_code();
		frame->lobby_controls = backend_->lobby_controls();
		update_scene(*frame);
		if (inference_requested_)
			frame->inference = backend_->inference_state();
		if (inspection_requested_)
			frame->inspection = backend_->inspection();
		frame->profile["accepted_commands"] = accepted_commands_;
		frame->profile["refused_commands"] = refused_commands_;
		auto times = std::vector<double>(tick_times_.begin(), tick_times_.end());
		if (!times.empty())
		{
			std::sort(times.begin(), times.end());
			double total = 0;
			for (const auto value : times)
				total += value;
			frame->profile = {{"completed_ticks", completed_ticks_},
			                  {"accepted_commands", accepted_commands_},
			                  {"refused_commands", refused_commands_},
			                  {"mean_tick_ms", total / times.size()},
			                  {"p95_tick_ms", times[(times.size() - 1) * 95 / 100]},
			                  {"p99_tick_ms", times[(times.size() - 1) * 99 / 100]},
			                  {"maximum_lag_ms", maximum_lag_},
			                  {"remaining_backlog_seconds",
			                   clock_requested_
			                       ? std::max(0.0, std::chrono::duration<double>(Clock::now() - tick_deadline_).count())
			                       : 0.0},
			                  {"failure", failure_}};
		}
		store_frame(std::move(frame));
	}
	std::uint64_t record_ticks(std::uint64_t before, Clock::time_point started)
	{
		const auto after = backend_->ticks();
		if (after <= before)
			return 0;
		tick_times_.push_back(std::chrono::duration<double, std::milli>(Clock::now() - started).count());
		if (tick_times_.size() > 4096)
			tick_times_.pop_front();
		completed_ticks_ += after - before;
		return after - before;
	}
	void run()
	{
		auto &next_tick = tick_deadline_;
		next_tick = Clock::now();
		auto next_poll = next_tick, next_publication = next_tick, next_attempt = next_tick;
		bool clock_active = false;
		while (true)
		{
			std::deque<std::function<void()>> jobs;
			{
				std::unique_lock lock(mutex_);
				wake_.wait_until(lock, std::min({next_attempt, next_poll, next_publication}),
				                 [&]
				                 {
					                 return stopping_ || !jobs_.empty();
				                 });
				if (stopping_)
					break;
				jobs.swap(jobs_);
			}
			try
			{
				for (auto &job : jobs)
					job();
				auto now = Clock::now();
				bool scene_changed = false;
				if (now >= next_poll)
				{
					const auto before = backend_->ticks();
					const auto started = Clock::now();
					backend_->poll();
					const auto advanced = record_ticks(before, started);
					scene_changed |= advanced > 0;
					if (clock_requested_ && advanced)
					{
						next_tick += std::chrono::milliseconds(50 * advanced);
						next_attempt = next_tick;
					}
					next_poll = Clock::now() +
					            std::chrono::milliseconds(clock_requested_ && Clock::now() >= next_tick ? 2 : 10);
				}
				now = Clock::now();
				if (!clock_requested_ || !backend_->running())
				{
					clock_active = false;
					next_tick = next_attempt = now + std::chrono::milliseconds(50);
				}
				else if (!clock_active)
				{
					clock_active = true;
					next_tick = next_attempt = now + std::chrono::milliseconds(50);
				}
				else if (now >= next_tick && now >= next_attempt)
				{
					maximum_lag_ =
					    std::max(maximum_lag_, std::chrono::duration<double, std::milli>(now - next_tick).count());
					const auto started = Clock::now();
					const auto before_tick = backend_->ticks();
					backend_->tick();
					const auto advanced = record_ticks(before_tick, started);
					scene_changed |= advanced > 0;
					next_tick += std::chrono::milliseconds(50 * advanced);
					// Durable hosts acknowledge a due tick from poll(), after persistence.
					// Retain that deadline until a real tick commits; retrying the due
					// request is idempotent and cannot manufacture extra simulation time.
					next_attempt = advanced ? next_tick : Clock::now() + std::chrono::milliseconds(2);
					next_poll = std::min(next_poll, next_attempt);
				}
				if (scene_requested_ && (scene_changed || !load_frame()->scene))
				{
					auto frame = std::make_shared<Frame>(*load_frame());
					frame->ticks = backend_->ticks();
					update_scene(*frame);
					store_frame(std::move(frame));
				}
				if (Clock::now() >= next_publication)
				{
					publish();
					do
					{
						next_publication += std::chrono::milliseconds(100);
					} while (next_publication <= Clock::now());
				}
			}
			catch (const std::exception &error)
			{
				failure_ = error.what();
				clock_requested_ = false;
				auto failed = std::make_shared<Frame>(*load_frame());
				failed->profile["failure"] = failure_;
				store_frame(std::move(failed));
				next_tick = Clock::now() + std::chrono::milliseconds(50);
				next_poll = next_attempt = next_tick;
				next_publication = next_tick;
			}
		}
		backend_.reset();
		finished_ = true;
	}
	template <typename Function> auto invoke(Function function) const
	{
		using Result = std::invoke_result_t<Function, SessionPort &>;
		auto task = std::make_shared<std::packaged_task<Result()>>(
		    [this, function = std::move(function)]() mutable -> Result
		    {
			    auto &self = *const_cast<FrameSession *>(this);
			    if constexpr (std::is_void_v<Result>)
			    {
				    function(*backend_);
				    self.publish();
			    }
			    else
			    {
				    auto result = function(*backend_);
				    self.publish();
				    return result;
			    }
		    });
		auto result = task->get_future();
		{
			std::lock_guard lock(mutex_);
			jobs_.emplace_back(
			    [task]
			    {
				    (*task)();
			    });
		}
		wake_.notify_one();
#ifdef __EMSCRIPTEN__
		while (result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
		{
			browser::pump_main_calls();
			std::this_thread::yield();
		}
#endif
		if constexpr (std::is_void_v<Result>)
		{
			result.get();
			reader_ = load_frame();
		}
		else
		{
			auto value = result.get();
			reader_ = load_frame();
			return value;
		}
	}

public:
	explicit FrameSession(std::shared_ptr<SessionPort> backend) : backend_(std::move(backend))
	{
		publish();
		reader_ = load_frame();
		worker_ = std::thread(
		    [this]
		    {
			    run();
		    });
	}
	~FrameSession() override
	{
		{
			std::lock_guard lock(mutex_);
			stopping_ = true;
		}
		wake_.notify_one();
#ifdef __EMSCRIPTEN__
		while (!finished_)
		{
			browser::pump_main_calls();
			std::this_thread::yield();
		}
#endif
		worker_.join();
	}
	Json profile() const
	{
		return reader_->profile;
	}
	Bytes pause_checkpoint()
	{
		return invoke(
		    [&](auto &session)
		    {
			    clock_requested_ = false;
			    return session.save();
		    });
	}
	void poll() override
	{
#ifdef __EMSCRIPTEN__
		browser::pump_main_calls();
#endif
		reader_ = load_frame();
		if (!reader_->profile.value("failure", std::string{}).empty())
			throw std::runtime_error(reader_->profile.at("failure").get<std::string>());
	}
	void tick() override
	{
		clock_requested_ = true;
		wake_.notify_one();
	}
	std::uint64_t generation() const override
	{
		return reader_->generation;
	}
	const Catalog &catalog() const override
	{
		return *reader_->catalog;
	}
	bool running() const override
	{
		return reader_->running;
	}
	bool recovery_pending() const override
	{
		return reader_->recovery;
	}
	std::uint64_t ticks() const override
	{
		return reader_->ticks;
	}
	std::string loading_scenario() const override
	{
		return reader_->loading;
	}
	std::string preparation_phase() const override
	{
		return reader_->phase;
	}
	std::optional<std::pair<std::uint64_t, std::uint64_t>> content_progress() const override
	{
		return reader_->progress;
	}
	LobbyProjection lobby() const override
	{
		return reader_->lobby;
	}
	bool networked() const override
	{
		return reader_->networked;
	}
	std::string connection_status() const override
	{
		return reader_->status;
	}
	std::string invitation_code() const override
	{
		return reader_->invitation;
	}
	std::vector<std::pair<std::string, std::string>> lobby_controls() const override
	{
		return reader_->lobby_controls;
	}
	std::vector<ConsoleSeat> console_seats() const override
	{
		return reader_->seats;
	}
	bool dispatch_console_command(std::uint64_t route, const OperatingCommand &value) override
	{
		if (!reader_->consoles.contains(route) || !reader_->consoles.at(route))
			return false;
		{
			std::lock_guard lock(mutex_);
			if (!stopping_ && jobs_.size() < 512)
			{
				jobs_.emplace_back(
				    [this, route, value]
				    {
					    if (backend_->console_command(route, value))
						    ++accepted_commands_;
					    else
						    ++refused_commands_;
				    });
				wake_.notify_one();
				return true;
			}
		}
		// Backpressure never discards neutral/release commands. A saturated UI
		// waits for authoritative admission instead of growing an unbounded queue.
		return console_command(route, value);
	}
	std::shared_ptr<const Observation> console_snapshot(std::uint64_t route) const override
	{
		return reader_->consoles.contains(route) ? reader_->consoles.at(route) : nullptr;
	}
	std::optional<Observation> console_observation(std::uint64_t route) const override
	{
		auto value = console_snapshot(route);
		return value ? std::optional(*value) : std::nullopt;
	}
	std::optional<Observation> observation() const override
	{
		return reader_->primary ? std::optional(*reader_->primary) : std::nullopt;
	}
	std::optional<BodyView> focus_body() const override
	{
		return reader_->focus;
	}
	std::vector<BodyView> viewscreen() const override
	{
		const_cast<FrameSession *>(this)->scene_requested_ = true;
		return reader_->viewscreen;
	}
	std::shared_ptr<const std::vector<SceneBody>> scene_snapshot() const override
	{
		const_cast<FrameSession *>(this)->scene_requested_ = true;
		return reader_->scene;
	}
	std::vector<SceneBody> scene() const override
	{
		const_cast<FrameSession *>(this)->scene_requested_ = true;
		return reader_->scene ? *reader_->scene : std::vector<SceneBody>{};
	}
	Json scene_lighting() const override
	{
		return reader_->lighting;
	}
	Json scene_presentation() const override
	{
		return reader_->presentation;
	}
	Json public_board() const override
	{
		return reader_->board;
	}
	Json inference_state() const override
	{
		const_cast<FrameSession *>(this)->inference_requested_ = true;
		return reader_->inference;
	}
	Json inspection() const override
	{
		const_cast<FrameSession *>(this)->inspection_requested_ = true;
		return reader_->inspection;
	}
	void add_pack(const Bytes &value) override
	{
		invoke(
		    [&](auto &s)
		    {
			    s.add_pack(value);
		    });
	}
	void clear_packs() override
	{
		invoke(
		    [](auto &s)
		    {
			    s.clear_packs();
		    });
	}
	void replace_packs(const std::vector<Bytes> &value) override
	{
		invoke(
		    [&](auto &s)
		    {
			    s.replace_packs(value);
		    });
	}
	void start(const GameSetup &value) override
	{
		invoke(
		    [&](auto &s)
		    {
			    s.start(value);
		    });
	}
	void prepare_lobby(const GameSetup &value) override
	{
		invoke(
		    [&](auto &s)
		    {
			    s.prepare_lobby(value);
		    });
	}
	bool set_player_profile(const std::string &name, const Bytes &avatar) override
	{
		return invoke(
		    [&](auto &s)
		    {
			    return s.set_player_profile(name, avatar);
		    });
	}
	bool set_local_player_profile(std::uint64_t route, const std::string &name, const Bytes &avatar) override
	{
		return invoke(
		    [&](auto &s)
		    {
			    return s.set_local_player_profile(route, name, avatar);
		    });
	}
	bool lobby_command(const LobbyCommand &value) override
	{
		return invoke(
		    [&](auto &s)
		    {
			    return s.lobby_command(value);
		    });
	}
	Bytes save() const override
	{
		return invoke(
		    [](auto &s)
		    {
			    return s.save();
		    });
	}
	Bytes fleet_setup(const GameSetup &value) const override
	{
		return invoke(
		    [&](auto &s)
		    {
			    return s.fleet_setup(value);
		    });
	}
	void restore(const Bytes &value) override
	{
		invoke(
		    [&](auto &s)
		    {
			    s.restore(value);
		    });
	}
	void resume_local() override
	{
		invoke(
		    [](auto &s)
		    {
			    s.resume_local();
		    });
	}
	void activate_prepared() override
	{
		invoke(
		    [](auto &s)
		    {
			    s.activate_prepared();
		    });
	}
	void authorise_lobby_member(const std::string &id, const LobbyAdmission &value) override
	{
		invoke(
		    [&](auto &s)
		    {
			    s.authorise_lobby_member(id, value);
		    });
	}
	bool command(const OperatingCommand &value) override
	{
		return invoke(
		    [&](auto &s)
		    {
			    return s.command(value);
		    });
	}
	bool console_command(std::uint64_t route, const OperatingCommand &value) override
	{
		return invoke(
		    [&](auto &s)
		    {
			    return s.console_command(route, value);
		    });
	}
	bool claim_console(std::uint64_t route, const ConsoleAddress &address) override
	{
		return invoke(
		    [&](auto &s)
		    {
			    const bool accepted = s.claim_console(route, address);
			    if (accepted)
				    routes_.insert(route);
			    return accepted;
		    });
	}
	bool release_console(std::uint64_t route) override
	{
		return invoke(
		    [&](auto &s)
		    {
			    const bool accepted = s.release_console(route);
			    if (accepted)
				    routes_.erase(route);
			    return accepted;
		    });
	}
	void set_inference_connection(std::shared_ptr<InferenceConnection> value) override
	{
		invoke(
		    [&](auto &s)
		    {
			    s.set_inference_connection(std::move(value));
		    });
	}
	bool inference_command(const Json &value) override
	{
		return invoke(
		    [&](auto &s)
		    {
			    return s.inference_command(value);
		    });
	}
	bool session_action(const std::string &value) override
	{
		return invoke(
		    [&](auto &s)
		    {
			    return s.session_action(value);
		    });
	}
};
} // namespace
#endif
std::shared_ptr<SessionPort> make_frame_session(std::shared_ptr<SessionPort> session)
{
#if defined(__EMSCRIPTEN__) && !defined(PHOENIX_BROWSER_THREADS)
	return session;
#else
	if (std::dynamic_pointer_cast<FrameSession>(session))
		return session;
	return std::make_shared<FrameSession>(std::move(session));
#endif
}
Json frame_session_profile(const std::shared_ptr<SessionPort> &session)
{
#if !defined(__EMSCRIPTEN__) || defined(PHOENIX_BROWSER_THREADS)
	if (const auto value = std::dynamic_pointer_cast<FrameSession>(session))
		return value->profile();
#endif
	return Json::object();
}
Bytes pause_frame_session_checkpoint(const std::shared_ptr<SessionPort> &session)
{
#if !defined(__EMSCRIPTEN__) || defined(PHOENIX_BROWSER_THREADS)
	if (const auto value = std::dynamic_pointer_cast<FrameSession>(session))
		return value->pause_checkpoint();
#endif
	throw std::runtime_error("Session does not own a background clock");
}
} // namespace phoenix
